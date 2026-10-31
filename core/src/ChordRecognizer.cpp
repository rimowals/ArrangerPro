#include "arranger/ChordRecognizer.h"

#include <algorithm>
#include <bit>

namespace arr {

namespace {

uint16_t rotateToRoot(uint16_t pcs, int root)
{
    uint32_t m = pcs;
    m = (m >> root) | (m << (12 - root));
    return uint16_t(m & 0x0FFF);
}

bool isBlackKey(int note)
{
    switch (note % 12)
    {
        case 1: case 3: case 6: case 8: case 10: return true;
        default: return false;
    }
}

constexpr uint16_t kFifth = 1u << 7;

// Order in which templates are tried; the first match wins among equally good candidates.
// Simple, common chords come before their rarer enharmonic twins (C6 before Am7/C etc.).
constexpr ChordType kSearchOrder[] = {
    ChordType::Maj, ChordType::Min, ChordType::Dom7, ChordType::Maj7, ChordType::Min7,
    ChordType::Maj6, ChordType::Min6, ChordType::Dim, ChordType::Dim7, ChordType::Aug,
    ChordType::Min7b5, ChordType::Sus4, ChordType::Dom7Sus4, ChordType::Sus2,
    ChordType::MajAdd9, ChordType::MinAdd9, ChordType::Maj9, ChordType::Min9, ChordType::Dom9,
    ChordType::Maj6_9, ChordType::MinMaj7, ChordType::MinMaj9, ChordType::Min11,
    ChordType::Maj7Sharp11, ChordType::Dom7Sharp11, ChordType::Dom13, ChordType::Dom7b9,
    ChordType::Dom7b13, ChordType::Dom7Sharp9, ChordType::Dom7b5, ChordType::Maj7Aug, ChordType::Dom7Aug,
    ChordType::OnePlus5,
};

struct Match { int root; ChordType type; int score; };

} // namespace

std::optional<Chord> ChordRecognizer::recognize(std::span<const int> notes) const
{
    if (notes.empty())
        return std::nullopt;
    return mode_ == ChordMode::SingleFinger ? recognizeSingleFinger(notes) : recognizeFingered(notes);
}

std::optional<Chord> ChordRecognizer::recognizeFingered(std::span<const int> notes) const
{
    uint16_t pcs = 0;
    int lowest = 128;
    for (int n : notes)
    {
        pcs |= uint16_t(1u << (n % 12));
        lowest = std::min(lowest, n);
    }
    const int lowestPc = lowest % 12;
    const int pcCount = std::popcount(pcs);

    auto noteOfPc = [&](int pc) {
        int best = -1;
        for (int n : notes)
            if (n % 12 == pc && (best < 0 || n < best))
                best = n;
        return best;
    };

    auto finish = [&](int root, ChordType type) {
        Chord c;
        c.root = root;
        c.type = type;
        c.rootNote = noteOfPc(root);
        if (mode_ == ChordMode::FingeredOnBass && lowestPc != root)
            c.bass = lowestPc;
        return c;
    };

    if (pcCount == 1)
        return finish(lowestPc, notes.size() > 1 ? ChordType::OnePlus8 : ChordType::Maj);

    std::optional<Match> best;
    auto consider = [&](int root, ChordType type, int score) {
        if (root == lowestPc)
            score += 10; // the bass note is by far the strongest root hint
        if (!best || score > best->score)
            best = Match{root, type, score};
    };

    for (int root = 0; root < 12; ++root)
    {
        if (!(pcs & (1u << root)))
            continue;
        const uint16_t rel = rotateToRoot(pcs, root);

        int order = 0;
        for (ChordType type : kSearchOrder)
        {
            const uint16_t tmpl = chordTypeInfo(type).mask;
            const int tieBreak = -order++; // earlier templates win ties
            if (rel == tmpl)
                consider(root, type, 100 + tieBreak);
            else if (std::popcount(tmpl) >= 4 && (tmpl & kFifth) && rel == uint16_t(tmpl & ~kFifth))
                consider(root, type, 60 + tieBreak); // omitted fifth, common in left-hand voicings
        }
    }

    // In on-bass mode a non-chord lowest note may be a slash bass: try the chord without it.
    if (!best && mode_ == ChordMode::FingeredOnBass && pcCount >= 4)
    {
        uint16_t upper = uint16_t(pcs & ~(1u << lowestPc));
        for (int root = 0; root < 12 && !best; ++root)
        {
            if (!(upper & (1u << root)))
                continue;
            const uint16_t rel = rotateToRoot(upper, root);
            for (ChordType type : kSearchOrder)
                if (rel == chordTypeInfo(type).mask)
                {
                    best = Match{root, type, 0};
                    break;
                }
        }
    }

    // Two notes: a third (or sixth, as an inverted third) implies a major/minor triad.
    if (!best && pcCount == 2)
    {
        int other = -1;
        for (int pc = 0; pc < 12; ++pc)
            if ((pcs & (1u << pc)) && pc != lowestPc)
                other = pc;
        int interval = (other - lowestPc + 12) % 12;
        switch (interval)
        {
            case 4: best = Match{lowestPc, ChordType::Maj, 0}; break;
            case 3: best = Match{lowestPc, ChordType::Min, 0}; break;
            case 8: best = Match{other, ChordType::Maj, 0}; break;  // E+C → C
            case 9: best = Match{other, ChordType::Min, 0}; break;  // C+A → Am
            case 5: best = Match{other, ChordType::OnePlus5, 0}; break; // G+C → C5
            default: break;
        }
    }

    if (!best)
        return std::nullopt;
    return finish(best->root, best->type);
}

std::optional<Chord> ChordRecognizer::recognizeSingleFinger(std::span<const int> notes) const
{
    const int top = *std::max_element(notes.begin(), notes.end());
    bool black = false, white = false;
    for (int n : notes)
        if (n < top)
            (isBlackKey(n) ? black : white) = true;

    Chord c;
    c.root = top % 12;
    c.rootNote = top;
    if (black && white)
        c.type = ChordType::Min7;
    else if (black)
        c.type = ChordType::Min;
    else if (white)
        c.type = ChordType::Dom7;
    else
        c.type = ChordType::Maj;
    return c;
}

} // namespace arr
