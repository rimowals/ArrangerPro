#include "arranger/Transposer.h"

namespace arr {

namespace {

int mod12(int v) { return (v % 12 + 12) % 12; }

// Signed distance from pitch class `from` to `to`, in [-6, 5].
int nearestDelta(int from, int to) { return mod12(to - from + 6) - 6; }

// Melody table: keep the scale degree, carry chromatic alterations along.
int mapByScale(int interval, const std::array<int8_t, 7>& from, const std::array<int8_t, 7>& to)
{
    int degree = 0;
    for (int d = 0; d < 7; ++d)
        if (from[d] <= interval)
            degree = d;
    return mod12(to[degree] + (interval - from[degree]));
}

// Chord table: a chord tone keeps its function; a missing function falls back to a related tone.
int mapByRole(int interval, const ChordTypeInfo& from, const ChordTypeInfo& to)
{
    int role = -1;
    for (int r = 0; r < kRoleCount; ++r)
        if (from.roles[r] == interval)
        {
            role = r;
            break;
        }
    if (role < 0)
        return mapByScale(interval, from.scale, to.scale); // passing tone

    static constexpr int kFallback[kRoleCount][3] = {
        {RoleRoot, RoleRoot, RoleRoot},
        {RoleThird, RoleFifth, RoleRoot},          // 3rd → 5th (1+5) → root (1+8)
        {RoleFifth, RoleRoot, RoleRoot},
        {RoleSeventh, RoleThirteenth, RoleRoot},   // 7th → 6th → octave
        {RoleNinth, RoleRoot, RoleRoot},
        {RoleEleventh, RoleFifth, RoleRoot},
        {RoleThirteenth, RoleSeventh, RoleFifth},  // 6th → 7th → 5th
    };
    for (int candidate : kFallback[role])
        if (to.roles[candidate] >= 0)
            return to.roles[candidate];
    return 0;
}

// The major/minor-only tables: transpose by root and only fix the degrees that differ
// between the major and minor flavour of the scale.
int mapMajorMinor(int interval, Ntt ntt, bool sourceMinor, const ChordTypeInfo& target)
{
    const bool targetMinor = target.family == ChordFamily::Minor || target.family == ChordFamily::Diminished;

    if (sourceMinor != targetMinor)
    {
        // pairs of (major-flavour interval, minor-flavour interval) that this table converts
        int pairs[3][2] = {{4, 3}, {-1, -1}, {-1, -1}};
        switch (ntt)
        {
            case Ntt::HarmonicMinor: case Ntt::HarmonicMinor5th:
                pairs[1][0] = 9; pairs[1][1] = 8; break;
            case Ntt::NaturalMinor: case Ntt::NaturalMinor5th:
                pairs[1][0] = 9; pairs[1][1] = 8; pairs[2][0] = 11; pairs[2][1] = 10; break;
            case Ntt::Dorian: case Ntt::Dorian5th:
                pairs[1][0] = 11; pairs[1][1] = 10; break;
            default: break;
        }
        for (const auto& p : pairs)
        {
            if (p[0] < 0)
                continue;
            if (!sourceMinor && interval == p[0])
                interval = p[1];
            else if (sourceMinor && interval == p[1])
                interval = p[0];
        }
    }

    const bool fifthVariant = ntt == Ntt::MelodicMinor5th || ntt == Ntt::HarmonicMinor5th
                           || ntt == Ntt::NaturalMinor5th || ntt == Ntt::Dorian5th;
    if (fifthVariant && interval == 7)
    {
        if (target.family == ChordFamily::Diminished)
            interval = 6;
        else if (target.family == ChordFamily::Augmented)
            interval = 8;
    }
    return interval;
}

int foldIntoLimits(int key, int low, int high)
{
    if (key < low)
        key += 12 * ((low - key + 11) / 12);
    if (key > high)
        key -= 12 * ((key - high + 11) / 12);
    if (key < low || key < 0 || key > 127)
        return -1;
    return key;
}

} // namespace

int transposeNote(int key, const TransposeRule& rule, const Chord& chord)
{
    const TransposeRange& range = rule.rangeFor(chord.rootNote);

    if (range.ntr == Ntr::RootFixed && range.ntt == Ntt::Bypass)
        return foldIntoLimits(key, range.noteLow, range.noteHigh);

    const ChordTypeInfo& source = chordTypeInfo(rule.sourceType);
    const ChordTypeInfo& target = chordTypeInfo(chord.type);
    const int interval = mod12(key - rule.sourceRoot);

    int targetInterval = interval;
    switch (range.ntt)
    {
        case Ntt::Bypass:
            break;
        case Ntt::Melody:
            targetInterval = mapByScale(interval, source.scale, target.scale);
            break;
        case Ntt::Chord:
        case Ntt::GuitarAllPurpose:
        case Ntt::GuitarStroke:
        case Ntt::GuitarArpeggio:
            targetInterval = mapByRole(interval, source, target);
            break;
        default:
        {
            const bool sourceMinor = source.family == ChordFamily::Minor || source.family == ChordFamily::Diminished;
            targetInterval = mapMajorMinor(interval, range.ntt, sourceMinor, target);
            break;
        }
    }

    int targetRoot = chord.root;
    int targetPc = mod12(targetRoot + targetInterval);
    if (range.bassOn && chord.bass >= 0 && interval == 0)
        targetPc = chord.bass;

    int result;
    if (range.ntr == Ntr::RootTrans)
    {
        // Shift up to the chord root, unless the root lies above the high key: then shift down.
        int shift = mod12(targetRoot - rule.sourceRoot);
        if (shift > mod12(range.highKey - rule.sourceRoot))
            shift -= 12;
        int moved = key + shift;
        result = moved + nearestDelta(mod12(moved), targetPc);
    }
    else
    {
        result = key + nearestDelta(mod12(key), targetPc);
    }

    return foldIntoLimits(result, range.noteLow, range.noteHigh);
}

int rootKeyNear(int key, const Chord& chord)
{
    return key + nearestDelta(mod12(key), chord.bassPitchClass());
}

} // namespace arr
