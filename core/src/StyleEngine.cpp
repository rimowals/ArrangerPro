#include "arranger/StyleEngine.h"

#include "arranger/Transposer.h"

#include <algorithm>

namespace arr {

namespace {

bool isProgramMessage(uint8_t status, uint8_t data1)
{
    uint8_t type = status & 0xF0;
    return type == 0xC0 || (type == 0xB0 && (data1 == 0 || data1 == 32));
}

int64_t floorDiv(int64_t a, int64_t b)
{
    int64_t q = a / b;
    return (a % b != 0 && (a < 0) != (b < 0)) ? q - 1 : q;
}

} // namespace

StyleEngine::StyleEngine(MidiSink sink) : sink_(std::move(sink)) {}

void StyleEngine::setStyle(std::shared_ptr<const Style> style)
{
    if (playing_)
        stopAt(now_);
    style_ = std::move(style);
    intro_.reset();
    pending_.reset();
    if (style_)
        selectedMain_ = resolveMain(selectedMain_).value_or(SectionId::MainA);
}

// ---------------------------------------------------------------- transport

void StyleEngine::start()
{
    if (playing_ || !style_)
        return;
    auto main = resolveMain(selectedMain_);
    if (!main)
        return;
    selectedMain_ = *main;
    syncStart_ = false;
    playing_ = true;
    sendSetup(now_);

    SectionId first = (intro_ && style_->has(*intro_)) ? *intro_ : selectedMain_;
    intro_.reset();
    beginSection(first, now_, now_);
}

void StyleEngine::stop()
{
    if (playing_)
        stopAt(now_);
}

void StyleEngine::stopAt(int64_t tick)
{
    releaseAll(tick);
    for (int ch : settings_.outputChannel)
    {
        send(tick, uint8_t(0xB0 | ch), 64, 0);  // sustain off
        send(tick, uint8_t(0xB0 | ch), 123, 0); // all notes off
    }
    playing_ = false;
    pending_.reset();
}

// ---------------------------------------------------------------- section control

void StyleEngine::selectMain(SectionId main)
{
    if (!style_ || !isMain(main))
        return;
    auto resolved = resolveMain(main);
    if (!resolved)
        return;

    if (!playing_ || !isMain(current_))
    {
        // Stopped, or inside an intro/fill/break that leads into the selected main anyway.
        selectedMain_ = *resolved;
        if (playing_ && isEnding(current_))
            pending_ = Pending{barStart(now_) + style_->barLength(), *resolved};
        return;
    }

    selectedMain_ = *resolved;
    const int64_t barEnd = barStart(now_) + style_->barLength();

    if (auto fill = settings_.autoFill ? resolveFill(*resolved) : std::nullopt)
    {
        // A fill pressed in the last beat of a bar would barely sound: play it in the next bar.
        if (barEnd - now_ < style_->beatLength())
            pending_ = Pending{barEnd, *fill};
        else
            beginSection(*fill, barStart(now_), now_);
    }
    else if (*resolved != current_)
        pending_ = Pending{barEnd, *resolved};
}

void StyleEngine::armIntro(std::optional<SectionId> intro)
{
    intro_ = intro && isIntro(*intro) ? intro : std::nullopt;
}

void StyleEngine::selectIntro(SectionId intro)
{
    if (!style_ || !isIntro(intro) || !style_->has(intro))
        return;
    if (!playing_)
    {
        armIntro(intro);
        return;
    }
    pending_ = Pending{barStart(now_) + style_->barLength(), intro};
}

void StyleEngine::selectEnding(SectionId ending)
{
    if (!style_ || !playing_ || !isEnding(ending))
        return;
    const int64_t barEnd = barStart(now_) + style_->barLength();
    if (auto resolved = resolveEnding(ending))
        pending_ = Pending{barEnd, *resolved};
    else
        pending_ = Pending{barEnd, std::nullopt, true};
}

void StyleEngine::triggerBreak()
{
    if (!style_ || !playing_ || !style_->has(SectionId::Break))
        return;
    const int64_t bar = barStart(now_);
    if (bar + style_->barLength() - now_ < style_->beatLength())
        pending_ = Pending{bar + style_->barLength(), SectionId::Break};
    else
        beginSection(SectionId::Break, bar, now_);
}

std::optional<SectionId> StyleEngine::nextSection() const
{
    if (!pending_ || pending_->stop)
        return std::nullopt;
    return pending_->section ? pending_->section : std::optional<SectionId>(selectedMain_);
}

// ---------------------------------------------------------------- chords & mutes

void StyleEngine::setChord(const Chord& chord)
{
    const Chord previous = chord_;
    chord_ = chord;

    if (!playing_)
    {
        if (syncStart_)
            start();
        return;
    }
    if (chord == previous && chord.rootNote == previous.rootNote)
        return;

    const Section* section = currentSectionData();
    if (!section)
        return;

    const size_t variation = section->variationForChord[int(chord.type)] < section->variations.size()
                           ? section->variationForChord[int(chord.type)] : 0;
    if (variation != variation_)
    {
        // Korg chord variations are different patterns: the old one's notes stop.
        releaseAll(now_);
        variation_ = variation;
        return;
    }

    std::vector<ActiveNote> restart;
    releaseIf(now_, [&](const ActiveNote& n) {
        if (!n.part->rule.playsFor(chord_))
            return true;
        const int key = transposeNote(n.sourceKey, n.part->rule, chord_);
        if (key == n.key)
            return false;

        const Rtr rtr = n.part->rule.rangeFor(chord_.rootNote).rtr;
        if (rtr != Rtr::Stop && key >= 0)
        {
            // MIDI cannot bend a single note of a chord, so pitch-shift rules retrigger too.
            const bool toRoot = rtr == Rtr::PitchShiftToRoot || rtr == Rtr::RetriggerToRoot;
            ActiveNote next = n;
            next.key = uint8_t(std::clamp(toRoot ? rootKeyNear(n.key, chord_) : key, 0, 127));
            restart.push_back(next);
        }
        return true;
    });

    for (auto n : restart)
    {
        if (std::any_of(active_.begin(), active_.end(), [&](const ActiveNote& a) { return a.channel == n.channel && a.key == n.key; }))
            continue;
        send(now_, uint8_t(0x90 | n.channel), n.key, n.velocity);
        active_.push_back(n);
    }
}

void StyleEngine::setPartMute(PartRole role, bool muted)
{
    muted_[int(role)] = muted;
    if (muted && playing_)
        releaseIf(now_, [&](const ActiveNote& n) { return n.part->role == role; });
}

// ---------------------------------------------------------------- timeline

void StyleEngine::advanceTo(int64_t tick)
{
    while (playing_ && now_ < tick)
    {
        if (pending_ && pending_->at <= now_)
        {
            handlePending();
            continue;
        }
        const int64_t segmentEnd = pending_ ? std::min(tick, pending_->at) : tick;
        emitRange(now_, segmentEnd);
        now_ = segmentEnd;
        if (pending_ && pending_->at <= now_)
            handlePending();
    }
    if (playing_)
        flushOffs(tick);
    now_ = std::max(now_, tick);
}

void StyleEngine::handlePending()
{
    const Pending p = *pending_;
    pending_.reset();
    if (p.stop)
    {
        stopAt(p.at);
        return;
    }
    beginSection(p.section.value_or(selectedMain_), p.at, p.at);
}

void StyleEngine::beginSection(SectionId id, int64_t sectionStart, int64_t switchTick)
{
    releaseAll(switchTick);

    if (!style_->has(id))
    {
        if (isEnding(id))
        {
            stopAt(switchTick);
            return;
        }
        id = selectedMain_;
    }

    current_ = id;
    sectionStart_ = sectionStart;
    const Section* section = currentSectionData();
    variation_ = section->variationForChord[int(chord_.type)] < section->variations.size()
               ? section->variationForChord[int(chord_.type)] : 0;

    if (isMain(id))
        pending_.reset();
    else
        pending_ = Pending{sectionStart + section->length, std::nullopt, isEnding(id)};
}

void StyleEngine::emitRange(int64_t from, int64_t to)
{
    const Section* section = currentSectionData();
    if (!section || section->length <= 0 || to <= from)
        return;
    const ChordVariation& variation = section->variations[variation_];
    const int64_t length = section->length;

    struct Item {
        int64_t tick;
        int order;
        const Part* part;
        const Note* note;
        const ChannelEvent* control;
    };
    std::vector<Item> items;

    // Pattern windows: mains loop, everything else plays once.
    struct Window { int64_t begin, end, songOffset; };
    std::vector<Window> windows;
    if (isMain(current_))
    {
        int64_t cycle = floorDiv(from - sectionStart_, length);
        while (true)
        {
            const int64_t offset = sectionStart_ + cycle * length;
            if (offset >= to)
                break;
            windows.push_back({std::max<int64_t>(from - offset, 0), std::min<int64_t>(to - offset, length), offset});
            ++cycle;
        }
    }
    else
        windows.push_back({std::max<int64_t>(from - sectionStart_, 0), std::min<int64_t>(to - sectionStart_, length), sectionStart_});

    for (const Part& part : variation.parts)
    {
        if (muted_[int(part.role)] || !part.rule.playsFor(chord_))
            continue;
        for (const Window& w : windows)
        {
            if (w.begin >= w.end)
                continue;
            auto byTick = [](const auto& e, int64_t t) { return e.tick < t; };
            for (auto it = std::lower_bound(part.controls.begin(), part.controls.end(), w.begin, byTick);
                 it != part.controls.end() && it->tick < w.end; ++it)
                items.push_back({w.songOffset + it->tick, 0, &part, nullptr, &*it});
            for (auto it = std::lower_bound(part.notes.begin(), part.notes.end(), w.begin, byTick);
                 it != part.notes.end() && it->tick < w.end; ++it)
                items.push_back({w.songOffset + it->tick, 1, &part, &*it, nullptr});
        }
    }

    std::stable_sort(items.begin(), items.end(), [](const Item& a, const Item& b) {
        return a.tick != b.tick ? a.tick < b.tick : a.order < b.order;
    });

    for (const Item& item : items)
    {
        flushOffs(item.tick);
        if (item.note)
        {
            // Notes never ring past the end of the pattern cycle they belong to.
            const int64_t cycleEnd = item.tick - item.note->tick + length;
            startNote(item.tick, *item.part, *item.note, std::min<int64_t>(item.tick + item.note->length, cycleEnd));
        }
        else
            sendPartEvent(item.tick, int(item.part->role), *item.control);
    }
    flushOffs(to);
}

void StyleEngine::startNote(int64_t tick, const Part& part, const Note& note, int64_t end)
{
    const int key = transposeNote(note.key, part.rule, chord_);
    if (key < 0)
        return;
    const uint8_t channel = uint8_t(settings_.outputChannel[int(part.role)]);

    // Re-striking a sounding key: end the old note first so the pair stays balanced.
    releaseIf(tick, [&](const ActiveNote& n) { return n.channel == channel && n.key == key; });

    send(tick, uint8_t(0x90 | channel), uint8_t(key), note.velocity);
    active_.push_back({end, &part, note.key, uint8_t(key), note.velocity, channel});
}

void StyleEngine::sendNoteOff(int64_t tick, const ActiveNote& n)
{
    send(tick, uint8_t(0x80 | n.channel), n.key, 0);
}

void StyleEngine::flushOffs(int64_t upTo)
{
    // Emit in time order so a renderer sees non-decreasing ticks.
    std::stable_sort(active_.begin(), active_.end(), [](const ActiveNote& a, const ActiveNote& b) { return a.end < b.end; });
    size_t i = 0;
    while (i < active_.size() && active_[i].end <= upTo)
    {
        sendNoteOff(std::max(active_[i].end, now_), active_[i]);
        ++i;
    }
    active_.erase(active_.begin(), active_.begin() + ptrdiff_t(i));
}

void StyleEngine::releaseAll(int64_t tick)
{
    releaseIf(tick, [](const ActiveNote&) { return true; });
}

void StyleEngine::releaseIf(int64_t tick, const std::function<bool(const ActiveNote&)>& predicate)
{
    auto it = std::stable_partition(active_.begin(), active_.end(), [&](const ActiveNote& n) { return !predicate(n); });
    for (auto n = it; n != active_.end(); ++n)
        sendNoteOff(tick, *n);
    active_.erase(it, active_.end());
}

void StyleEngine::send(int64_t tick, uint8_t status, uint8_t d1, uint8_t d2, uint8_t size)
{
    if (sink_)
        sink_(MidiOut{tick, {status, uint8_t(d1 & 0x7F), uint8_t(d2 & 0x7F)}, size});
}

void StyleEngine::sendPartEvent(int64_t tick, int role, const ChannelEvent& e)
{
    if (!settings_.sendProgramChanges && isProgramMessage(e.status, e.data1))
        return;
    const uint8_t type = e.type();
    uint8_t value = e.data2;
    if (type == 0xB0 && e.data1 == 7 && settings_.volume[role] >= 0)
        value = uint8_t(settings_.volume[role]);
    const uint8_t channel = uint8_t(settings_.outputChannel[role]);
    send(tick, uint8_t(type | channel), e.data1, value, (type == 0xC0 || type == 0xD0) ? 2 : 3);
}

void StyleEngine::sendSetup(int64_t tick)
{
    for (int role = 0; role < kPartCount; ++role)
    {
        for (const auto& e : style_->setup[role])
            sendPartEvent(tick, role, e);
        if (settings_.volume[role] >= 0)
            send(tick, uint8_t(0xB0 | settings_.outputChannel[role]), 7, uint8_t(settings_.volume[role]));
    }
}

void StyleEngine::sendSetupNow()
{
    if (style_)
        sendSetup(now_);
}

void StyleEngine::setPartVolume(PartRole role, int volume)
{
    settings_.volume[int(role)] = std::clamp(volume, -1, 127);
    if (volume >= 0)
        send(now_, uint8_t(0xB0 | settings_.outputChannel[int(role)]), 7, uint8_t(volume));
}

// ---------------------------------------------------------------- helpers

const Section* StyleEngine::currentSectionData() const
{
    return style_ ? style_->section(current_) : nullptr;
}

int64_t StyleEngine::barStart(int64_t tick) const
{
    const int64_t bar = style_->barLength();
    return sectionStart_ + floorDiv(tick - sectionStart_, bar) * bar;
}

int64_t StyleEngine::positionInSection() const
{
    const Section* s = currentSectionData();
    if (!playing_ || !s || s->length <= 0)
        return 0;
    int64_t pos = now_ - sectionStart_;
    return isMain(current_) ? ((pos % s->length) + s->length) % s->length : std::clamp<int64_t>(pos, 0, s->length);
}

int StyleEngine::barInSection() const
{
    return style_ ? int(positionInSection() / style_->barLength()) : 0;
}

int StyleEngine::beatInBar() const
{
    return style_ ? int((positionInSection() % style_->barLength()) / style_->beatLength()) : 0;
}

std::optional<SectionId> StyleEngine::resolveMain(SectionId main) const
{
    if (style_->has(main))
        return main;
    // Nearest existing main, preferring the lower neighbour (B missing → A before C).
    for (int distance = 1; distance < 4; ++distance)
        for (int index : {mainIndex(main) - distance, mainIndex(main) + distance})
            if (index >= 0 && index < 4 && style_->has(mainSection(index)))
                return mainSection(index);
    return std::nullopt;
}

std::optional<SectionId> StyleEngine::resolveFill(SectionId main) const
{
    if (style_->has(fillInto(main)))
        return fillInto(main);
    for (int distance = 1; distance < 4; ++distance)
        for (int index : {mainIndex(main) - distance, mainIndex(main) + distance})
            if (index >= 0 && index < 4 && style_->has(fillInto(mainSection(index))))
                return fillInto(mainSection(index));
    return std::nullopt;
}

std::optional<SectionId> StyleEngine::resolveEnding(SectionId ending) const
{
    if (style_->has(ending))
        return ending;
    for (int i = int(SectionId::EndingA); i <= int(SectionId::EndingD); ++i)
        if (style_->has(SectionId(i)))
            return SectionId(i);
    return std::nullopt;
}

} // namespace arr
