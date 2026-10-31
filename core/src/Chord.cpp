#include "arranger/Chord.h"

#include <cctype>
#include <initializer_list>

namespace arr {

namespace {

constexpr uint16_t mask(std::initializer_list<int> intervals)
{
    uint16_t m = 0;
    for (int i : intervals)
        m |= uint16_t(1u << i);
    return m;
}

using S = std::array<int8_t, 7>;
using R = std::array<int8_t, kRoleCount>;
using F = ChordFamily;

constexpr S kIonian      {0, 2, 4, 5, 7, 9, 11};
constexpr S kLydian      {0, 2, 4, 6, 7, 9, 11};
constexpr S kDorian      {0, 2, 3, 5, 7, 9, 10};
constexpr S kLocrian     {0, 1, 3, 5, 6, 8, 10};
constexpr S kMelMinor    {0, 2, 3, 5, 7, 9, 11};
constexpr S kDiminished  {0, 2, 3, 5, 6, 8, 9};
constexpr S kMixolydian  {0, 2, 4, 5, 7, 9, 10};

//                                 root 3rd 5th 7th 9th 11th 13th
const ChordTypeInfo kTable[kChordTypeCount] = {
    {"",       mask({0, 4, 7}),         kIonian,                 R{0, 4, 7, -1, -1, -1, -1}, F::Major},
    {"6",      mask({0, 4, 7, 9}),      kIonian,                 R{0, 4, 7, -1, -1, -1, 9},  F::Major},
    {"maj7",   mask({0, 4, 7, 11}),     kIonian,                 R{0, 4, 7, 11, -1, -1, -1}, F::Major},
    {"maj7#11",mask({0, 4, 6, 7, 11}),  kLydian,                 R{0, 4, 7, 11, -1, 6, -1},  F::Major},
    {"add9",   mask({0, 2, 4, 7}),      kIonian,                 R{0, 4, 7, -1, 2, -1, -1},  F::Major},
    {"maj9",   mask({0, 2, 4, 7, 11}),  kIonian,                 R{0, 4, 7, 11, 2, -1, -1},  F::Major},
    {"69",     mask({0, 2, 4, 7, 9}),   kIonian,                 R{0, 4, 7, -1, 2, -1, 9},   F::Major},
    {"aug",    mask({0, 4, 8}),         S{0, 2, 4, 6, 8, 9, 11}, R{0, 4, 8, -1, -1, -1, -1}, F::Augmented},
    {"m",      mask({0, 3, 7}),         kDorian,                 R{0, 3, 7, -1, -1, -1, -1}, F::Minor},
    {"m6",     mask({0, 3, 7, 9}),      kDorian,                 R{0, 3, 7, -1, -1, -1, 9},  F::Minor},
    {"m7",     mask({0, 3, 7, 10}),     kDorian,                 R{0, 3, 7, 10, -1, -1, -1}, F::Minor},
    {"m7b5",   mask({0, 3, 6, 10}),     kLocrian,                R{0, 3, 6, 10, -1, -1, -1}, F::Diminished},
    {"madd9",  mask({0, 2, 3, 7}),      kDorian,                 R{0, 3, 7, -1, 2, -1, -1},  F::Minor},
    {"m9",     mask({0, 2, 3, 7, 10}),  kDorian,                 R{0, 3, 7, 10, 2, -1, -1},  F::Minor},
    {"m11",    mask({0, 3, 5, 7, 10}),  kDorian,                 R{0, 3, 7, 10, -1, 5, -1},  F::Minor},
    {"mmaj7",  mask({0, 3, 7, 11}),     kMelMinor,               R{0, 3, 7, 11, -1, -1, -1}, F::Minor},
    {"mmaj9",  mask({0, 2, 3, 7, 11}),  kMelMinor,               R{0, 3, 7, 11, 2, -1, -1},  F::Minor},
    {"dim",    mask({0, 3, 6}),         kDiminished,             R{0, 3, 6, -1, -1, -1, -1}, F::Diminished},
    {"dim7",   mask({0, 3, 6, 9}),      kDiminished,             R{0, 3, 6, 9, -1, -1, -1},  F::Diminished},
    {"7",      mask({0, 4, 7, 10}),     kMixolydian,             R{0, 4, 7, 10, -1, -1, -1}, F::Dominant},
    {"7sus4",  mask({0, 5, 7, 10}),     S{0, 2, 5, 5, 7, 9, 10}, R{0, 5, 7, 10, -1, -1, -1}, F::Suspended},
    {"7b5",    mask({0, 4, 6, 10}),     S{0, 2, 4, 5, 6, 9, 10}, R{0, 4, 6, 10, -1, -1, -1}, F::Dominant},
    {"9",      mask({0, 2, 4, 7, 10}),  kMixolydian,             R{0, 4, 7, 10, 2, -1, -1},  F::Dominant},
    {"7#11",   mask({0, 4, 6, 7, 10}),  S{0, 2, 4, 6, 7, 9, 10}, R{0, 4, 7, 10, -1, 6, -1},  F::Dominant},
    {"13",     mask({0, 4, 7, 9, 10}),  kMixolydian,             R{0, 4, 7, 10, -1, -1, 9},  F::Dominant},
    {"7b9",    mask({0, 1, 4, 7, 10}),  S{0, 1, 4, 5, 7, 9, 10}, R{0, 4, 7, 10, 1, -1, -1},  F::Dominant},
    {"7b13",   mask({0, 4, 7, 8, 10}),  S{0, 2, 4, 5, 7, 8, 10}, R{0, 4, 7, 10, -1, -1, 8},  F::Dominant},
    {"7#9",    mask({0, 3, 4, 7, 10}),  S{0, 3, 4, 5, 7, 9, 10}, R{0, 4, 7, 10, 3, -1, -1},  F::Dominant},
    {"maj7#5", mask({0, 4, 8, 11}),     S{0, 2, 4, 6, 8, 9, 11}, R{0, 4, 8, 11, -1, -1, -1}, F::Augmented},
    {"7#5",    mask({0, 4, 8, 10}),     S{0, 2, 4, 6, 8, 9, 10}, R{0, 4, 8, 10, -1, -1, -1}, F::Augmented},
    {"(1+8)",  mask({0}),               kIonian,                 R{0, -1, -1, -1, -1, -1, -1}, F::Major},
    {"5",      mask({0, 7}),            kIonian,                 R{0, -1, 7, -1, -1, -1, -1}, F::Major},
    {"sus4",   mask({0, 5, 7}),         S{0, 2, 5, 5, 7, 9, 11}, R{0, 5, 7, -1, -1, -1, -1}, F::Suspended},
    {"sus2",   mask({0, 2, 7}),         kIonian,                 R{0, 2, 7, -1, -1, -1, -1}, F::Suspended},
};

constexpr std::string_view kSharpNames[12] = {"C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B"};

struct Alias { std::string_view text; ChordType type; };

// Extra spellings accepted by parseChord besides the canonical symbols.
constexpr Alias kAliases[] = {
    {"M", ChordType::Maj}, {"maj", ChordType::Maj}, {"M7", ChordType::Maj7}, {"Maj7", ChordType::Maj7},
    {"M9", ChordType::Maj9}, {"6/9", ChordType::Maj6_9}, {"+", ChordType::Aug}, {"min", ChordType::Min},
    {"-", ChordType::Min}, {"-7", ChordType::Min7}, {"min7", ChordType::Min7}, {"mM7", ChordType::MinMaj7},
    {"m(maj7)", ChordType::MinMaj7}, {"o", ChordType::Dim}, {"o7", ChordType::Dim7}, {"0", ChordType::Min7b5},
    {"7(9)", ChordType::Dom9}, {"7(13)", ChordType::Dom13}, {"7(b9)", ChordType::Dom7b9},
    {"7(#9)", ChordType::Dom7Sharp9}, {"7(b13)", ChordType::Dom7b13}, {"7(#11)", ChordType::Dom7Sharp11},
    {"7+", ChordType::Dom7Aug}, {"7aug", ChordType::Dom7Aug}, {"maj7aug", ChordType::Maj7Aug},
    {"sus", ChordType::Sus4}, {"m(9)", ChordType::MinAdd9}, {"(9)", ChordType::MajAdd9}, {"1+8", ChordType::OnePlus8},
    {"1+5", ChordType::OnePlus5}, {"1+2+5", ChordType::Sus2},
};

std::optional<int> parseRoot(std::string_view& s)
{
    if (s.empty())
        return std::nullopt;

    static constexpr int kLetters[7] = {9, 11, 0, 2, 4, 5, 7}; // A..G
    char letter = char(std::toupper(static_cast<unsigned char>(s[0])));
    if (letter < 'A' || letter > 'G')
        return std::nullopt;

    int pc = kLetters[letter - 'A'];
    s.remove_prefix(1);
    while (!s.empty() && (s[0] == '#' || s[0] == 'b'))
    {
        // "b" right after the letter is a flat; "Bb", "Ebm" etc. work because the letter is consumed first.
        pc += s[0] == '#' ? 1 : -1;
        s.remove_prefix(1);
    }
    return (pc % 12 + 12) % 12;
}

} // namespace

const ChordTypeInfo& chordTypeInfo(ChordType type)
{
    return kTable[static_cast<int>(type)];
}

std::string_view pitchClassName(int pc)
{
    return kSharpNames[(pc % 12 + 12) % 12];
}

bool Chord::isMinor() const
{
    auto f = chordTypeInfo(type).family;
    return f == ChordFamily::Minor || f == ChordFamily::Diminished;
}

std::string Chord::name() const
{
    std::string s(pitchClassName(root));
    s += chordTypeInfo(type).symbol;
    if (bass >= 0 && bass != root)
    {
        s += '/';
        s += pitchClassName(bass);
    }
    return s;
}

std::optional<Chord> parseChord(std::string_view symbol)
{
    std::string_view rest = symbol;
    auto root = parseRoot(rest);
    if (!root)
        return std::nullopt;

    Chord chord;
    chord.root = *root;

    std::string_view suffix = rest;
    if (auto slash = rest.rfind('/'); slash != std::string_view::npos && rest.substr(slash) != "/9")
    {
        std::string_view bassText = rest.substr(slash + 1);
        auto bass = parseRoot(bassText);
        if (!bass || !bassText.empty())
            return std::nullopt;
        chord.bass = *bass;
        suffix = rest.substr(0, slash);
    }

    bool found = false;
    for (int i = 0; i < kChordTypeCount; ++i)
        if (kTable[i].symbol == suffix)
        {
            chord.type = static_cast<ChordType>(i);
            found = true;
            break;
        }
    if (!found)
        for (const auto& alias : kAliases)
            if (alias.text == suffix)
            {
                chord.type = alias.type;
                found = true;
                break;
            }

    if (!found)
        return std::nullopt;
    if (chord.bass == chord.root)
        chord.bass = -1;
    return chord;
}

} // namespace arr
