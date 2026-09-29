#include "dsp/synth_engine.h"
#include "dsp/dsp_profile.h"
#include "midi/midi_event.h"
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <vector>

using namespace pocketpan;
using namespace pocketpan::dsp;

namespace {

struct Event {
    uint32_t sample;
    midi::MidiEventType type;
    uint8_t data1;
    uint8_t data2;
};

constexpr size_t kFrames = 128;

struct Render {
    std::vector<int32_t> pcm;
    uint32_t fastPathBlocks = 0;
};

Render render(bool fast, InstrumentModel model, const std::vector<Event>& events,
              uint32_t totalFrames, uint32_t switchAt, InstrumentModel switchTo) {
    SynthEngine engine;
    engine.init(48000.0f);
    engine.setInstrumentModel(model);
#if POCKETPAN_SUSTAIN_FASTPATH
    engine.setSustainFastPathEnabledForTest(fast);
#endif
    Render result;
    result.pcm.reserve(totalFrames * 2);
    int32_t buffer[kFrames * 2];
    size_t next = 0;
    for (uint32_t pos = 0; pos < totalFrames; pos += kFrames) {
        if (switchAt != 0xFFFFFFFFu && pos == switchAt) {
            engine.setInstrumentModel(switchTo);
#if POCKETPAN_SUSTAIN_FASTPATH
            engine.setSustainFastPathEnabledForTest(fast);
#endif
        }
        while (next < events.size() && events[next].sample < pos + kFrames) {
            midi::MidiEvent e{};
            e.type = events[next].type;
            e.data1 = events[next].data1;
            e.data2 = events[next].data2;
            engine.handleMidiEvent(e);
            ++next;
        }
        engine.renderBlock(buffer, kFrames);
        for (size_t k = 0; k < kFrames * 2; ++k) result.pcm.push_back(buffer[k]);
    }
#if POCKETPAN_SUSTAIN_FASTPATH
    result.fastPathBlocks = engine.getSustainFastPathBlocksForTest();
#endif
    return result;
}

void compare(const char* name, bool fast, InstrumentModel model,
             const std::vector<Event>& events, uint32_t totalFrames,
             uint32_t switchAt = 0xFFFFFFFFu, InstrumentModel switchTo = InstrumentModel::Pan) {
    const Render a = render(true, model, events, totalFrames, switchAt, switchTo);
    const Render b = render(false, model, events, totalFrames, switchAt, switchTo);
    assert(a.pcm.size() == b.pcm.size());
    for (size_t i = 0; i < a.pcm.size(); ++i) {
        if (a.pcm[i] != b.pcm[i]) {
            std::fprintf(stderr, "sustain fast path PCM differs: %s at %zu (%d vs %d)\n",
                         name, i, a.pcm[i], b.pcm[i]);
            assert(false);
        }
    }
    bool nonzero = false;
    for (int32_t v : a.pcm) { if (v != 0) { nonzero = true; break; } }
    assert(nonzero && "fixture must produce audio");
#if POCKETPAN_SUSTAIN_FASTPATH
    if (fast) assert(a.fastPathBlocks > 0 && "fast path must actually be exercised");
    std::fprintf(stderr, "fastpath %-22s exact (%u fast voice-blocks)\n", name, a.fastPathBlocks);
#else
    (void)fast;
    std::fprintf(stderr, "fastpath %-22s exact (candidate has no fast path)\n", name);
#endif
}

std::vector<Event> chord(uint32_t at, const uint8_t* notes, size_t count, uint8_t vel) {
    std::vector<Event> events;
    for (size_t i = 0; i < count; ++i)
        events.push_back({at, midi::MidiEventType::NoteOn, notes[i], vel});
    return events;
}

} // namespace

int main() {
#if POCKETPAN_SUSTAIN_FASTPATH
    const uint32_t total = 96000; // 2 s
    const uint8_t single[] = {62};
    const uint8_t four[] = {50, 57, 62, 69};
    const uint8_t cluster[] = {50, 52, 54, 56, 57, 59, 61, 62};
    const uint8_t nine[] = {50, 52, 54, 56, 57, 59, 61, 62, 64};

    compare("PAN single", true, InstrumentModel::Pan, chord(0, single, 1, 90), total);
    compare("PAN chord4", true, InstrumentModel::Pan, chord(0, four, 4, 90), total);
    compare("PAN cluster8", true, InstrumentModel::Pan, chord(0, cluster, 8, 100), total);
    compare("PAN steal9", true, InstrumentModel::Pan, chord(0, nine, 9, 100), total);
    compare("BELL single", true, InstrumentModel::Bell, chord(0, single, 1, 90), total);
    compare("BELL chord4", true, InstrumentModel::Bell, chord(0, four, 4, 90), total);
    compare("BELL cluster8", true, InstrumentModel::Bell, chord(0, cluster, 8, 100), total);
    compare("TONGUE single", true, InstrumentModel::Tongue, chord(0, single, 1, 90), total);
    compare("TONGUE chord4", true, InstrumentModel::Tongue, chord(0, four, 4, 90), total);
    compare("TONGUE cluster8", true, InstrumentModel::Tongue, chord(0, cluster, 8, 100), total);

    {
        auto events = chord(0, four, 4, 90);
        events.push_back({48000, midi::MidiEventType::ChannelPressure, 70, 0});
        events.push_back({64000, midi::MidiEventType::NoteOff, 50, 0});
        events.push_back({72000, midi::MidiEventType::NoteOn, 50, 100});
        compare("PAN pressure/off/restrike", true, InstrumentModel::Pan, events, total);
    }
    {
        auto events = chord(0, four, 4, 90);
        events.push_back({48000, midi::MidiEventType::PolyPressure, 62, 90});
        compare("BELL polypressure", true, InstrumentModel::Bell, events, total);
    }
    {
        auto events = chord(0, four, 4, 90);
        events.push_back({48000, midi::MidiEventType::PolyPressure, 62, 90});
        compare("TONGUE polypressure", true, InstrumentModel::Tongue, events, total);
    }
    {
        // A model switch resets every voice, so strike again after the switch
        // to exercise the sustain fast path on the new model too.
        auto events = chord(0, four, 4, 90);
        for (auto e : chord(49152, four, 4, 90)) events.push_back(e);
        compare("PAN to BELL switch", true, InstrumentModel::Pan, events,
                total, 48000, InstrumentModel::Bell);
    }
    {
        auto events = chord(0, cluster, 8, 100);
        for (auto e : chord(49152, cluster, 8, 100)) events.push_back(e);
        compare("BELL to PAN switch", true, InstrumentModel::Bell, events,
                total, 48000, InstrumentModel::Pan);
    }
    {
        auto events = chord(0, four, 4, 90);
        for (auto e : chord(49152, four, 4, 90)) events.push_back(e);
        compare("BELL to TONGUE switch", true, InstrumentModel::Bell, events,
                total, 48000, InstrumentModel::Tongue);
    }
    {
        auto events = chord(0, cluster, 8, 100);
        for (auto e : chord(49152, cluster, 8, 100)) events.push_back(e);
        compare("TONGUE to PAN switch", true, InstrumentModel::Tongue, events,
                total, 48000, InstrumentModel::Pan);
    }
#else
    std::fprintf(stderr, "fastpath: candidate %d has no sustain fast path; skipped\n",
                 POCKETPAN_DSP_CANDIDATE);
#endif
    return 0;
}
