#pragma once

#include "arranger/Chord.h"

#include <array>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace arr {

// Internal time resolution; every reader rescales its source data to this.
inline constexpr int kPpq = 1920;

class StyleError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// Accompaniment parts, in Yamaha channel order (MIDI channels 9..16).
enum class PartRole : uint8_t { SubRhythm, Rhythm, Bass, Chord1, Chord2, Pad, Phrase1, Phrase2 };
inline constexpr int kPartCount = 8;
std::string_view partRoleName(PartRole role);

enum class SectionId : uint8_t {
    IntroA, IntroB, IntroC, IntroD,
    MainA, MainB, MainC, MainD,
    FillAA, FillBB, FillCC, FillDD, Break,
    EndingA, EndingB, EndingC, EndingD,
};
inline constexpr int kSectionCount = 17;

std::string_view sectionName(SectionId id);
std::optional<SectionId> sectionFromYamahaMarker(std::string_view marker);

inline bool isIntro(SectionId s) { return s <= SectionId::IntroD; }
inline bool isMain(SectionId s) { return s >= SectionId::MainA && s <= SectionId::MainD; }
inline bool isFill(SectionId s) { return s >= SectionId::FillAA && s <= SectionId::Break; }
inline bool isEnding(SectionId s) { return s >= SectionId::EndingA; }
// Main A..D ↔ index 0..3, and the fill that leads into a given main.
inline int mainIndex(SectionId s) { return int(s) - int(SectionId::MainA); }
inline SectionId mainSection(int index) { return SectionId(int(SectionId::MainA) + index); }
inline SectionId fillInto(SectionId main) { return SectionId(int(SectionId::FillAA) + mainIndex(main)); }

// ---- note transposition rules (Yamaha CASM Ctab/Ctb2 semantics) ----

enum class Ntr : uint8_t { RootTrans, RootFixed, Guitar };

enum class Ntt : uint8_t {
    Bypass, Melody, Chord,
    MelodicMinor, MelodicMinor5th, HarmonicMinor, HarmonicMinor5th,
    NaturalMinor, NaturalMinor5th, Dorian, Dorian5th,
    GuitarAllPurpose, GuitarStroke, GuitarArpeggio,
};

// Retrigger rule: what happens to a sounding note when the chord changes.
enum class Rtr : uint8_t { Stop, PitchShift, PitchShiftToRoot, Retrigger, RetriggerToRoot, NoteGenerator };

struct TransposeRange {
    Ntr ntr = Ntr::RootTrans;
    Ntt ntt = Ntt::Melody;
    bool bassOn = false;   // notes on the source root follow the on-bass note of slash chords
    uint8_t highKey = 5;   // highest chord root (pitch class) the part is transposed *up* to
    uint8_t noteLow = 0, noteHigh = 127;
    Rtr rtr = Rtr::PitchShift;
};

struct TransposeRule {
    int sourceRoot = 0;
    ChordType sourceType = ChordType::Maj7;
    uint16_t noteMute = 0x0FFF;            // bit per chord-root pitch class: set = part plays
    uint64_t chordMute = (1ull << kChordTypeCount) - 1; // bit per ChordType: set = part plays

    // SFF2 selects one of three ranges by the MIDI note of the played chord root;
    // all other formats only use `mid`.
    bool hasRanges = false;
    uint8_t midLow = 0, midHigh = 127;
    TransposeRange low, mid, high;

    const TransposeRange& rangeFor(int rootNote) const
    {
        if (!hasRanges || rootNote < 0)
            return mid;
        if (rootNote < midLow)
            return low;
        if (rootNote > midHigh)
            return high;
        return mid;
    }

    bool playsFor(const Chord& chord) const
    {
        return (noteMute & (1u << chord.root)) && (chordMute & (1ull << int(chord.type)));
    }
};

// ---- pattern data ----

struct Note {
    int64_t tick = 0;   // from the start of the section
    int32_t length = 0;
    uint8_t key = 0;
    uint8_t velocity = 0;
};

// A non-note channel message; the channel nibble of `status` is ignored (the engine routes by part).
struct ChannelEvent {
    int64_t tick = 0;
    uint8_t status = 0, data1 = 0, data2 = 0;

    uint8_t type() const { return status & 0xF0; }
};

struct Part {
    PartRole role = PartRole::Chord1;
    int sourceChannel = -1;        // channel in the source file (diagnostics only)
    bool isDrum = false;
    TransposeRule rule;
    std::vector<Note> notes;            // sorted by tick
    std::vector<ChannelEvent> controls; // sorted by tick
};

// One set of parts. Yamaha sections have exactly one; Korg sections one per chord variation.
struct ChordVariation {
    std::vector<Part> parts;
};

struct Section {
    SectionId id = SectionId::MainA;
    int64_t length = 0; // ticks
    std::vector<ChordVariation> variations;
    std::array<uint8_t, kChordTypeCount> variationForChord{}; // index into `variations`

    const ChordVariation& variationFor(ChordType type) const
    {
        uint8_t i = variationForChord[int(type)];
        return variations[i < variations.size() ? i : 0];
    }
};

enum class StyleFormat : uint8_t { YamahaSff1, YamahaSff2, KorgPa };
std::string_view styleFormatName(StyleFormat f);

struct Style {
    std::string name;
    std::string sourcePath;
    StyleFormat format = StyleFormat::YamahaSff1;
    double tempo = 120.0;
    int timeSigNum = 4, timeSigDen = 4;

    // Initial channel messages (bank/program/volume/pan/effects) per part, from the style's setup area.
    std::array<std::vector<ChannelEvent>, kPartCount> setup;
    std::array<std::optional<Section>, kSectionCount> sections;

    std::vector<std::string> warnings;

    int64_t barLength() const { return int64_t(kPpq) * 4 * timeSigNum / timeSigDen; }
    int64_t beatLength() const { return int64_t(kPpq) * 4 / timeSigDen; }

    const Section* section(SectionId id) const
    {
        const auto& s = sections[int(id)];
        return s ? &*s : nullptr;
    }
    bool has(SectionId id) const { return section(id) != nullptr; }
};

} // namespace arr
