#include "arranger/StyleLoader.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iterator>

namespace arr {

namespace fs = std::filesystem;

namespace {

std::vector<uint8_t> readFile(const fs::path& path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in)
        throw StyleError("cannot open " + path.string());
    return std::vector<uint8_t>(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

bool startsWith(const std::vector<uint8_t>& data, std::string_view magic)
{
    return data.size() >= magic.size() && std::equal(magic.begin(), magic.end(), data.begin());
}

std::string upper(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return char(std::toupper(c)); });
    return s;
}

std::vector<Style> loadFile(const fs::path& path)
{
    const auto data = readFile(path);
    std::vector<Style> styles;

    if (startsWith(data, "MThd"))
    {
        styles.push_back(readYamahaStyle(data, path.stem().string()));
    }
    else
    {
        styles = readKorgStyleBank(data, path.stem().string());
        // Korg names are only unique per bank; prefix the bank for display.
        for (auto& s : styles)
            if (s.name.empty())
                s.name = path.stem().string();
    }

    for (auto& s : styles)
        s.sourcePath = path.string();
    return styles;
}

} // namespace

std::vector<Style> loadStyles(const fs::path& path)
{
    if (!fs::is_directory(path))
        return loadFile(path);

    // A Korg .SET folder keeps its style banks in STYLE/*.STY.
    fs::path styleDir = path / "STYLE";
    if (!fs::is_directory(styleDir))
        throw StyleError(path.string() + " is a folder without a STYLE subfolder");

    std::vector<fs::path> banks;
    for (const auto& entry : fs::directory_iterator(styleDir))
        if (entry.is_regular_file() && upper(entry.path().extension().string()) == ".STY")
            banks.push_back(entry.path());
    std::sort(banks.begin(), banks.end());

    std::vector<Style> styles;
    for (const auto& bank : banks)
    {
        auto loaded = loadFile(bank);
        std::move(loaded.begin(), loaded.end(), std::back_inserter(styles));
    }
    return styles;
}

} // namespace arr
