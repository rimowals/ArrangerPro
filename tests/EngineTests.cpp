#include "Test.h"

#include "arranger/StyleEngine.h"

#include <memory>
#include <vector>

using namespace arr;

namespace {

constexpr int64_t kBeat = kPpq;
constexpr int64_t kBar = kPpq * 4;

Section oneBar(SectionId id, std::vector<Note> notes)
{
    Part part;
    part.role = PartRole::Chord1;
    part.rule.mid.ntr = Ntr::RootTrans;
    part.rule.mid.ntt = Ntt::Chord;
    part.rule.mid.rtr = Rtr::PitchShift;
    part.notes = std::move(notes);

    Section s;
    s.id = id;
    s.length = kBar;
    s.variations.push_back({{part}});
    return s;
}

std::shared_ptr<Style> testStyle()
{
    auto style = std::make_shared<Style>();
    style->sections[int(SectionId::MainA)] = oneBar(SectionId::MainA, {{0, int32_t(kBeat), 60, 100}});
    style->sections[int(SectionId::MainB)] = oneBar(SectionId::MainB, {{0, int32_t(kBeat), 67, 100}});
    style->sections[int(SectionId::FillBB)] = oneBar(SectionId::FillBB, {{0, int32_t(kBeat), 62, 100}, {3 * kBeat, int32_t(kBeat), 64, 100}});
    style->sections[int(SectionId::EndingA)] = oneBar(SectionId::EndingA, {{0, int32_t(2 * kBeat), 72, 100}});
    style->sections[int(SectionId::IntroA)] = oneBar(SectionId::IntroA, {{0, int32_t(kBeat), 48, 100}});
    return style;
}

struct Recorder {
    std::vector<MidiOut> events;
    StyleEngine engine{[this](const MidiOut& m) { events.push_back(m); }};

    std::vector<std::pair<int64_t, int>> noteOns() const
    {
        std::vector<std::pair<int64_t, int>> r;
        for (const auto& e : events)
            if ((e.data[0] & 0xF0) == 0x90 && e.data[2] > 0)
                r.push_back({e.tick, e.data[1]});
        return r;
    }

    int balance() const
    {
        int b = 0;
        for (const auto& e : events)
            if ((e.data[0] & 0xF0) == 0x90)
                ++b;
            else if ((e.data[0] & 0xF0) == 0x80)
                --b;
        return b;
    }
};

using On = std::vector<std::pair<int64_t, int>>;

} // namespace

TEST_CASE("main section loops")
{
    Recorder r;
    r.engine.setStyle(testStyle());
    r.engine.start();
    r.engine.advanceTo(2 * kBar);
    CHECK(r.noteOns() == On({{0, 60}, {kBar, 60}}));
    CHECK_EQ(r.balance(), 0);
}

TEST_CASE("chord change retriggers a sounding note")
{
    Recorder r;
    r.engine.setStyle(testStyle());
    r.engine.start();
    r.engine.advanceTo(kBeat / 2);
    r.engine.setChord(*parseChord("F"));
    r.engine.advanceTo(kBar + 1);
    CHECK(r.noteOns() == On({{0, 60}, {kBeat / 2, 65}, {kBar, 65}}));
}

TEST_CASE("fill leads into the new main at the bar line")
{
    Recorder r;
    r.engine.setStyle(testStyle());
    r.engine.start();
    r.engine.advanceTo(kBeat + 10);       // in beat 2 of bar 1
    r.engine.selectMain(SectionId::MainB);
    CHECK(r.engine.currentSection() == SectionId::FillBB);
    r.engine.advanceTo(2 * kBar);
    // fill plays from the current position (its beat-4 note), then Main B from bar 2
    CHECK(r.noteOns() == On({{0, 60}, {3 * kBeat, 64}, {kBar, 67}}));
    CHECK(r.engine.currentSection() == SectionId::MainB);
}

TEST_CASE("switch without fills waits for the bar line")
{
    Recorder r;
    r.engine.settings().autoFill = false;
    r.engine.setStyle(testStyle());
    r.engine.start();
    r.engine.advanceTo(kBeat);
    r.engine.selectMain(SectionId::MainB);
    r.engine.advanceTo(kBar + 1);
    CHECK(r.noteOns() == On({{0, 60}, {kBar, 67}}));
}

TEST_CASE("intro, then main; ending stops")
{
    Recorder r;
    r.engine.setStyle(testStyle());
    r.engine.armIntro(SectionId::IntroA);
    r.engine.start();
    r.engine.advanceTo(kBar + kBeat);
    r.engine.selectEnding(SectionId::EndingA);
    r.engine.advanceTo(4 * kBar);
    CHECK(r.noteOns() == On({{0, 48}, {kBar, 60}, {2 * kBar, 72}}));
    CHECK(!r.engine.isPlaying());
    CHECK_EQ(r.balance(), 0);
}

TEST_CASE("intro can be started again while playing")
{
    Recorder r;
    r.engine.setStyle(testStyle());
    r.engine.armIntro(SectionId::IntroA);
    r.engine.start();
    r.engine.advanceTo(2 * kBar + kBeat);     // intro done, Main A playing in bar 2
    r.engine.selectIntro(SectionId::IntroA);
    CHECK(r.engine.nextSection() == SectionId::IntroA);
    r.engine.advanceTo(4 * kBar + 1);
    // intro again from bar 3, then back to Main A in bar 4
    CHECK(r.noteOns() == On({{0, 48}, {kBar, 60}, {2 * kBar, 60}, {3 * kBar, 48}, {4 * kBar, 60}}));
}

TEST_CASE("sync start begins with the first chord")
{
    Recorder r;
    r.engine.setStyle(testStyle());
    r.engine.setSyncStart(true);
    r.engine.advanceTo(500);
    CHECK(!r.engine.isPlaying());
    r.engine.setChord(*parseChord("G"));
    CHECK(r.engine.isPlaying());
    r.engine.advanceTo(600);
    CHECK(r.noteOns() == On({{500, 55}}));
}

TEST_CASE("mixer volume overrides the style's CC7")
{
    auto style = testStyle();
    style->setup[int(PartRole::Chord1)].push_back({0, 0xB0, 7, 90});
    Recorder r;
    r.engine.setStyle(style);
    r.engine.setPartVolume(PartRole::Chord1, 50);
    r.events.clear();
    r.engine.start();
    int last = -1;
    for (const auto& e : r.events)
        if (e.data[0] == (0xB0 | 11) && e.data[1] == 7)
            last = e.data[2];
    CHECK_EQ(last, 50);
}

TEST_CASE("program changes are sent only when enabled")
{
    auto style = testStyle();
    style->setup[int(PartRole::Bass)] = {{0, 0xB0, 0, 0}, {0, 0xC0, 33, 0}};
    auto programChanges = [](const Recorder& r) {
        int n = 0;
        for (const auto& e : r.events)
            if (e.data[0] == (0xC0 | 10) && e.data[1] == 33 && e.size == 2)
                ++n;
        return n;
    };

    Recorder off;
    off.engine.setStyle(style);
    off.engine.sendSetupNow();
    CHECK_EQ(programChanges(off), 0);

    Recorder on;
    on.engine.settings().sendProgramChanges = true;
    on.engine.setStyle(style);
    on.engine.sendSetupNow();
    CHECK_EQ(programChanges(on), 1);
}

TEST_CASE("muted part is silent and released")
{
    Recorder r;
    r.engine.setStyle(testStyle());
    r.engine.start();
    r.engine.advanceTo(10);
    r.engine.setPartMute(PartRole::Chord1, true);
    CHECK_EQ(r.balance(), 0);
    r.engine.advanceTo(2 * kBar);
    CHECK_EQ(r.noteOns().size(), size_t(1));
}
