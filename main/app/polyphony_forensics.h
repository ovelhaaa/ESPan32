#pragma once
#include "sdkconfig.h"
#ifdef CONFIG_POCKETPAN_POLYPHONY_FORENSICS
#include "dsp/synth_engine.h"
#include "dsp/dsp_profile.h"
#include "esp_cpu.h"
#include "esp_log.h"
#include <array>
#include <atomic>

namespace pocketpan::forensics {
// Diagnostic fixture owns synthesis while BLE, UI and I2S remain running.
// Each 22 s fixture aggregates 0.5 s ringing segments. No note-offs/restrikes
// in sustain segments. Counts are checked on every measured block.
struct Timing {
    uint64_t sum = 0;
    uint32_t count = 0, maximum = 0, deadline = 0;
    std::array<uint32_t, 512> bins{};
    void add(uint32_t cycles) {
        const uint32_t us = (cycles + 239) / 240;
        sum += cycles; ++count; maximum = std::max(maximum, us);
        if (cycles >= 640000) ++deadline;
        ++bins[std::min<size_t>(us / 25, bins.size() - 1)];
    }
    uint32_t p99() const {
        uint32_t n = 0;
        for (size_t i = 0; i < bins.size(); ++i) {
            n += bins[i];
            if (n >= (count * 99 + 99) / 100) return (i + 1) * 25;
        }
        return 0;
    }
};
struct Result {
    Timing steady, event;
    uint32_t badVoices = 0, bleLost = 0, hardClamp = 0, modalSat = 0;
#ifdef CONFIG_POCKETPAN_DSP_PROFILE
    uint64_t phases[dsp::profile::Count]{};
    uint64_t profileTotal = 0;
    uint32_t profiled = 0;
#endif
};
inline Result results[19]{};
inline std::atomic<unsigned> completed{0};
inline std::atomic<bool> ready{false};
inline unsigned fixture = 0, block = 0;
#ifdef CONFIG_POCKETPAN_DSP_PROFILE
inline constexpr unsigned fixtureCount = POCKETPAN_FORENSICS_CRITICAL_ONLY ? 3 : 19;
#else
inline constexpr unsigned fixtureCount = POCKETPAN_FORENSICS_CRITICAL_ONLY ? 3 : 16;
#endif
inline unsigned kindOf(unsigned id) { return id >= 16 ? 5 : id % 8; }
inline bool isPan(unsigned id) { return id < 8 || id == 16; }
inline unsigned fixtureId(unsigned index) {
    constexpr unsigned critical[] = {5, 14, 13};
    return POCKETPAN_FORENSICS_CRITICAL_ONLY ? critical[index] : index;
}
inline constexpr unsigned counts[8] = {0, 1, 2, 4, 6, 8, 4, 1};
inline constexpr uint8_t cluster[8] = {50, 52, 54, 56, 57, 59, 61, 62};
inline constexpr uint8_t chord[4] = {50, 57, 62, 69};
inline void render(dsp::SynthEngine& synth, int32_t* output, size_t frames) {
    if (fixture == fixtureCount || (!block && !ready.load(std::memory_order_acquire))) {
        std::fill(output, output + frames * 2, 0); return;
    }
    const unsigned id = fixtureId(fixture);
    const unsigned kind = kindOf(id);
    const bool roll = kind == 7;
    const bool event = roll ? (block % 38 == 0) : (block % 188 == 0);
    if (block == 0) {
        synth.setInstrumentModel(isPan(id) ? dsp::InstrumentModel::Pan : dsp::InstrumentModel::Bell);
#ifdef CONFIG_POCKETPAN_DSP_PROFILE
        if (id == 16) synth.setBodyEnabled(false); // Diagnostic PAN isolation only.
        if (id >= 17) {
            static dsp::ModalPreset preset;
            preset = dsp::kPresetBell;
            auto config = dsp::getInstrumentModelConfig(dsp::InstrumentModel::Bell);
            preset.modeCount = 9; // Measurement only: omit upper 5.2f.
            if (id == 18) {
                for (unsigned i = 6; i < 8; ++i) {
                    preset.modes[i] = preset.modes[i + 1];
                    config.voicing.softModeCoupling[i] = config.voicing.softModeCoupling[i + 1];
                    config.voicing.hardModeCoupling[i] = config.voicing.hardModeCoupling[i + 1];
                }
                preset.modeCount = 8; // Also omit nominal doublet, keeping order.
            }
            config.modalPreset = &preset;
            synth.setModelConfigForTest(config);
        }
#endif
    }
    if (event && !roll) synth.reset(); // Reset cost deliberately excluded from event timing.
#ifdef CONFIG_POCKETPAN_DSP_PROFILE
    dsp::profile::enabled = block % 32 == 1;
    std::fill(std::begin(dsp::profile::cycles), std::end(dsp::profile::cycles), 0);
#endif
    const uint32_t start = esp_cpu_get_cycle_count();
    if (event) {
        for (unsigned v = 0; v < counts[kind]; ++v) {
            midi::MidiEvent note{}; note.type = midi::MidiEventType::NoteOn;
            note.data1 = kind == 6 ? chord[v] : counts[kind] == 1 ? 62 : cluster[v];
            note.data2 = kind == 6 || counts[kind] == 1 ? 90 : 100;
            synth.handleMidiEvent(note);
        }
    }
    synth.renderBlock(output, frames);
    const uint32_t elapsed = esp_cpu_get_cycle_count() - start;
    auto& r = results[id];
    (event ? r.event : r.steady).add(elapsed);
    if (synth.getVoiceAllocator().getActiveVoiceCount() != counts[kind]) ++r.badVoices;
    if (!ready.load(std::memory_order_acquire)) ++r.bleLost;
#ifdef CONFIG_POCKETPAN_DSP_PROFILE
    if (dsp::profile::enabled) {
        r.profileTotal += elapsed;
        for (unsigned i = 0; i < dsp::profile::Count; ++i) r.phases[i] += dsp::profile::cycles[i];
        ++r.profiled;
    }
    dsp::profile::enabled = false;
#endif
    if (++block == CONFIG_POCKETPAN_FORENSICS_BLOCKS) {
        r.hardClamp = synth.getHardClampCount(); r.modalSat = synth.getModalInternalSaturationCount();
        completed.store(++fixture, std::memory_order_release); block = 0;
    }
}
inline void logCompleted() {
    static unsigned logged = 0;
    const unsigned done = completed.load(std::memory_order_acquire);
    while (logged < done) {
        const unsigned id = fixtureId(logged);
        const auto& r = results[id];
        for (unsigned e = 0; e < 2; ++e) {
            const auto& t = e ? r.event : r.steady;
            ESP_LOGI("forensics", "[CURVE] model=%s fixture=%u voices=%u class=%s n=%u avg_us=%.2f p99_us=%u max_us=%u deadline=%u bad_voices=%u ble_lost=%u hard=%u sat=%u",
                isPan(id) ? "PAN" : "BELL", id, counts[kindOf(id)], e ? "event" : "steady", (unsigned)t.count,
                t.count ? double(t.sum) / t.count / 240 : 0, (unsigned)t.p99(), (unsigned)t.maximum, (unsigned)t.deadline,
                (unsigned)r.badVoices, (unsigned)r.bleLost, (unsigned)r.hardClamp, (unsigned)r.modalSat);
            for (unsigned i = 0; i < t.bins.size(); ++i) if (t.bins[i])
                ESP_LOGI("forensics", "[HIST] fixture=%u class=%s lower_us=%u n=%u", id,
                    e ? "event" : "steady", i * 25, (unsigned)t.bins[i]);
        }
#ifdef CONFIG_POCKETPAN_DSP_PROFILE
        ESP_LOGI("forensics", "[PROFILE_TOTAL] fixture=%u cycles_per_block=%.2f n=%u", id,
            r.profiled ? double(r.profileTotal) / r.profiled : 0, (unsigned)r.profiled);
        for (unsigned i = 0; i < dsp::profile::Count; ++i)
            ESP_LOGI("forensics", "[PHASE] fixture=%u phase=%u cycles_per_block=%.2f n=%u", id, i,
                r.profiled ? double(r.phases[i]) / r.profiled : 0, (unsigned)r.profiled);
#endif
        ++logged;
    }
}
} // namespace pocketpan::forensics
#endif
