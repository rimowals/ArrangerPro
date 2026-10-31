#pragma once

#include "arranger/Style.h"

#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

namespace arr {

// Yamaha .sty/.prs/.bcs/.sst (SFF1, SFF2, SFF GE): one style per file.
Style readYamahaStyle(std::span<const uint8_t> file, std::string name);

// Korg Pa-series style bank (e.g. STYLE/USER01.STY inside a .SET folder): many styles per file.
std::vector<Style> readKorgStyleBank(std::span<const uint8_t> file, const std::string& bankName);

// Loads a Yamaha style file, a Korg bank file, or every bank of a Korg .SET folder.
// Throws StyleError when the content is not recognised.
std::vector<Style> loadStyles(const std::filesystem::path& path);

} // namespace arr
