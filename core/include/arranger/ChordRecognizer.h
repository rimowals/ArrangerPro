#pragma once

#include "arranger/Chord.h"

#include <optional>
#include <span>

namespace arr {

enum class ChordMode : uint8_t {
    Fingered,        // full chord recognition, lowest note never becomes a separate bass
    FingeredOnBass,  // as Fingered, but a lowest note other than the root yields a slash chord
    SingleFinger,    // Yamaha single finger: key = major, + black key below = minor,
                     // + white key below = 7th, + black and white keys below = m7
};

class ChordRecognizer {
public:
    explicit ChordRecognizer(ChordMode mode = ChordMode::FingeredOnBass) : mode_(mode) {}

    void setMode(ChordMode mode) { mode_ = mode; }
    ChordMode mode() const { return mode_; }

    // `notes` are the MIDI note numbers currently held in the chord zone (any order).
    // Returns nullopt when the combination is not a chord; callers keep the previous chord then.
    std::optional<Chord> recognize(std::span<const int> notes) const;

private:
    std::optional<Chord> recognizeFingered(std::span<const int> notes) const;
    std::optional<Chord> recognizeSingleFinger(std::span<const int> notes) const;

    ChordMode mode_;
};

} // namespace arr
