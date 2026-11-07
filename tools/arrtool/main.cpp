// arrtool — inspect styles and render them to MIDI files without real-time I/O.
//
//   arrtool info   <style file | Korg bank | Korg .SET folder> [--detail]
//   arrtool render <path> [--style N] --song "C | Am | @B F | G7 !break | C" [--intro A] [--ending A]
//                  [--tempo BPM] [--out file.mid]
//
// Song script: each bar is separated by '|'. A bar holds one or more chord symbols (split evenly)
// and optional commands: @A..@D select a main section, !break triggers the break, '-' repeats
// the previous chord.

#include "arranger/KorgPads.h"
#include "arranger/SmfWriter.h"
#include "arranger/StyleEngine.h"
#include "arranger/StyleLoader.h"

#include <cctype>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

using namespace arr;

namespace {

std::string_view ntrName(Ntr n)
{
    switch (n)
    {
        case Ntr::RootTrans: return "RootTrans";
        case Ntr::RootFixed: return "RootFixed";
        case Ntr::Guitar: return "Guitar";
    }
    return "?";
}

std::string_view nttName(Ntt n)
{
    static constexpr std::string_view kNames[] = {
        "Bypass", "Melody", "Chord", "MelMinor", "MelMinor5", "HarmMinor", "HarmMinor5",
        "NatMinor", "NatMinor5", "Dorian", "Dorian5", "GtAllPurpose", "GtStroke", "GtArpeggio"};
    return kNames[int(n)];
}

std::string_view rtrName(Rtr r)
{
    static constexpr std::string_view kNames[] = {"Stop", "PitchShift", "PitchShiftRoot", "Retrigger", "RetriggerRoot", "NoteGen"};
    return kNames[int(r)];
}

int cmdInfo(const std::vector<Style>& styles, bool detail)
{
    for (size_t i = 0; i < styles.size(); ++i)
    {
        const Style& s = styles[i];
        std::printf("[%zu] %s  (%s, %.0f BPM, %d/%d)\n", i, s.name.c_str(), std::string(styleFormatName(s.format)).c_str(),
                    s.tempo, s.timeSigNum, s.timeSigDen);

        for (int id = 0; id < kSectionCount; ++id)
        {
            const Section* sec = s.section(SectionId(id));
            if (!sec)
                continue;
            std::printf("    %-11s %5.2f bars", std::string(sectionName(sec->id)).c_str(), double(sec->length) / double(s.barLength()));
            if (sec->variations.size() > 1)
                std::printf(", %zu chord variations", sec->variations.size());
            std::printf("\n");

            if (!detail)
                continue;
            for (size_t v = 0; v < sec->variations.size(); ++v)
                for (const Part& p : sec->variations[v].parts)
                {
                    const TransposeRange& r = p.rule.mid;
                    Chord src{p.rule.sourceRoot, p.rule.sourceType};
                    std::printf("        v%zu %-9s ch%-2d %4zu notes  src %-6s %s/%s hk=%s %d-%d %s%s%s\n", v,
                                std::string(partRoleName(p.role)).c_str(), p.sourceChannel + 1, p.notes.size(),
                                src.name().c_str(), std::string(ntrName(r.ntr)).c_str(), std::string(nttName(r.ntt)).c_str(),
                                std::string(pitchClassName(r.highKey)).c_str(), r.noteLow, r.noteHigh,
                                std::string(rtrName(r.rtr)).c_str(), r.bassOn ? " bassOn" : "",
                                p.rule.hasRanges ? " (3 ranges)" : "");
                }
        }
        for (size_t o = 0; o < s.ots.size(); ++o)
        {
            std::printf("    OTS %zu:", o + 1);
            for (int p = 0; p < kKeyboardPartCount; ++p)
            {
                const PartVoice& v = s.ots[o].parts[size_t(p)];
                std::printf("  %s=", std::string(keyboardPartName(KeyboardPart(p))).c_str());
                if (!v.on)
                    std::printf("off");
                else if (v.hasVoice)
                {
                    std::printf("%d/%d/%d", v.bankMsb, v.bankLsb, v.program);
                    if (v.transpose)
                        std::printf("(%+d)", v.transpose);
                }
                else
                    std::printf("on");
            }
            for (int p = 0; p < kStylePadCount; ++p)
                if (const PadRef& pad = s.ots[o].pads[size_t(p)]; pad.id >= 0)
                    std::printf("  pad%d=#%d %s", p + 1, pad.id, pad.id >= 0 && korgPa50Pad(pad.id) ? std::string(korgPa50Pad(pad.id)->name).c_str() : "");
            std::printf("\n");
        }
        for (const auto& w : s.warnings)
            std::printf("    warning: %s\n", w.c_str());
    }
    return 0;
}

std::optional<SectionId> sectionLetter(std::string_view prefix, char letter)
{
    if (letter < 'A' || letter > 'D')
        return std::nullopt;
    return sectionFromYamahaMarker(std::string(prefix) + " " + letter);
}

struct Bar {
    std::vector<Chord> chords;
    std::optional<SectionId> main;
    bool breakFill = false;
};

std::vector<Bar> parseSong(const std::string& script)
{
    std::vector<Bar> bars;
    std::stringstream barStream(script);
    std::string barText;
    Chord previous;
    bool havePrevious = false;

    while (std::getline(barStream, barText, '|'))
    {
        Bar bar;
        std::stringstream tokens(barText);
        std::string token;
        while (tokens >> token)
        {
            if (token.size() == 2 && token[0] == '@')
            {
                bar.main = sectionLetter("Main", char(std::toupper(static_cast<unsigned char>(token[1]))));
                if (!bar.main)
                    throw StyleError("bad section command: " + token);
            }
            else if (token == "!break")
                bar.breakFill = true;
            else if (token == "-")
            {
                if (!havePrevious)
                    throw StyleError("'-' before any chord");
                bar.chords.push_back(previous);
            }
            else if (auto chord = parseChord(token))
            {
                bar.chords.push_back(*chord);
                previous = *chord;
                havePrevious = true;
            }
            else
                throw StyleError("cannot parse chord: " + token);
        }
        if (bar.chords.empty() && havePrevious)
            bar.chords.push_back(previous);
        bars.push_back(std::move(bar));
    }
    return bars;
}

int cmdRender(std::vector<Style> styles, const std::map<std::string, std::string>& opts)
{
    size_t index = opts.contains("--style") ? std::stoul(opts.at("--style")) : 0;
    if (index >= styles.size())
        throw StyleError("style index out of range");
    auto style = std::make_shared<Style>(std::move(styles[index]));
    if (!opts.contains("--song"))
        throw StyleError("--song is required");

    const auto bars = parseSong(opts.at("--song"));
    std::vector<MidiOut> out;
    StyleEngine engine([&](const MidiOut& m) { out.push_back(m); });
    engine.settings().sendProgramChanges = true; // rendered files are played on GM/XG players
    engine.setStyle(style);

    if (opts.contains("--intro"))
        engine.armIntro(sectionLetter("Intro", opts.at("--intro")[0]));
    if (!bars.empty() && bars[0].main)
        engine.selectMain(*bars[0].main);
    if (!bars.empty() && !bars[0].chords.empty())
        engine.setChord(bars[0].chords[0]);
    engine.start();

    const int64_t bar = style->barLength();
    int64_t t = 0;
    for (size_t b = 0; b < bars.size(); ++b)
    {
        engine.advanceTo(t);
        if (b > 0 && bars[b].main)
            engine.selectMain(*bars[b].main);
        if (bars[b].breakFill)
            engine.triggerBreak();
        const auto& chords = bars[b].chords;
        for (size_t c = 0; c < chords.size(); ++c)
        {
            engine.advanceTo(t + bar * int64_t(c) / int64_t(chords.size()));
            engine.setChord(chords[c]);
        }
        t += bar;
    }
    engine.advanceTo(t);

    if (opts.contains("--ending"))
    {
        engine.selectEnding(*sectionLetter("Ending", opts.at("--ending")[0]));
        for (int guard = 0; engine.isPlaying() && guard < 64; ++guard)
            engine.advanceTo(t += bar);
    }
    engine.stop();

    const double tempo = opts.contains("--tempo") ? std::stod(opts.at("--tempo")) : style->tempo;
    const auto smf = writeSmf(out, tempo, style->timeSigNum, style->timeSigDen);
    const std::string path = opts.contains("--out") ? opts.at("--out") : "render.mid";
    std::ofstream(path, std::ios::binary).write(reinterpret_cast<const char*>(smf.data()), std::streamsize(smf.size()));
    std::printf("%s: %zu MIDI messages, %.1f bars -> %s\n", style->name.c_str(), out.size(), double(t) / double(bar), path.c_str());
    return 0;
}

int usage()
{
    std::fputs("usage:\n"
               "  arrtool info <path> [--detail]\n"
               "  arrtool render <path> [--style N] --song \"C | Am | @B F | G7\" [--intro A] [--ending A] [--tempo BPM] [--out file.mid]\n",
               stderr);
    return 2;
}

} // namespace

int main(int argc, char** argv)
{
    if (argc < 3)
        return usage();

    const std::string command = argv[1];
    std::map<std::string, std::string> opts;
    for (int i = 3; i < argc; ++i)
    {
        std::string key = argv[i];
        if (key == "--detail")
            opts[key] = "1";
        else if (i + 1 < argc)
            opts[key] = argv[++i];
        else
            return usage();
    }

    try
    {
        auto styles = loadStyles(argv[2]);
        if (command == "info")
            return cmdInfo(styles, opts.contains("--detail"));
        if (command == "render")
            return cmdRender(std::move(styles), opts);
        return usage();
    }
    catch (const std::exception& e)
    {
        std::fprintf(stderr, "error: %s\n", e.what());
        return 1;
    }
}
