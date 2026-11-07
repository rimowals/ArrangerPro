// Yamaha style files: a Standard MIDI File (format 0) whose track holds all sections back
// to back, delimited by marker meta events ("SFF1"/"SFF2", "SInt", "Intro A", "Main A", ...),
// followed by the CASM chunk with per-channel transposition rules (Ctab for SFF1, Ctb2 for SFF2)
// and the OTSc chunk with the One Touch Settings.
// Layout per "Style Files — Introduction and Details" (P. Wierzba, M. P. Bedesem).

#include "arranger/StyleLoader.h"

#include "ByteReader.h"
#include "Smf.h"
#include "StyleDefaults.h"

#include <algorithm>
#include <map>

namespace arr {

namespace {

using detail::ByteReader;
using detail::SmfEvent;
using detail::isChannelControl;

struct Marker {
    int64_t tick;
    std::string name;
};

struct CtabEntry {
    int sourceChannel = 0;
    int destChannel = 0;
    TransposeRule rule;
};

using Casm = std::map<SectionId, std::vector<CtabEntry>>;

// ---------------------------------------------------------------- CASM

Ntt sff2Ntt(Ntr ntr, uint8_t value)
{
    static constexpr Ntt kNtt[] = {
        Ntt::Bypass, Ntt::Melody, Ntt::Chord, Ntt::MelodicMinor, Ntt::MelodicMinor5th,
        Ntt::HarmonicMinor, Ntt::HarmonicMinor5th, Ntt::NaturalMinor, Ntt::NaturalMinor5th,
        Ntt::Dorian, Ntt::Dorian5th};
    static constexpr Ntt kGuitar[] = {Ntt::GuitarAllPurpose, Ntt::GuitarStroke, Ntt::GuitarArpeggio};

    if (ntr == Ntr::Guitar)
        return value < std::size(kGuitar) ? kGuitar[value] : Ntt::GuitarAllPurpose;
    return value < std::size(kNtt) ? kNtt[value] : Ntt::Melody;
}

Ntr toNtr(uint8_t v) { return v == 1 ? Ntr::RootFixed : v == 2 ? Ntr::Guitar : Ntr::RootTrans; }
Rtr toRtr(uint8_t v) { return v <= 5 ? Rtr(v) : Rtr::PitchShift; }

// Bytes 0..19 are common to Ctab and Ctb2.
CtabEntry readCtabHeader(std::span<const uint8_t> d)
{
    CtabEntry e;
    e.sourceChannel = d[0] & 0x0F;
    e.destChannel = d[9] & 0x0F;
    // Note mute: byte 11 bits 3..0 = B..G#, byte 12 bits 7..0 = G..C.
    e.rule.noteMute = uint16_t(((d[11] & 0x0F) << 8) | d[12]);
    // Chord mute: 34 bits, byte 13 bits 1..0 = types 33..32, ..., byte 17 bits 7..0 = types 7..0.
    e.rule.chordMute = (uint64_t(d[13] & 0x03) << 32) | (uint64_t(d[14]) << 24) | (uint64_t(d[15]) << 16)
                     | (uint64_t(d[16]) << 8) | d[17];
    e.rule.sourceRoot = d[18] % 12;
    e.rule.sourceType = d[19] < kChordTypeCount ? ChordType(d[19]) : ChordType::Maj7;
    return e;
}

std::optional<CtabEntry> readCtab(std::span<const uint8_t> d)
{
    if (d.size() < 26)
        return std::nullopt;
    CtabEntry e = readCtabHeader(d);
    TransposeRange& m = e.rule.mid;
    m.ntr = toNtr(d[20]);
    // SFF1 NTT: 0 bypass, 1 melody, 2 chord, 3 bass, 4 melodic minor, 5 harmonic minor.
    switch (d[21])
    {
        case 0: m.ntt = Ntt::Bypass; break;
        case 2: m.ntt = Ntt::Chord; break;
        case 3: m.ntt = Ntt::Melody; m.bassOn = true; break;
        case 4: m.ntt = Ntt::MelodicMinor; break;
        case 5: m.ntt = Ntt::HarmonicMinor; break;
        default: m.ntt = Ntt::Melody; break;
    }
    m.highKey = d[22] % 12;
    m.noteLow = d[23] & 0x7F;
    m.noteHigh = d[24] & 0x7F;
    m.rtr = toRtr(d[25]);
    return e;
}

std::optional<CtabEntry> readCtb2(std::span<const uint8_t> d)
{
    if (d.size() < 40)
        return std::nullopt;
    CtabEntry e = readCtabHeader(d);
    e.rule.hasRanges = true;
    e.rule.midLow = d[20] & 0x7F;
    e.rule.midHigh = d[21] & 0x7F;

    // Three 6-byte blocks: low, middle and high range of the chord root note.
    TransposeRange* ranges[3] = {&e.rule.low, &e.rule.mid, &e.rule.high};
    for (int i = 0; i < 3; ++i)
    {
        auto b = d.subspan(size_t(22 + 6 * i), 6);
        TransposeRange& r = *ranges[i];
        r.ntr = toNtr(b[0]);
        r.ntt = sff2Ntt(r.ntr, b[1] & 0x7F);
        r.bassOn = (b[1] & 0x80) != 0;
        r.highKey = b[2] % 12;
        r.noteLow = b[3] & 0x7F;
        r.noteHigh = b[4] & 0x7F;
        r.rtr = toRtr(b[5]);
    }
    return e;
}

void readCseg(ByteReader r, Casm& casm, std::vector<std::string>& warnings)
{
    std::vector<SectionId> sections;
    std::vector<CtabEntry> entries;

    while (r.remaining() >= 8)
    {
        std::string id = r.ascii(4);
        uint32_t len = r.u32();
        auto body = r.bytes(std::min<size_t>(len, r.remaining()));

        if (id == "Sdec")
        {
            std::string list(body.begin(), body.end());
            size_t start = 0;
            while (start <= list.size())
            {
                size_t comma = list.find(',', start);
                std::string name = list.substr(start, comma == std::string::npos ? std::string::npos : comma - start);
                if (auto s = sectionFromYamahaMarker(name))
                    sections.push_back(*s);
                if (comma == std::string::npos)
                    break;
                start = comma + 1;
            }
        }
        else if (id == "Ctab" || id == "Ctb2")
        {
            auto entry = id == "Ctab" ? readCtab(body) : readCtb2(body);
            if (entry)
                entries.push_back(*entry);
            else
                warnings.push_back("short " + id + " record ignored");
        }
        else if (id == "Cntt" && body.size() >= 2)
        {
            // Extended NTT for the channel's preceding Ctab (SFF2 numbering, bit 7 = bass on).
            for (auto it = entries.rbegin(); it != entries.rend(); ++it)
                if (it->sourceChannel == (body[0] & 0x0F))
                {
                    it->rule.mid.ntt = sff2Ntt(it->rule.mid.ntr, body[1] & 0x7F);
                    it->rule.mid.bassOn = (body[1] & 0x80) != 0;
                    break;
                }
        }
    }

    for (SectionId s : sections)
    {
        auto& list = casm[s];
        list.insert(list.end(), entries.begin(), entries.end());
    }
}

Casm readCasm(ByteReader r, std::vector<std::string>& warnings)
{
    Casm casm;
    while (r.remaining() >= 8)
    {
        std::string id = r.ascii(4);
        uint32_t len = r.u32();
        auto body = r.bytes(std::min<size_t>(len, r.remaining()));
        if (id == "CSEG")
            readCseg(ByteReader(body), casm, warnings);
    }
    return casm;
}

// ---------------------------------------------------------------- OTS

// OTS tracks address the keyboard parts by channel: 1 = Right1, 2 = Right2, 3 = Right3, 4 = Left
// (in real styles channel 4 keeps the same pad/strings voice across all four OTS).
constexpr int kOtsPartOfChannel[4] = {int(KeyboardPart::Right1), int(KeyboardPart::Right2),
                                      int(KeyboardPart::Right3), int(KeyboardPart::Left)};

OneTouchSetting readOtsTrack(std::span<const uint8_t> track)
{
    std::vector<SmfEvent> events;
    detail::readTrackEvents(track, events);

    OneTouchSetting ots;
    for (const auto& e : events)
    {
        const int channel = e.status & 0x0F;
        if (!isChannelControl(e) || channel >= 4)
            continue;
        PartVoice& v = ots.parts[size_t(kOtsPartOfChannel[channel])];
        v.on = true;
        if ((e.status & 0xF0) == 0xC0)
        {
            v.program = e.data1;
            v.hasVoice = true;
        }
        else if ((e.status & 0xF0) == 0xB0)
            switch (e.data1)
            {
                case 0: v.bankMsb = e.data2; break;
                case 32: v.bankLsb = e.data2; break;
                case 7: v.volume = e.data2; break;
                case 10: v.pan = e.data2; break;
                case 91: v.reverb = e.data2; break;
                case 93: v.chorus = e.data2; break;
                default: break;
            }
    }
    return ots;
}

std::vector<OneTouchSetting> readOts(std::span<const uint8_t> body)
{
    std::vector<OneTouchSetting> result;
    ByteReader r(body);
    while (r.remaining() >= 8 && result.size() < 4)
    {
        std::string id = r.ascii(4);
        uint32_t len = r.u32();
        auto track = r.bytes(std::min<size_t>(len, r.remaining()));
        if (id == "MTrk")
            result.push_back(readOtsTrack(track));
    }
    return result;
}

// ---------------------------------------------------------------- assembly

struct PairedNote {
    int64_t tick;
    int64_t end;
    int channel;
    uint8_t key, velocity;
};

std::vector<PairedNote> pairNotes(const std::vector<SmfEvent>& events, int64_t trackEnd)
{
    std::vector<PairedNote> notes;
    std::map<int, std::vector<size_t>> open; // (channel << 7 | key) → indices of sounding notes, FIFO

    for (const auto& e : events)
    {
        uint8_t type = e.status & 0xF0;
        if (e.status == 0xFF || (type != 0x80 && type != 0x90))
            continue;
        int channel = e.status & 0x0F;
        int id = (channel << 7) | e.data1;

        if (type == 0x90 && e.data2 > 0)
        {
            open[id].push_back(notes.size());
            notes.push_back({e.tick, -1, channel, e.data1, e.data2});
        }
        else if (auto it = open.find(id); it != open.end() && !it->second.empty())
        {
            notes[it->second.front()].end = e.tick;
            it->second.erase(it->second.begin());
        }
    }
    for (auto& n : notes)
        if (n.end < 0)
            n.end = trackEnd;
    return notes;
}

Part buildPart(const CtabEntry& entry, int64_t start, int64_t end,
               const std::vector<PairedNote>& notes, const std::vector<SmfEvent>& events)
{
    Part part;
    part.role = PartRole(entry.destChannel - 8);
    part.sourceChannel = entry.sourceChannel;
    part.isDrum = part.role == PartRole::Rhythm || part.role == PartRole::SubRhythm;
    part.rule = entry.rule;

    for (const auto& n : notes)
        if (n.channel == entry.sourceChannel && n.tick >= start && n.tick < end)
        {
            int64_t length = std::min(n.end, end) - n.tick;
            part.notes.push_back({n.tick - start, int32_t(std::max<int64_t>(length, 1)), n.key, n.velocity});
        }

    for (const auto& e : events)
        if (isChannelControl(e) && (e.status & 0x0F) == entry.sourceChannel && e.tick >= start && e.tick < end)
            part.controls.push_back({e.tick - start, e.status, e.data1, e.data2});

    return part;
}

} // namespace

Style readYamahaStyle(std::span<const uint8_t> file, std::string name)
{
    Style style;
    style.name = std::move(name);

    detail::SmfFile smf = detail::readSmf(file);
    detail::rescaleToPpq(smf.events, smf.division);
    const auto& events = smf.events;

    Casm casm;
    bool hasCasm = false;
    for (const auto& chunk : smf.extra)
    {
        if (chunk.id == "CASM")
        {
            casm = readCasm(ByteReader(chunk.body), style.warnings);
            hasCasm = true;
        }
        else if (chunk.id == "OTSc")
            style.ots = readOts(chunk.body);
    }

    const int64_t trackEnd = events.empty() ? 0 : events.back().tick;

    std::vector<Marker> markers;
    bool tempoSet = false, timeSigSet = false;
    for (const auto& e : events)
    {
        if (e.status != 0xFF)
            continue;
        if (e.data1 == 0x06)
            markers.push_back({e.tick, e.meta});
        else if (e.data1 == 0x51 && e.meta.size() == 3 && !tempoSet)
        {
            uint32_t us = (uint32_t(uint8_t(e.meta[0])) << 16) | (uint32_t(uint8_t(e.meta[1])) << 8) | uint8_t(e.meta[2]);
            if (us > 0)
                style.tempo = 60'000'000.0 / us;
            tempoSet = true;
        }
        else if (e.data1 == 0x58 && e.meta.size() >= 2 && !timeSigSet)
        {
            style.timeSigNum = std::max(1, int(uint8_t(e.meta[0])));
            style.timeSigDen = 1 << std::min(5, int(uint8_t(e.meta[1])));
            timeSigSet = true;
        }
        else if (e.data1 == 0x03 && style.name.empty())
            style.name = e.meta;
    }

    style.format = StyleFormat::YamahaSff1;
    for (const auto& m : markers)
        if (m.name == "SFF2")
            style.format = StyleFormat::YamahaSff2;

    const auto pairedNotes = pairNotes(events, trackEnd);

    for (size_t i = 0; i < markers.size(); ++i)
    {
        const int64_t start = markers[i].tick;
        int64_t end = trackEnd;
        for (size_t j = i + 1; j < markers.size(); ++j)
            if (markers[j].tick > start)
            {
                end = markers[j].tick;
                break;
            }

        const std::string& marker = markers[i].name;
        if (marker == "SFF1" || marker == "SFF2")
            continue;

        if (marker == "SInt")
        {
            for (const auto& e : events)
                if (isChannelControl(e) && e.tick >= start && e.tick < end && (e.status & 0x0F) >= 8)
                    style.setup[size_t((e.status & 0x0F) - 8)].push_back({0, e.status, e.data1, e.data2});
            continue;
        }

        auto id = sectionFromYamahaMarker(marker);
        if (!id)
        {
            style.warnings.push_back("unknown marker '" + marker + "' ignored");
            continue;
        }
        if (end <= start)
            continue;

        std::vector<CtabEntry> entries;
        if (auto it = casm.find(*id); hasCasm && it != casm.end())
            entries = it->second;
        else
            for (int ch = 8; ch < 16; ++ch)
                entries.push_back({ch, ch, detail::defaultRule(PartRole(ch - 8))});

        Section section;
        section.id = *id;
        section.length = end - start;
        ChordVariation variation;
        for (const auto& entry : entries)
        {
            if (entry.destChannel < 8)
                continue; // parts routed below channel 9 are not accompaniment parts
            Part part = buildPart(entry, start, end, pairedNotes, events);
            if (!part.notes.empty() || !part.controls.empty())
                variation.parts.push_back(std::move(part));
        }
        section.variations.push_back(std::move(variation));
        style.sections[size_t(*id)] = std::move(section);
    }

    bool anySection = std::any_of(style.sections.begin(), style.sections.end(), [](const auto& s) { return s.has_value(); });
    if (!anySection)
        throw StyleError("no style sections found (missing markers)");
    return style;
}

} // namespace arr
