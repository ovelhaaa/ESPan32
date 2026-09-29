// M6.3.5 differential harness.
//
// Validates the three M6.3.5 optimization families against the general path on
// independent engine instances:
//   * Phase A1 sympathetic low-pass coefficient cache  -> bit-exact coefficient
//   * Phase A2 PAN stable-8 sample-outer sustain path   -> PCM + voice-state exact
//   * Phase B  attack-voice fast path                   -> PCM + voice-state exact
//
// PAN stable-8 exactness is also exercised by test_fastpath (sustain toggle);
// this harness adds the explicit attack toggle and the coefficient-bit check.
#include "dsp/synth_engine.h"
#include "dsp/dsp_profile.h"
#include "midi/midi_event.h"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

using namespace pocketpan;
using namespace pocketpan::dsp;

namespace {

constexpr size_t kFrames = 128;

uint32_t bits(float f) {
    uint32_t u;
    std::memcpy(&u, &f, sizeof(u));
    return u;
}

struct Event {
    uint32_t sample;
    midi::MidiEventType type;
    uint8_t data1;
    uint8_t data2;
};

struct Render {
    std::vector<int32_t> pcm;
    std::vector<uint32_t> voiceState; // energy/lastSample bits, age, active per voice
    uint32_t attackBlocks = 0;
    uint32_t panStable8Blocks = 0;
};

Render render(bool attack, InstrumentModel model, const std::vector<Event>& events,
              uint32_t totalFrames) {
    SynthEngine engine;
    engine.init(48000.0f);
    engine.setInstrumentModel(model);
#if POCKETPAN_ATTACK_VOICE_FASTPATH
    engine.setAttackFastPathEnabledForTest(attack);
#else
    (void)attack;
#endif
    Render result;
    result.pcm.reserve(totalFrames * 2);
    int32_t buffer[kFrames * 2];
    size_t next = 0;
    for (uint32_t pos = 0; pos < totalFrames; pos += kFrames) {
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
#if POCKETPAN_ATTACK_VOICE_FASTPATH
    result.attackBlocks = engine.getAttackFastPathBlocksForTest();
#endif
#if POCKETPAN_PAN_STABLE8_FASTPATH
    result.panStable8Blocks = engine.getPanStable8BlocksForTest();
#endif
    for (size_t v = 0; v < kMaxVoices; ++v) {
        const auto& voice = engine.getVoiceAllocator().getVoice(v);
        result.voiceState.push_back(bits(voice.getEstimatedEnergy()));
        result.voiceState.push_back(bits(voice.getLastSample()));
        result.voiceState.push_back(voice.getAge());
        result.voiceState.push_back(voice.isActive() ? 1u : 0u);
    }
    return result;
}

// requireExercise selects fixtures where the attack specialization is expected
// to be selected (all-8 PAN, or any voice-major Bell).  Single/chord PAN run
// through the general path and are exactness-only.
void compare(const char* name, InstrumentModel model, const std::vector<Event>& events,
             uint32_t totalFrames, bool requireExercise) {
#if POCKETPAN_ATTACK_VOICE_FASTPATH
    const Render a = render(true, model, events, totalFrames);
    const Render b = render(false, model, events, totalFrames);
    assert(a.pcm.size() == b.pcm.size());
    for (size_t i = 0; i < a.pcm.size(); ++i) {
        if (a.pcm[i] != b.pcm[i]) {
            std::fprintf(stderr, "m635 attack PCM differs: %s at %zu (%d vs %d)\n",
                         name, i, a.pcm[i], b.pcm[i]);
            assert(false);
        }
    }
    assert(a.voiceState == b.voiceState && "attack fast path voice state must be exact");
    bool nonzero = false;
    for (int32_t v : a.pcm) { if (v != 0) { nonzero = true; break; } }
    assert(nonzero && "fixture must produce audio");
    if (requireExercise) assert(a.attackBlocks > 0 && "attack fast path must be exercised");
#if POCKETPAN_PAN_STABLE8_FASTPATH
    if (requireExercise && model == InstrumentModel::Pan) {
        assert(a.panStable8Blocks > 0 && "PAN stable-8 sustain path must be exercised");
    }
#endif
    std::fprintf(stderr, "m635 attack %-26s exact (%u attack-blocks, %u stable8-blocks)\n",
                 name, a.attackBlocks, a.panStable8Blocks);
#else
    (void)name; (void)model; (void)events; (void)totalFrames; (void)requireExercise;
#endif
}

std::vector<Event> chord(uint32_t at, const uint8_t* notes, size_t count, uint8_t vel) {
    std::vector<Event> events;
    for (size_t i = 0; i < count; ++i)
        events.push_back({at, midi::MidiEventType::NoteOn, notes[i], vel});
    return events;
}

#if POCKETPAN_HEADROOM_TABLE
// M6.3.6 (§28): the nine-entry headroom table must be the bit-identical value of
// the historical per-block expression for every integer voice count 0..8.
void headroomTableExactness() {
    SynthEngine engine;
    engine.init(48000.0f);
    for (size_t v = 0; v <= kMaxVoices; ++v) {
        const float voices = static_cast<float>(v);
        const float targetDb = voices <= 1.0f ? 0.0f : -1.5f * std::log2(voices);
        const float reference = std::pow(10.0f, std::max(targetDb, -5.0f) / 20.0f);
        if (bits(engine.headroomTargetForTest(v)) != bits(reference)) {
            std::fprintf(stderr, "m636 headroom table mismatch voices=%zu\n", v);
            assert(false);
        }
    }
    std::fprintf(stderr, "m636 headroom table: bit-exact (9 entries)\n");
}
#endif

#if POCKETPAN_SYMPATHETIC_COEFF_CACHE
void coefficientExactness() {
    const float rates[] = {44100.0f, 48000.0f, 96000.0f};
    const float lowpasses[] = {0.0f, 10.0f, 500.0f, 1500.0f, 5000.0f, 20000.0f, 100000.0f};
    for (float fs : rates) {
        VoiceAllocator allocator;
        allocator.init(fs);
        for (float lp : lowpasses) {
            SympatheticConfig config{};
            config.enabled = true;
            config.lowpassHz = lp;
            allocator.setSympatheticConfig(config);
            const float cutoff = std::clamp(lp, 10.0f, fs * 0.45f);
            const float reference = std::exp(-2.0f * 3.14159265358979323846f * cutoff / fs);
            if (bits(allocator.getSympatheticLowpassCoefficientForTest()) != bits(reference)) {
                std::fprintf(stderr, "m635 sympathetic coefficient mismatch fs=%.0f lp=%.0f\n", fs, lp);
                assert(false);
            }
        }
    }
    std::fprintf(stderr, "m635 sympathetic coefficient cache: bit-exact (21 configs)\n");
}
#endif

} // namespace

int main() {
#if POCKETPAN_HEADROOM_TABLE
    headroomTableExactness();
#endif
#if POCKETPAN_SYMPATHETIC_COEFF_CACHE
    coefficientExactness();
#endif
    // Attack path is gated by the candidate macro; without it the fixture is
    // still rendered once to guarantee the harness compiles and links.
#if POCKETPAN_ATTACK_VOICE_FASTPATH
    const uint32_t total = 96000; // 2 s
    const uint8_t single[] = {62};
    const uint8_t four[] = {50, 57, 62, 69};
    const uint8_t cluster[] = {50, 52, 54, 56, 57, 59, 61, 62};
    const uint8_t nine[] = {50, 52, 54, 56, 57, 59, 61, 62, 64};
    const uint8_t velocities[] = {30, 70, 110, 127};

    for (uint8_t vel : velocities) {
        const float normalized = static_cast<float>(vel) / 127.0f;
        (void)normalized;
        compare("PAN single", InstrumentModel::Pan, chord(0, single, 1, vel), total, false);
        compare("PAN chord4", InstrumentModel::Pan, chord(0, four, 4, vel), total, false);
        compare("PAN cluster8", InstrumentModel::Pan, chord(0, cluster, 8, vel), total, true);
        compare("BELL single", InstrumentModel::Bell, chord(0, single, 1, vel), total, true);
        compare("BELL chord4", InstrumentModel::Bell, chord(0, four, 4, vel), total, true);
        compare("BELL cluster8", InstrumentModel::Bell, chord(0, cluster, 8, vel), total, true);
        compare("TONGUE single", InstrumentModel::Tongue, chord(0, single, 1, vel), total, true);
        compare("TONGUE chord4", InstrumentModel::Tongue, chord(0, four, 4, vel), total, true);
        compare("TONGUE cluster8", InstrumentModel::Tongue, chord(0, cluster, 8, vel), total, true);
    }

    compare("PAN steal9", InstrumentModel::Pan, chord(0, nine, 9, 100), total, false);
    {
        auto events = chord(0, cluster, 8, 100);
        events.push_back({48000, midi::MidiEventType::ChannelPressure, 70, 0});
        compare("PAN pressure/cluster8", InstrumentModel::Pan, events, total, false);
    }
    {
        auto events = chord(0, cluster, 8, 100);
        events.push_back({48000, midi::MidiEventType::NoteOff, 50, 0});
        events.push_back({72000, midi::MidiEventType::NoteOn, 50, 100});
        compare("PAN noteoff/restrike", InstrumentModel::Pan, events, total, false);
    }
#else
    SynthEngine engine;
    engine.init(48000.0f);
    int32_t buffer[kFrames * 2]{};
    engine.renderBlock(buffer, kFrames);
    std::fprintf(stderr, "m635 attack fast path: candidate %d has none; skipped\n",
                 POCKETPAN_DSP_CANDIDATE);
#endif
    return 0;
}
