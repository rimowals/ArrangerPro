#pragma once

#include "arranger/StyleEngine.h"

#include <cstdint>
#include <vector>

namespace arr {

// Writes engine output as a format-0 Standard MIDI File at kPpq resolution.
std::vector<uint8_t> writeSmf(const std::vector<MidiOut>& events, double tempo, int timeSigNum, int timeSigDen);

} // namespace arr
