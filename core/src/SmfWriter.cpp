#include "arranger/SmfWriter.h"

#include <algorithm>
#include <cmath>

namespace arr {

namespace {

void putVarLen(std::vector<uint8_t>& out, uint64_t value)
{
    uint8_t buffer[10];
    int n = 0;
    buffer[n++] = uint8_t(value & 0x7F);
    while (value >>= 7)
        buffer[n++] = uint8_t(0x80 | (value & 0x7F));
    while (n > 0)
        out.push_back(buffer[--n]);
}

void putU32(std::vector<uint8_t>& out, uint32_t v)
{
    for (int shift = 24; shift >= 0; shift -= 8)
        out.push_back(uint8_t(v >> shift));
}

} // namespace

std::vector<uint8_t> writeSmf(const std::vector<MidiOut>& events, double tempo, int timeSigNum, int timeSigDen)
{
    std::vector<MidiOut> sorted = events;
    std::stable_sort(sorted.begin(), sorted.end(), [](const MidiOut& a, const MidiOut& b) { return a.tick < b.tick; });

    std::vector<uint8_t> track;
    const uint32_t usPerQuarter = uint32_t(std::lround(60'000'000.0 / std::clamp(tempo, 10.0, 400.0)));
    uint8_t denPower = 0;
    while ((1 << denPower) < timeSigDen && denPower < 6)
        ++denPower;

    track.insert(track.end(), {0x00, 0xFF, 0x51, 0x03, uint8_t(usPerQuarter >> 16), uint8_t(usPerQuarter >> 8), uint8_t(usPerQuarter)});
    track.insert(track.end(), {0x00, 0xFF, 0x58, 0x04, uint8_t(timeSigNum), denPower, 24, 8});

    int64_t last = 0;
    for (const auto& e : sorted)
    {
        const int64_t tick = std::max<int64_t>(e.tick, 0);
        putVarLen(track, uint64_t(std::max<int64_t>(tick - last, 0)));
        last = std::max(last, tick);
        track.insert(track.end(), e.data, e.data + e.size);
    }
    track.insert(track.end(), {0x00, 0xFF, 0x2F, 0x00});

    std::vector<uint8_t> file = {'M', 'T', 'h', 'd', 0, 0, 0, 6, 0, 0, 0, 1, uint8_t(kPpq >> 8), uint8_t(kPpq & 0xFF)};
    file.insert(file.end(), {'M', 'T', 'r', 'k'});
    putU32(file, uint32_t(track.size()));
    file.insert(file.end(), track.begin(), track.end());
    return file;
}

} // namespace arr
