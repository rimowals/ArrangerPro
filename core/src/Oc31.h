#pragma once

#include <cstdint>
#include <span>
#include <vector>

namespace arr::detail {

// KORG's "OC31" LZSS-variant compression used inside Pa-series bank files.
// Algorithm as publicly documented by the KORG-Tools project (documentation/oc31.c, Amir Czwink).
bool looksLikeOc31(std::span<const uint8_t> payload);
std::vector<uint8_t> decompressOc31(std::span<const uint8_t> payload);

} // namespace arr::detail
