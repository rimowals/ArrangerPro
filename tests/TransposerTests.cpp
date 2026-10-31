#include "Test.h"

#include "arranger/Transposer.h"

using namespace arr;

namespace {

TransposeRule rule(Ntr ntr, Ntt ntt, bool bassOn = false)
{
    TransposeRule r;
    r.sourceRoot = 0;
    r.sourceType = ChordType::Maj7;
    r.mid.ntr = ntr;
    r.mid.ntt = ntt;
    r.mid.bassOn = bassOn;
    r.mid.highKey = 5; // F
    return r;
}

int t(int key, const TransposeRule& r, const char* chord) { return transposeNote(key, r, *parseChord(chord)); }

} // namespace

TEST_CASE("chord table, root transposition with high key")
{
    auto r = rule(Ntr::RootTrans, Ntt::Chord);
    // up to F (F is the high key)
    CHECK_EQ(t(60, r, "F"), 65);
    CHECK_EQ(t(64, r, "F"), 69);
    CHECK_EQ(t(67, r, "F"), 72);
    CHECK_EQ(t(71, r, "F"), 77); // the 7th becomes the octave of a triad
    // G lies above the high key: down a fourth
    CHECK_EQ(t(60, r, "G"), 55);
    CHECK_EQ(t(60, r, "Am"), 57);
    CHECK_EQ(t(64, r, "Am"), 60);
    CHECK_EQ(t(71, r, "C7"), 70);
    CHECK_EQ(t(71, r, "Cmaj7"), 71);
}

TEST_CASE("melody table follows the chord scale")
{
    auto r = rule(Ntr::RootTrans, Ntt::Melody);
    CHECK_EQ(t(64, r, "Cm"), 63);
    CHECK_EQ(t(62, r, "Cm"), 62);
    CHECK_EQ(t(69, r, "Cm"), 69); // dorian sixth
    CHECK_EQ(t(71, r, "Cm"), 70);
    CHECK_EQ(t(61, r, "C"), 61);  // chromatic passing tone is kept
}

TEST_CASE("root fixed keeps notes close to the recorded pitch")
{
    auto r = rule(Ntr::RootFixed, Ntt::Chord);
    // Functions are kept (root → root, 3rd → 3rd...), each at the nearest octave.
    CHECK_EQ(t(60, r, "F"), 65);
    CHECK_EQ(t(64, r, "F"), 69);
    CHECK_EQ(t(67, r, "F"), 72);
    CHECK_EQ(t(67, r, "Am"), 64);
    CHECK_EQ(t(60, rule(Ntr::RootFixed, Ntt::Bypass), "F#"), 60);
}

TEST_CASE("bass on follows the slash bass")
{
    auto r = rule(Ntr::RootTrans, Ntt::Melody, true);
    CHECK_EQ(t(36, r, "C/E"), 40);
    CHECK_EQ(t(43, r, "C/E"), 43);
    CHECK_EQ(t(36, rule(Ntr::RootTrans, Ntt::Melody), "C/E"), 36);
}

TEST_CASE("major/minor tables")
{
    auto mel = rule(Ntr::RootTrans, Ntt::MelodicMinor);
    CHECK_EQ(t(64, mel, "Cm"), 63);
    CHECK_EQ(t(69, mel, "Cm"), 69);
    auto harm = rule(Ntr::RootTrans, Ntt::HarmonicMinor);
    CHECK_EQ(t(69, harm, "Cm"), 68);
    CHECK_EQ(t(64, harm, "D"), 66);
}

TEST_CASE("note limits fold by octave")
{
    auto r = rule(Ntr::RootTrans, Ntt::Chord);
    r.mid.noteLow = 60;
    r.mid.noteHigh = 72;
    CHECK_EQ(t(60, r, "G"), 67);
    CHECK_EQ(t(71, r, "F"), 65);
}

TEST_CASE("note and chord mute")
{
    TransposeRule r;
    r.noteMute = 0x0FFF & ~(1u << 2); // no D
    r.chordMute &= ~(1ull << int(ChordType::Min));
    CHECK(r.playsFor(*parseChord("C")));
    CHECK(!r.playsFor(*parseChord("D")));
    CHECK(!r.playsFor(*parseChord("Em")));
}
