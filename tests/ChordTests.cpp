#include "Test.h"

#include "arranger/ChordRecognizer.h"

#include <vector>

using namespace arr;

namespace {

std::string recognized(ChordMode mode, std::vector<int> notes)
{
    auto chord = ChordRecognizer(mode).recognize(notes);
    return chord ? chord->name() : "-";
}

} // namespace

TEST_CASE("parse chord symbols")
{
    auto c = parseChord("F#m7");
    CHECK(c.has_value());
    CHECK_EQ(c->root, 6);
    CHECK(c->type == ChordType::Min7);

    auto slash = parseChord("G7/B");
    CHECK(slash.has_value());
    CHECK_EQ(slash->root, 7);
    CHECK_EQ(slash->bass, 11);

    CHECK(parseChord("Bbmaj7")->root == 10);
    CHECK(parseChord("C6/9")->type == ChordType::Maj6_9);
    CHECK(parseChord("Dsus4")->type == ChordType::Sus4);
    CHECK(!parseChord("H7").has_value());
    CHECK(!parseChord("Cxyz").has_value());
    CHECK_EQ(parseChord("Ebm7b5")->name(), std::string("Ebm7b5"));
}

TEST_CASE("fingered recognition")
{
    CHECK_EQ(recognized(ChordMode::Fingered, {48, 52, 55}), std::string("C"));
    CHECK_EQ(recognized(ChordMode::Fingered, {57, 60, 64}), std::string("Am"));
    CHECK_EQ(recognized(ChordMode::Fingered, {52, 55, 60}), std::string("C"));
    CHECK_EQ(recognized(ChordMode::Fingered, {48, 52, 58}), std::string("C7"));   // fifth omitted
    CHECK_EQ(recognized(ChordMode::Fingered, {43, 47, 50, 53}), std::string("G7"));
    CHECK_EQ(recognized(ChordMode::Fingered, {45, 48, 52, 55}), std::string("Am7"));
    CHECK_EQ(recognized(ChordMode::Fingered, {48, 52, 55, 57}), std::string("C6"));
    CHECK_EQ(recognized(ChordMode::Fingered, {48, 51, 54, 57}), std::string("Cdim7"));
    CHECK_EQ(recognized(ChordMode::Fingered, {50, 53, 57, 60, 64}), std::string("Dm9"));
    CHECK_EQ(recognized(ChordMode::Fingered, {48, 60}), std::string("C(1+8)"));
    CHECK_EQ(recognized(ChordMode::Fingered, {48, 55}), std::string("C5"));
    CHECK_EQ(recognized(ChordMode::Fingered, {48, 52}), std::string("C"));
    CHECK_EQ(recognized(ChordMode::Fingered, {48, 49, 50}), std::string("-"));
}

TEST_CASE("on-bass recognition")
{
    CHECK_EQ(recognized(ChordMode::FingeredOnBass, {52, 55, 60}), std::string("C/E"));
    CHECK_EQ(recognized(ChordMode::FingeredOnBass, {47, 50, 55}), std::string("G/B"));
    CHECK_EQ(recognized(ChordMode::FingeredOnBass, {48, 52, 55}), std::string("C"));
}

TEST_CASE("single finger recognition")
{
    CHECK_EQ(recognized(ChordMode::SingleFinger, {48}), std::string("C"));
    CHECK_EQ(recognized(ChordMode::SingleFinger, {46, 48}), std::string("Cm"));
    CHECK_EQ(recognized(ChordMode::SingleFinger, {45, 48}), std::string("C7"));
    CHECK_EQ(recognized(ChordMode::SingleFinger, {45, 46, 48}), std::string("Cm7"));
}
