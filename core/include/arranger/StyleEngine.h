#pragma once

#include "arranger/Chord.h"
#include "arranger/Style.h"

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

namespace arr {

struct MidiOut {
    int64_t tick;       // song position (kPpq) at which the message is due
    uint8_t data[3];
    uint8_t size;
};

using MidiSink = std::function<void(const MidiOut&)>;

struct EngineSettings {
    std::array<int, kPartCount> outputChannel{8, 9, 10, 11, 12, 13, 14, 15}; // 0-based
    bool sendProgramChanges = false; // off by default: sounds are chosen in the VST host
    bool autoFill = true;            // a fill leads into a newly selected main section
    std::array<int, kPartCount> volume{-1, -1, -1, -1, -1, -1, -1, -1}; // CC7 override, -1 = the style's
};

// Tick-driven style player. Not thread-safe: the host serialises calls (e.g. under a lock
// shared with its timer). All control calls take effect at now().
class StyleEngine {
public:
    explicit StyleEngine(MidiSink sink);

    void setStyle(std::shared_ptr<const Style> style);
    const Style* style() const { return style_.get(); }
    EngineSettings& settings() { return settings_; }
    const EngineSettings& settings() const { return settings_; }

    // ---- transport ----
    void start();
    void stop();
    bool isPlaying() const { return playing_; }
    void setSyncStart(bool armed) { syncStart_ = armed; }
    bool syncStartArmed() const { return syncStart_; }

    // ---- sections ----
    void selectMain(SectionId main);         // while playing: fill (if enabled) then switch at the bar line
    void armIntro(std::optional<SectionId> intro); // used by the next start()
    // Stopped: arms the intro for start(). Playing: the intro starts at the next bar line and
    // leads back into the selected main.
    void selectIntro(SectionId intro);
    void selectEnding(SectionId ending);     // while playing: ending from the next bar, then stop
    void triggerBreak();

    // ---- performance ----
    void setChord(const Chord& chord);
    const Chord& chord() const { return chord_; }
    void setPartMute(PartRole role, bool muted);
    bool partMuted(PartRole role) const { return muted_[int(role)]; }
    void setPartVolume(PartRole role, int volume); // 0..127, -1 = back to the style's level
    // Sends the style's channel setup (voices, volumes, effects) now, e.g. right after loading
    // a style, so a GM/XG module has the right sounds before playback starts.
    void sendSetupNow();

    // Emits every message due before `tick` (note-offs up to and including `tick`).
    void advanceTo(int64_t tick);
    int64_t now() const { return now_; }

    // ---- status for the UI ----
    SectionId currentSection() const { return current_; }
    SectionId selectedMain() const { return selectedMain_; }
    std::optional<SectionId> armedIntro() const { return intro_; }
    std::optional<SectionId> nextSection() const;
    int64_t positionInSection() const;
    int barInSection() const;
    int beatInBar() const;

private:
    struct ActiveNote {
        int64_t end;
        const Part* part;
        uint8_t sourceKey, key, velocity, channel;
    };

    struct Pending {
        int64_t at;
        std::optional<SectionId> section; // nullopt = the selected main
        bool stop = false;
    };

    const Section* currentSectionData() const;
    void beginSection(SectionId id, int64_t sectionStart, int64_t switchTick);
    void handlePending();
    void emitRange(int64_t from, int64_t to);
    void startNote(int64_t tick, const Part& part, const Note& note, int64_t end);
    void sendNoteOff(int64_t tick, const ActiveNote& n);
    void flushOffs(int64_t upTo);
    void releaseAll(int64_t tick);
    void releaseIf(int64_t tick, const std::function<bool(const ActiveNote&)>& predicate);
    void send(int64_t tick, uint8_t status, uint8_t d1, uint8_t d2, uint8_t size = 3);
    void sendPartEvent(int64_t tick, int role, const ChannelEvent& e);
    void sendSetup(int64_t tick);
    void stopAt(int64_t tick);

    int64_t barStart(int64_t tick) const;
    std::optional<SectionId> resolveMain(SectionId main) const;
    std::optional<SectionId> resolveFill(SectionId main) const;
    std::optional<SectionId> resolveEnding(SectionId ending) const;

    MidiSink sink_;
    EngineSettings settings_;
    std::shared_ptr<const Style> style_;

    bool playing_ = false;
    bool syncStart_ = false;
    int64_t now_ = 0;

    SectionId current_ = SectionId::MainA;
    SectionId selectedMain_ = SectionId::MainA;
    std::optional<SectionId> intro_;
    int64_t sectionStart_ = 0;
    size_t variation_ = 0;
    std::optional<Pending> pending_;

    Chord chord_;
    std::array<bool, kPartCount> muted_{};
    std::vector<ActiveNote> active_;
};

} // namespace arr
