#pragma once

#include "arranger/Chord.h"
#include "arranger/Style.h"

namespace arr {

// Maps a pattern note, recorded against `rule.sourceRoot`/`rule.sourceType`, onto `chord`.
// Returns the MIDI key to play, or -1 when the note must be dropped (outside the note limits).
int transposeNote(int key, const TransposeRule& rule, const Chord& chord);

// The pitch class the source chord's root is mapped to for "...ToRoot" retrigger rules.
int rootKeyNear(int key, const Chord& chord);

} // namespace arr
