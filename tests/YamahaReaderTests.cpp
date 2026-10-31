#include "Test.h"

#include "arranger/StyleLoader.h"

#include <string>
#include <vector>

using namespace arr;

namespace {

using Bytes = std::vector<uint8_t>;

void u32(Bytes& b, uint32_t v)
{
    for (int s = 24; s >= 0; s -= 8)
        b.push_back(uint8_t(v >> s));
}

void chunk(Bytes& out, const char* id, const Bytes& body)
{
    out.insert(out.end(), id, id + 4);
    u32(out, uint32_t(body.size()));
    out.insert(out.end(), body.begin(), body.end());
}

void varLen(Bytes& b, uint32_t v)
{
    Bytes tmp{uint8_t(v & 0x7F)};
    while (v >>= 7)
        tmp.insert(tmp.begin(), uint8_t(0x80 | (v & 0x7F)));
    b.insert(b.end(), tmp.begin(), tmp.end());
}

void marker(Bytes& t, uint32_t delta, const std::string& text)
{
    varLen(t, delta);
    t.insert(t.end(), {0xFF, 0x06, uint8_t(text.size())});
    t.insert(t.end(), text.begin(), text.end());
}

void ev(Bytes& t, uint32_t delta, std::initializer_list<uint8_t> bytes)
{
    varLen(t, delta);
    t.insert(t.end(), bytes);
}

Bytes ctb2(uint8_t channel, uint8_t ntr, uint8_t ntt)
{
    Bytes d(47, 0);
    d[0] = channel;
    const char* name = "Part    ";
    std::copy(name, name + 8, d.begin() + 1);
    d[9] = channel;
    d[11] = 0x0F; d[12] = 0xFF;                           // all roots
    d[13] = 0x03; d[14] = d[15] = d[16] = d[17] = 0xFF;   // all chord types
    d[18] = 0; d[19] = uint8_t(ChordType::Maj7);
    d[20] = 0; d[21] = 127;
    for (int r = 0; r < 3; ++r)
    {
        uint8_t* b = &d[size_t(22 + 6 * r)];
        b[0] = ntr; b[1] = ntt; b[2] = 6; b[3] = 0; b[4] = 127; b[5] = 1;
    }
    return d;
}

// A two-bar SFF2 style at 480 PPQ: setup bar, then a one-bar Main A.
Bytes buildStyle(bool withCasm)
{
    Bytes t;
    ev(t, 0, {0xFF, 0x51, 0x03, 0x07, 0xA1, 0x20}); // 120 BPM
    ev(t, 0, {0xFF, 0x58, 0x04, 4, 2, 24, 8});
    marker(t, 0, "SFF2");
    marker(t, 0, "SInt");
    ev(t, 0, {0xBA, 7, 90});                         // bass volume
    marker(t, 1920, "Main A");
    ev(t, 0, {0x99, 36, 100});                       // kick, channel 10
    ev(t, 0, {0x9B, 64, 80});                        // chord note, channel 12
    ev(t, 240, {0x89, 36, 0});
    ev(t, 240, {0x8B, 64, 0});
    ev(t, 1440, {0xFF, 0x2F, 0x00});

    Bytes file;
    chunk(file, "MThd", {0, 0, 0, 1, 0x01, 0xE0});
    chunk(file, "MTrk", t);

    if (withCasm)
    {
        Bytes cseg;
        std::string sdec = "Main A";
        chunk(cseg, "Sdec", Bytes(sdec.begin(), sdec.end()));
        chunk(cseg, "Ctb2", ctb2(9, 1, 0));
        chunk(cseg, "Ctb2", ctb2(11, 0, 2));
        Bytes casm;
        chunk(casm, "CSEG", cseg);
        chunk(file, "CASM", casm);
    }
    return file;
}

} // namespace

TEST_CASE("yamaha SFF2 with CASM")
{
    Style s = readYamahaStyle(buildStyle(true), "test");
    CHECK(s.format == StyleFormat::YamahaSff2);
    CHECK_EQ(int(s.tempo + 0.5), 120);
    CHECK_EQ(s.setup[int(PartRole::Bass)].size(), size_t(1));

    const Section* main = s.section(SectionId::MainA);
    CHECK(main != nullptr);
    if (!main)
        return;
    CHECK_EQ(main->length, int64_t(kPpq * 4));
    CHECK_EQ(main->variations.size(), size_t(1));
    const auto& parts = main->variations[0].parts;
    CHECK_EQ(parts.size(), size_t(2));
    if (parts.size() != 2)
        return;

    CHECK(parts[0].role == PartRole::Rhythm);
    CHECK(parts[0].rule.mid.ntr == Ntr::RootFixed);
    CHECK(parts[0].rule.mid.ntt == Ntt::Bypass);
    CHECK_EQ(parts[0].notes[0].length, 960);

    CHECK(parts[1].role == PartRole::Chord1);
    CHECK(parts[1].rule.mid.ntt == Ntt::Chord);
    CHECK(parts[1].rule.mid.rtr == Rtr::PitchShift);
    CHECK_EQ(int(parts[1].rule.mid.highKey), 6);
    CHECK_EQ(parts[1].notes[0].length, 1920);
}

TEST_CASE("yamaha style without CASM uses default rules")
{
    Style s = readYamahaStyle(buildStyle(false), "test");
    const Section* main = s.section(SectionId::MainA);
    CHECK(main != nullptr);
    if (!main)
        return;
    const auto& parts = main->variations[0].parts;
    CHECK_EQ(parts.size(), size_t(2));
    CHECK(parts[1].rule.mid.ntt == Ntt::Chord);
}

TEST_CASE("garbage is rejected")
{
    Bytes junk{'M', 'T', 'h', 'd', 0, 0};
    bool threw = false;
    try { readYamahaStyle(junk, "x"); } catch (const StyleError&) { threw = true; }
    CHECK(threw);
}
