#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace arr {

// Chord types in Yamaha numbering — the same indices are stored as the "source chord type"
// in CASM Ctab/Ctb2 and used by the chord-mute bit mask.
enum class ChordType : uint8_t {
    Maj, Maj6, Maj7, Maj7Sharp11, MajAdd9, Maj9, Maj6_9, Aug,
    Min, Min6, Min7, Min7b5, MinAdd9, Min9, Min11, MinMaj7, MinMaj9,
    Dim, Dim7,
    Dom7, Dom7Sus4, Dom7b5, Dom9, Dom7Sharp11, Dom13, Dom7b9, Dom7b13, Dom7Sharp9,
    Maj7Aug, Dom7Aug, OnePlus8, OnePlus5, Sus4, Sus2,
};
inline constexpr int kChordTypeCount = 34;

enum class ChordFamily : uint8_t { Major, Minor, Dominant, Diminished, Augmented, Suspended };

// Functional roles of chord tones, used by the "Chord" note transposition table.
enum ChordRole : int { RoleRoot, RoleThird, RoleFifth, RoleSeventh, RoleNinth, RoleEleventh, RoleThirteenth };
inline constexpr int kRoleCount = 7;

struct ChordTypeInfo {
    std::string_view symbol;            // suffix after the root, e.g. "m7b5"
    uint16_t mask;                      // pitch classes relative to the root (bit n = n semitones)
    std::array<int8_t, 7> scale;        // seven-degree scale used by the "Melody" table
    std::array<int8_t, kRoleCount> roles; // interval for each ChordRole, -1 = not in the chord
    ChordFamily family;
};

const ChordTypeInfo& chordTypeInfo(ChordType type);

struct Chord {
    int root = 0;                      // pitch class 0..11 (C = 0)
    ChordType type = ChordType::Maj;
    int bass = -1;                     // pitch class of an on-bass note, -1 = root is the bass
    int rootNote = -1;                 // MIDI note the root was played on, -1 = unknown

    int bassPitchClass() const { return bass >= 0 ? bass : root; }
    bool isMinor() const;
    std::string name() const;

    bool operator==(const Chord& o) const { return root == o.root && type == o.type && bass == o.bass; }
};

std::string_view pitchClassName(int pc);

// Parses symbols such as "C", "F#m7", "Bbmaj7", "G7/B", "Dsus4". Returns nullopt on failure.
std::optional<Chord> parseChord(std::string_view symbol);

} // namespace arr
