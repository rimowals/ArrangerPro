// KORG Pa-series style banks (Pa50/60/80/500/...): a chunked container with a table of
// contents and one OC31-compressed chunk tree per style. Structures as documented by the
// KORG-Tools / libkorg project. Each style element (Variation 1-4, Intro, Fill, Ending...)
// holds several chord variations; a "master track" per chord variation starts and stops
// the shared MIDI tracks of the style.

#include "arranger/StyleLoader.h"

#include "ByteReader.h"
#include "Oc31.h"
#include "StyleDefaults.h"

#include <algorithm>
#include <map>

namespace arr {

namespace {

using detail::ByteReader;

constexpr int kKorgPpq = 384;
constexpr int kScale = kPpq / kKorgPpq;

// ---------------------------------------------------------------- raw model

enum class KEv : uint8_t { NoteOff = 0, NoteOn = 1, Control = 3, Aftertouch = 5, Bend = 6, Meta = 9, NoiseOff = 12, NoiseOn = 13, Delta = 100 };

struct KEvent {
    KEv type;
    int value1 = 0;     // note / CC number, signed bend value, or delta ticks
    uint8_t value2 = 0; // velocity / CC value
    uint8_t metaType = 0;
    std::vector<uint8_t> meta;
};

using KTrack = std::vector<KEvent>;

// Accompaniment part of a chord-variation slot.
enum KTrackNumber : uint8_t { KDrum, KPerc, KBass, KAcc1, KAcc2, KAcc3, KAcc4, KAcc5 };

struct KTrackData {
    uint8_t expression = 0, bankMsb = 0, bankLsb = 0, program = 0;
    uint8_t rangeBottom = 0, rangeTop = 127;
    uint8_t ntt = 0; // bits 5-7: type (0 = parallel, 1 = fixed), bits 0-4: table
};

struct KChordVariation {
    bool hasData = false;
    KTrack master;
    std::vector<KTrackNumber> slotTrack; // slot → part
};

struct KElement {
    bool hasData = false;
    uint8_t cvWithData = 0;
    uint8_t timeSig = 0;
    std::vector<uint8_t> chordTable; // chord-type group → chord variation index
    KTrackData tracks[8];
    std::vector<KChordVariation> cvs;
};

struct KStyle {
    std::string tocName;                // name in the bank's table of contents
    std::string name;                   // name stored in the style itself
    uint16_t elementsWithData = 0;
    std::vector<uint16_t> trackMapping; // one-based MIDI track index per CV slot (all elements, file order)
    std::vector<KTrack> midiTracks;
    KElement elements[15];              // Variation 1-4, then the elements listed in kOtherElements
    std::vector<uint8_t> sts;           // StylePerformances object, used for the tempo
};

struct ChunkHeader {
    uint8_t type, verMajor, verMinor, flags;
    std::span<const uint8_t> body;
};

ChunkHeader readChunk(ByteReader& r)
{
    uint32_t id = r.u32();
    uint32_t size = r.u32();
    return {uint8_t(id >> 24), uint8_t(id >> 16), uint8_t(id >> 8), uint8_t(id), r.bytes(size)};
}

// ---------------------------------------------------------------- object parsing

void readEvents(KTrack& track, ByteReader& r)
{
    int remaining = r.u16();
    while (remaining > 0)
    {
        int type = r.u8();
        --remaining;

        if (type & 0x80)
        {
            int delta = 0;
            while (type & 0x80)
            {
                delta = (delta << 7) | (type & 0x7F);
                type = r.u8();
                --remaining;
            }
            track.push_back({KEv::Delta, delta});
        }
        if (type & 0x40)
        {
            type &= ~0x40;
            r.u8(); // meaning unknown (also to libkorg)
            --remaining;
        }

        switch (type)
        {
            case 0: case 1: case 3: case 12: case 13:
            {
                uint8_t v1 = r.u8(), v2 = r.u8();
                remaining -= 2;
                track.push_back({KEv(type), v1, uint8_t(v2 & 0x7F)});
                break;
            }
            case 5:
                track.push_back({KEv::Aftertouch, r.u8() & 0x7F});
                --remaining;
                break;
            case 6:
            {
                uint8_t lo = r.u8(), hi = r.u8();
                remaining -= 2;
                track.push_back({KEv::Bend, (((hi & 0x7F) << 7) | lo) - 8192});
                break;
            }
            case 9:
            {
                KEvent e{KEv::Meta};
                e.metaType = r.u8();
                int hi = r.u8(), lo = r.u8();
                int len = (hi << 7) | (lo & 0x7F);
                remaining -= 3 + len;
                auto data = r.bytes(size_t(len));
                e.meta.assign(data.begin(), data.end());
                bool end = e.metaType == 0x2F;
                track.push_back(std::move(e));
                if (end)
                    return;
                break;
            }
            default:
                throw StyleError("unknown KORG MIDI event type " + std::to_string(type));
        }
    }
}

void readTrackList(KStyle& style, ByteReader r)
{
    while (!r.atEnd())
    {
        auto c = readChunk(r);
        ByteReader b(c.body);
        switch (c.type)
        {
            case 1: // track mapping
            {
                b.u16();
                int n = b.u16();
                for (int i = 0; i < n; ++i)
                    style.trackMapping.push_back(b.u16());
                break;
            }
            case 2: // drum / percussion track
            {
                KTrack t;
                b.u16(); b.u8(); b.u8(); b.u16();
                readEvents(t, b);
                style.midiTracks.push_back(std::move(t));
                break;
            }
            case 3: case 4: case 5: // bass, accompaniment, guitar track
            {
                KTrack t;
                b.u16(); b.u8(); b.u8(); b.u8(); b.u8();
                readEvents(t, b);
                style.midiTracks.push_back(std::move(t));
                break;
            }
            default:
                break;
        }
    }
}

void readElement(KElement& element, ByteReader r)
{
    int cvCursor = -1;
    while (!r.atEnd())
    {
        auto c = readChunk(r);
        ByteReader b(c.body);
        switch (c.type)
        {
            case 1: // element info
            {
                element.cvWithData = b.u8();
                const bool nttPerElement = c.verMajor == 0;
                uint8_t elementNtt = nttPerElement ? b.u8() : 0;
                element.timeSig = b.u8();
                int n = b.u8();
                auto table = b.bytes(size_t(n));
                element.chordTable.assign(table.begin(), table.end());
                if (nttPerElement)
                    for (auto& t : element.tracks)
                        t.ntt = elementNtt;
                break;
            }
            case 2: // per-part track data (fields accumulate with the minor version)
            {
                for (auto& t : element.tracks)
                {
                    t.expression = b.u8();
                    t.bankMsb = b.u8();
                    t.bankLsb = b.u8();
                    t.program = b.u8();
                    t.rangeBottom = b.u8();
                    t.rangeTop = b.u8();
                }
                if (c.verMinor >= 1) for (int i = 0; i < 8; ++i) b.skip(3);
                if (c.verMinor >= 2) for (auto& t : element.tracks) t.ntt = b.u8();
                break;
            }
            case 3: // master track of the next chord variation that has data
            {
                do { ++cvCursor; }
                while (cvCursor < int(element.cvs.size()) && !(element.cvWithData & (1 << cvCursor)));
                if (cvCursor >= int(element.cvs.size()))
                    throw StyleError("more master tracks than chord variations");

                auto& cv = element.cvs[size_t(cvCursor)];
                cv.hasData = true;
                b.u16(); b.u8(); b.u8(); b.u8(); b.u8();
                readEvents(cv.master, b);
                int n = b.u8();
                for (int i = 0; i < n; ++i)
                {
                    b.u8(); // slot type
                    cv.slotTrack.push_back(KTrackNumber(std::min<int>(b.u8(), KAcc5)));
                }
                break;
            }
            default:
                break;
        }
    }
}

void readStyleChunk(KStyle& style, const ChunkHeader& c, ByteReader& b, int& elementCursor)
{
    switch (c.type)
    {
        case 1: // style info
        {
            int nameLen = b.u8();
            std::string name = b.ascii(size_t(nameLen));
            while (!name.empty() && (name.back() == ' ' || name.back() == '\0'))
                name.pop_back();
            if (!name.empty())
                style.name = name;
            b.u16(); b.u8();
            uint16_t enabled = b.u16();
            // Older versions end inside the 11 unknown bytes that precede "elements with data".
            if (c.verMinor > 2 && b.remaining() >= 13)
            {
                b.skip(11);
                enabled = b.u16();
            }
            style.elementsWithData = enabled;
            break;
        }
        case 2:
            readTrackList(style, b);
            break;
        case 3:
        {
            do { ++elementCursor; }
            while (elementCursor < 15 && !(style.elementsWithData & (1 << elementCursor)));
            if (elementCursor >= 15)
                throw StyleError("more style elements than declared");
            auto& element = style.elements[elementCursor];
            element.hasData = true;
            element.cvs.resize(elementCursor < 4 ? 6 : 2);
            readElement(element, b);
            break;
        }
        default:
            break;
    }
}

void readStyleObject(KStyle& style, std::span<const uint8_t> object)
{
    ByteReader top(object);
    auto root = readChunk(top);
    if (root.type != 1)
        throw StyleError("style object: unexpected root chunk");

    ByteReader r(root.body);
    int elementCursor = -1;
    while (!r.atEnd())
    {
        auto c = readChunk(r);
        ByteReader b(c.body);
        try
        {
            readStyleChunk(style, c, b, elementCursor);
        }
        catch (const StyleError& e)
        {
            throw StyleError("chunk type " + std::to_string(c.type) + " v" + std::to_string(c.verMajor) + "."
                             + std::to_string(c.verMinor) + ": " + e.what());
        }
    }
}

// ---------------------------------------------------------------- conversion

// Element slots 4.. in file order, and the section each one becomes.
constexpr SectionId kElementSection[15] = {
    SectionId::MainA, SectionId::MainB, SectionId::MainC, SectionId::MainD,
    SectionId::IntroA, SectionId::IntroB, SectionId::FillAA, SectionId::FillBB,
    SectionId::EndingA, SectionId::EndingB, SectionId::Break, SectionId::IntroC,
    SectionId::EndingC, SectionId::FillCC, SectionId::FillDD,
};

constexpr PartRole kRoleOf[8] = {
    PartRole::Rhythm, PartRole::SubRhythm, PartRole::Bass,
    PartRole::Chord1, PartRole::Chord2, PartRole::Pad, PartRole::Phrase1, PartRole::Phrase2,
};

// Positions in the chord table for the three chord groups the reader distinguishes.
// (Major = 0, minor = 7, seventh = 12 have been confirmed on Pa50 banks; the remaining
// entries are finer chord types that fall back to these groups.)
constexpr int kTableMajor = 0, kTableMinor = 7, kTableSeventh = 12;

int tableGroupFor(ChordType type)
{
    const auto& info = chordTypeInfo(type);
    if (info.family == ChordFamily::Minor || info.family == ChordFamily::Diminished)
        return kTableMinor;
    if (info.roles[RoleSeventh] == 10)
        return kTableSeventh;
    return kTableMajor;
}

// The CV slot mapping is indexed by the running count of slots over all elements in file order.
int globalSlotBase(const KStyle& style, int element, int cv)
{
    int base = 0;
    for (int e = 0; e < 15; ++e)
    {
        const auto& cvs = style.elements[e].cvs;
        int upTo = e == element ? cv : int(cvs.size());
        for (int i = 0; i < upTo && i < int(cvs.size()); ++i)
            base += int(cvs[size_t(i)].slotTrack.size());
        if (e == element)
            break;
    }
    return base;
}

TransposeRule ruleFor(KTrackNumber track, const KTrackData& data, ChordType sourceType)
{
    PartRole role = kRoleOf[track];
    TransposeRule rule = detail::defaultRule(role);
    rule.sourceType = sourceType;
    TransposeRange& r = rule.mid;
    r.highKey = 5;

    if (track >= KAcc1)
    {
        const bool fixed = (data.ntt >> 5) != 0;
        r.ntr = fixed ? Ntr::RootFixed : Ntr::RootTrans;
        r.ntt = fixed ? Ntt::Chord : Ntt::Melody;
    }
    if (track != KDrum && track != KPerc && data.rangeBottom < data.rangeTop && data.rangeTop <= 127)
    {
        r.noteLow = data.rangeBottom;
        r.noteHigh = data.rangeTop;
    }
    return rule;
}

// Renders one MIDI track into a part, played from `offset` and cut at `clip` (both Korg ticks).
void mergeTrack(const KTrack& track, int64_t offset, int64_t clip, Part& part)
{
    int64_t t = 0;
    std::map<int, std::vector<size_t>> open;

    for (const auto& e : track)
    {
        if (e.type == KEv::Delta)
        {
            t += e.value1;
            continue;
        }
        const int64_t at = offset + t;
        switch (e.type)
        {
            case KEv::NoteOn:
            case KEv::NoiseOn:
                if (at < clip && e.value2 > 0)
                {
                    open[e.value1].push_back(part.notes.size());
                    part.notes.push_back({at * kScale, -1, uint8_t(e.value1), e.value2});
                    break;
                }
                [[fallthrough]];
            case KEv::NoteOff:
            case KEv::NoiseOff:
                if (auto it = open.find(e.value1); it != open.end() && !it->second.empty())
                {
                    Note& n = part.notes[it->second.front()];
                    n.length = int32_t(std::max<int64_t>(std::min(at, clip) * kScale - n.tick, 1));
                    it->second.erase(it->second.begin());
                }
                break;
            case KEv::Control:
                if (at < clip)
                    part.controls.push_back({at * kScale, 0xB0, uint8_t(e.value1), e.value2});
                break;
            case KEv::Bend:
                if (at < clip)
                {
                    int v = std::clamp(e.value1 + 8192, 0, 16383);
                    part.controls.push_back({at * kScale, 0xE0, uint8_t(v & 0x7F), uint8_t(v >> 7)});
                }
                break;
            case KEv::Aftertouch:
                if (at < clip)
                    part.controls.push_back({at * kScale, 0xD0, uint8_t(e.value1), 0});
                break;
            default:
                break;
        }
    }
    for (auto& [key, list] : open)
        for (size_t i : list)
            part.notes[i].length = int32_t(std::max<int64_t>(clip * kScale - part.notes[i].tick, 1));
}

std::optional<ChordVariation> buildVariation(const KStyle& style, int elementIndex, int cvIndex,
                                             ChordType sourceType, int64_t& sectionEnd, std::vector<std::string>& warnings)
{
    const KElement& element = style.elements[elementIndex];
    const KChordVariation& cv = element.cvs[size_t(cvIndex)];

    struct Command { int64_t tick; bool play; int slot; };
    std::vector<Command> commands;
    int64_t t = 0, end = 0;
    for (const auto& e : cv.master)
    {
        if (e.type == KEv::Delta)
            t += e.value1;
        else if (e.type == KEv::Meta && e.metaType == 0x2F)
            end = std::max(end, t);
        else if (e.type == KEv::Meta && e.metaType == 0x7E && e.meta.size() >= 2)
            commands.push_back({t, e.meta[0] == 3, e.meta[1]});
    }
    if (end == 0)
        end = t;
    sectionEnd = std::max(sectionEnd, end * kScale);

    const int base = globalSlotBase(style, elementIndex, cvIndex);
    std::map<int, Part> partsBySlot;

    for (const auto& cmd : commands)
    {
        if (!cmd.play)
            continue;
        if (cmd.slot >= int(cv.slotTrack.size()))
        {
            warnings.push_back("master track refers to a missing slot");
            continue;
        }
        size_t mapIndex = size_t(base + cmd.slot);
        if (mapIndex >= style.trackMapping.size())
            continue;
        int midiTrack = int(style.trackMapping[mapIndex]) - 1;
        if (midiTrack < 0 || midiTrack >= int(style.midiTracks.size()))
            continue;

        int64_t clip = end;
        for (const auto& other : commands)
            if (!other.play && other.slot == cmd.slot && other.tick > cmd.tick)
                clip = std::min(clip, other.tick);

        const KTrackNumber number = cv.slotTrack[size_t(cmd.slot)];
        auto [it, inserted] = partsBySlot.try_emplace(cmd.slot);
        Part& part = it->second;
        if (inserted)
        {
            part.role = kRoleOf[number];
            part.sourceChannel = number;
            part.isDrum = number == KDrum || number == KPerc;
            part.rule = ruleFor(number, element.tracks[number], sourceType);
        }
        mergeTrack(style.midiTracks[size_t(midiTrack)], cmd.tick, clip, part);
    }

    ChordVariation variation;
    for (auto& [slot, part] : partsBySlot)
    {
        std::sort(part.notes.begin(), part.notes.end(), [](const Note& a, const Note& b) { return a.tick < b.tick; });
        std::stable_sort(part.controls.begin(), part.controls.end(),
                         [](const ChannelEvent& a, const ChannelEvent& b) { return a.tick < b.tick; });
        variation.parts.push_back(std::move(part));
    }
    if (variation.parts.empty())
        return std::nullopt;
    return variation;
}

// StylePerformances (STS) object. Not documented publicly; layout found on Pa50 banks:
// every track's sound record contains the marker 7F 15 6C followed by
//   +3 bank MSB (GM2: 120 kits / 121 melodic), +4 bank LSB, +6 program, +7 volume, +8 pan,
//   +10 transpose (signed semitones)
// and is preceded by three header bytes. The object holds 8 accompaniment records, then one
// block of 4 keyboard records (Upper 1, Upper 2, Upper 3, Lower) per STS. In the header,
// bit 2 of the second byte marks a playing track (Upper 1 has it in every STS of every
// Pa50 style examined; to be confirmed on the keyboard).
std::vector<OneTouchSetting> readSts(const std::vector<uint8_t>& sts)
{
    std::vector<size_t> records;
    for (size_t i = 3; i + 11 <= sts.size(); ++i)
        if (sts[i] == 0x7F && sts[i + 1] == 0x15 && sts[i + 2] == 0x6C && (sts[i + 3] == 120 || sts[i + 3] == 121))
            records.push_back(i);

    constexpr size_t kAccompanimentRecords = 8;
    if (records.size() < kAccompanimentRecords + 4)
        return {};

    static constexpr KeyboardPart kOrder[4] = {KeyboardPart::Right1, KeyboardPart::Right2, KeyboardPart::Right3, KeyboardPart::Left};
    std::vector<OneTouchSetting> result;
    for (size_t first = kAccompanimentRecords; first + 4 <= records.size() && result.size() < 4; first += 4)
    {
        OneTouchSetting ots;
        for (size_t p = 0; p < 4; ++p)
        {
            const size_t i = records[first + p];
            PartVoice& v = ots.parts[size_t(kOrder[p])];
            const bool drums = sts[i + 3] == 120;
            v.on = (sts[i - 2] & 0x04) != 0;
            v.hasVoice = true;
            v.bankMsb = drums ? 127 : 0;
            v.bankLsb = 0;
            v.program = sts[i + 6] & 0x7F;
            v.volume = sts[i + 7] & 0x7F;
            v.pan = sts[i + 8] & 0x7F;
            v.transpose = int(int8_t(sts[i + 10]));
            if (v.transpose < -24 || v.transpose > 24)
                v.transpose = 0;
        }
        result.push_back(ots);
    }

    // Pad assignments: "<sts> 02 01 00 00 00 DE 00 FE 04 02 00 02 01 00 02 00 00", then 4 records
    // of 10 bytes: pad number, FF, volume, pan, ... (styles from older firmware have none).
    for (size_t i = 6; i + 12 + 4 * 10 <= sts.size(); ++i)
    {
        if (sts[i] != 0xDE || sts[i + 1] != 0x00 || sts[i + 2] != 0xFE || sts[i + 3] != 0x04 || sts[i - 5] != 0x02)
            continue;
        const size_t index = sts[i - 6];
        if (index >= result.size())
            continue;
        for (size_t p = 0; p < size_t(kStylePadCount); ++p)
        {
            const size_t r = i + 12 + p * 10;
            if (sts[r + 1] != 0xFF)
                continue;
            PadRef& pad = result[index].pads[p];
            pad.id = sts[r];
            pad.volume = sts[r + 2] & 0x7F;
            pad.pan = sts[r + 3] & 0x7F;
        }
    }
    return result;
}

Style convert(const KStyle& k, const std::string& bankName)
{
    Style style;
    style.name = k.name;
    style.sourcePath = bankName;
    style.format = StyleFormat::KorgPa;

    style.ots = readSts(k.sts);

    // The StylePerformances tempo byte (offset found empirically on Pa50 banks).
    if (k.sts.size() > 0x19 && k.sts[0x19] >= 30 && k.sts[0x19] <= 250)
        style.tempo = k.sts[0x19];

    const KElement* first = nullptr;
    for (const auto& e : k.elements)
        if (e.hasData)
        {
            first = &e;
            break;
        }
    if (!first)
        throw StyleError("Korg style '" + k.name + "' has no data");

    int num = first->timeSig >> 3, den = 1 << (first->timeSig & 7);
    if (num >= 1 && num <= 32 && den >= 1 && den <= 32)
    {
        style.timeSigNum = num;
        style.timeSigDen = den;
    }

    for (int track = 0; track < 8; ++track)
    {
        const KTrackData& d = first->tracks[track];
        auto& setup = style.setup[int(kRoleOf[track])];
        // Pa styles address GM2 banks: MSB 120 = drum kits, MSB 121 = melodic (LSB = variation),
        // programs are GM numbers. XG/GS modules want MSB 127 for kits and MSB 0 otherwise; the
        // GM2 variation LSB means something else there, so the basic voice is selected.
        const bool drums = d.bankMsb == 120;
        setup.push_back({0, 0xB0, 0, uint8_t(drums ? 127 : 0)});
        setup.push_back({0, 0xB0, 32, 0});
        setup.push_back({0, 0xC0, uint8_t(d.program & 0x7F), 0});
        setup.push_back({0, 0xB0, 7, 100});
        setup.push_back({0, 0xB0, 11, d.expression > 0 && d.expression <= 127 ? d.expression : uint8_t(127)});
    }

    for (int e = 0; e < 15; ++e)
    {
        const KElement& element = k.elements[e];
        if (!element.hasData)
            continue;

        Section section;
        section.id = kElementSection[e];
        std::vector<int> varOfCv(element.cvs.size(), -1);

        auto groupOfCv = [&](int cv) {
            auto at = [&](int i) { return i < int(element.chordTable.size()) ? element.chordTable[size_t(i)] : 0; };
            if (at(kTableMajor) == cv) return ChordType::Maj7;
            if (at(kTableMinor) == cv) return ChordType::Min7;
            if (at(kTableSeventh) == cv) return ChordType::Dom7;
            return ChordType::Maj7;
        };

        for (int cv = 0; cv < int(element.cvs.size()); ++cv)
        {
            if (!element.cvs[size_t(cv)].hasData)
                continue;
            if (auto v = buildVariation(k, e, cv, groupOfCv(cv), section.length, style.warnings))
            {
                varOfCv[size_t(cv)] = int(section.variations.size());
                section.variations.push_back(std::move(*v));
            }
        }
        if (section.variations.empty() || section.length <= 0)
            continue;

        for (int t = 0; t < kChordTypeCount; ++t)
        {
            int group = tableGroupFor(ChordType(t));
            int cv = group < int(element.chordTable.size()) ? element.chordTable[size_t(group)] : 0;
            int var = cv < int(varOfCv.size()) ? varOfCv[size_t(cv)] : -1;
            section.variationForChord[size_t(t)] = uint8_t(var >= 0 ? var : 0);
        }
        style.sections[int(section.id)] = std::move(section);
    }
    return style;
}

std::string fixedName(std::span<const uint8_t> b)
{
    std::string s(b.begin(), b.end());
    if (auto z = s.find('\0'); z != std::string::npos)
        s.resize(z);
    while (!s.empty() && s.back() == ' ')
        s.pop_back();
    return s;
}

} // namespace

std::vector<Style> readKorgStyleBank(std::span<const uint8_t> file, const std::string& bankName)
{
    constexpr uint8_t kFlagOc31 = 0x20, kFlagEncrypted = 0x40;

    ByteReader r(file);
    auto container = readChunk(r);
    if (container.type != 1)
        throw StyleError("not a Korg bank file");

    ByteReader inner(container.body);
    auto korf = readChunk(inner);
    if (korf.type != 2 || korf.body.size() < 12 || korf.body[7] != 'K' || korf.body[8] != 'O'
        || korf.body[9] != 'R' || korf.body[10] != 'F')
        throw StyleError("Korg bank: KORF header missing");

    auto tocChunk = readChunk(inner);
    if (tocChunk.type != 5 || tocChunk.verMajor != 0)
        throw StyleError("Korg bank: unsupported table of contents");

    struct TocEntry { std::string name; uint8_t objectType; };
    std::vector<TocEntry> toc;
    ByteReader t(tocChunk.body);
    while (t.remaining() >= 24)
    {
        std::string name = fixedName(t.bytes(18));
        uint8_t type = t.u8();
        t.skip(5);
        toc.push_back({name, type});
    }

    std::vector<KStyle> raw;
    std::vector<std::string> skipped;
    for (size_t i = 0; i < toc.size() && !inner.atEnd(); ++i)
    {
        auto chunk = readChunk(inner);
        if (chunk.type == 0xFE)
            break;
        if (chunk.flags & kFlagEncrypted)
        {
            skipped.push_back(toc[i].name);
            continue;
        }

        std::vector<uint8_t> payload;
        std::span<const uint8_t> data = chunk.body;
        if (chunk.flags & kFlagOc31)
        {
            payload = detail::decompressOc31(chunk.body);
            data = payload;
        }

        if (chunk.type == 6 && toc[i].objectType == 2)
        {
            KStyle style;
            style.tocName = style.name = toc[i].name;
            try
            {
                readStyleObject(style, data);
            }
            catch (const StyleError& e)
            {
                throw StyleError("Korg style '" + toc[i].name + "': " + e.what());
            }
            raw.push_back(std::move(style));
        }
        // The StylePerformances object follows its style; its name may differ from the style's.
        else if (chunk.type == 9 && toc[i].objectType == 7 && !raw.empty() && raw.back().sts.empty())
            raw.back().sts.assign(data.begin(), data.end());
    }

    std::vector<Style> styles;
    for (const auto& k : raw)
    {
        styles.push_back(convert(k, bankName));
        for (const auto& name : skipped)
            styles.back().warnings.push_back("encrypted object '" + name + "' skipped");
    }
    return styles;
}

} // namespace arr
