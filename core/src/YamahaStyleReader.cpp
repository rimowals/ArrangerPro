// Yamaha style files: a Standard MIDI File (format 0) whose track holds all sections back
// to back, delimited by marker meta events ("SFF1"/"SFF2", "SInt", "Intro A", "Main A", ...),
// followed by the CASM chunk with per-channel transposition rules (Ctab for SFF1, Ctb2 for SFF2).
// Layout per "Style Files — Introduction and Details" (P. Wierzba, M. P. Bedesem).

#include "arranger/StyleLoader.h"

#include "ByteReader.h"
#include "StyleDefaults.h"

#include <algorithm>
#include <map>

namespace arr {

namespace {

using detail::ByteReader;

struct RawEvent {
    int64_t tick;
    uint8_t status;      // 0xFF = meta, 0xF0/0xF7 = sysex, otherwise channel status
    uint8_t data1 = 0, data2 = 0;
    std::string meta;    // meta payload
};

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

// ---------------------------------------------------------------- SMF

void readTrack(ByteReader r, std::vector<RawEvent>& out)
{
    int64_t tick = 0;
    uint8_t running = 0;

    while (!r.atEnd())
    {
        tick += r.varLen();
        uint8_t status = r.peek();
        if (status & 0x80)
            r.u8();
        else if (running)
            status = running;
        else
            throw StyleError("MIDI data without status byte");

        if (status == 0xFF)
        {
            uint8_t type = r.u8();
            uint32_t len = r.varLen();
            RawEvent e{tick, 0xFF, type};
            e.meta = r.ascii(len);
            out.push_back(std::move(e)); // end-of-track is kept: it marks the end of the last section
            if (type == 0x2F)
                return;
        }
        else if (status == 0xF0 || status == 0xF7)
        {
            r.skip(r.varLen()); // sysex is not needed: setups are sent as channel messages
        }
        else
        {
            running = status;
            RawEvent e{tick, status, r.u8()};
            uint8_t type = status & 0xF0;
            if (type != 0xC0 && type != 0xD0)
                e.data2 = r.u8();
            out.push_back(std::move(e));
        }
    }
}

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
        auto b = d.subspan(22 + 6 * i, 6);
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

// ---------------------------------------------------------------- assembly

struct PairedNote {
    int64_t tick;
    int64_t end;
    int channel;
    uint8_t key, velocity;
};

std::vector<PairedNote> pairNotes(const std::vector<RawEvent>& events, int64_t trackEnd)
{
    std::vector<PairedNote> notes;
    std::map<int, std::vector<size_t>> open; // (channel << 7 | key) → indices of sounding notes, FIFO

    for (const auto& e : events)
    {
        uint8_t type = e.status & 0xF0;
        if (type != 0x80 && type != 0x90)
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

bool isControlEvent(const RawEvent& e)
{
    uint8_t type = e.status & 0xF0;
    return e.status < 0xF0 && type != 0x80 && type != 0x90;
}

Part buildPart(const CtabEntry& entry, int64_t start, int64_t end,
               const std::vector<PairedNote>& notes, const std::vector<RawEvent>& events)
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
        if (isControlEvent(e) && (e.status & 0x0F) == entry.sourceChannel && e.tick >= start && e.tick < end)
            part.controls.push_back({e.tick - start, e.status, e.data1, e.data2});

    return part;
}

} // namespace

Style readYamahaStyle(std::span<const uint8_t> file, std::string name)
{
    Style style;
    style.name = std::move(name);

    ByteReader r(file);
    if (r.remaining() < 14 || r.ascii(4) != "MThd")
        throw StyleError("not a Yamaha style (missing MThd)");
    uint32_t headerLen = r.u32();
    r.u16(); // format
    uint16_t trackCount = r.u16();
    uint16_t division = r.u16();
    r.skip(headerLen - 6);
    if (division == 0 || (division & 0x8000))
        throw StyleError("unsupported MIDI time division");

    std::vector<RawEvent> events;
    Casm casm;
    bool hasCasm = false;
    int tracksRead = 0;

    while (r.remaining() >= 8)
    {
        std::string id = r.ascii(4);
        if (!std::all_of(id.begin(), id.end(), [](char c) { return c >= 0x20 && c < 0x7F; }))
            break; // trailing garbage
        uint32_t len = r.u32();
        auto body = r.bytes(std::min<size_t>(len, r.remaining()));

        if (id == "MTrk" && tracksRead < trackCount)
        {
            readTrack(ByteReader(body), events);
            ++tracksRead;
        }
        else if (id == "CASM")
        {
            casm = readCasm(ByteReader(body), style.warnings);
            hasCasm = true;
        }
    }
    if (tracksRead == 0)
        throw StyleError("style has no MIDI track");

    // Rescale to the internal resolution and order events of all tracks.
    for (auto& e : events)
        e.tick = (e.tick * kPpq + division / 2) / division;
    std::stable_sort(events.begin(), events.end(), [](const RawEvent& a, const RawEvent& b) { return a.tick < b.tick; });

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
            uint32_t us = (uint8_t(e.meta[0]) << 16) | (uint8_t(e.meta[1]) << 8) | uint8_t(e.meta[2]);
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
                if (isControlEvent(e) && e.tick >= start && e.tick < end && (e.status & 0x0F) >= 8)
                    style.setup[(e.status & 0x0F) - 8].push_back({0, e.status, e.data1, e.data2});
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
        style.sections[int(*id)] = std::move(section);
    }

    bool anySection = std::any_of(style.sections.begin(), style.sections.end(), [](const auto& s) { return s.has_value(); });
    if (!anySection)
        throw StyleError("no style sections found (missing markers)");
    return style;
}

} // namespace arr
