#include "arranger/Style.h"

namespace arr {

namespace {

constexpr std::string_view kPartNames[kPartCount] = {
    "SubRhythm", "Rhythm", "Bass", "Chord1", "Chord2", "Pad", "Phrase1", "Phrase2"};

// Yamaha marker names, which double as display names.
constexpr std::string_view kSectionNames[kSectionCount] = {
    "Intro A", "Intro B", "Intro C", "Intro D",
    "Main A", "Main B", "Main C", "Main D",
    "Fill In AA", "Fill In BB", "Fill In CC", "Fill In DD", "Fill In BA",
    "Ending A", "Ending B", "Ending C", "Ending D",
};

} // namespace

std::string_view partRoleName(PartRole role)
{
    return kPartNames[int(role)];
}

std::string_view sectionName(SectionId id)
{
    return kSectionNames[int(id)];
}

std::optional<SectionId> sectionFromYamahaMarker(std::string_view marker)
{
    while (!marker.empty() && (marker.back() == ' ' || marker.back() == '\0'))
        marker.remove_suffix(1);
    while (!marker.empty() && marker.front() == ' ')
        marker.remove_prefix(1);

    for (int i = 0; i < kSectionCount; ++i)
        if (kSectionNames[i] == marker)
            return SectionId(i);
    return std::nullopt;
}

std::string_view styleFormatName(StyleFormat f)
{
    switch (f)
    {
        case StyleFormat::YamahaSff1: return "Yamaha SFF1";
        case StyleFormat::YamahaSff2: return "Yamaha SFF2";
        case StyleFormat::KorgPa: return "Korg Pa";
    }
    return "?";
}

} // namespace arr
