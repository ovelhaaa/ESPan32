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
static_assert(CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ == 240, "Forensics cycle conversion requires 240 MHz");
// M6.3.6 (§10): configurable histogram resolution.  Legacy captures used 25 us
// bins; the fine mode uses 5 us (or 10 us) so the 1650-1800 us region around the
// gate can be resolved.  The bin count stays fixed, so only diagnostic builds
// pay for the finer resolution.
#ifndef POCKETPAN_FORENSICS_FINE_BINS
#define POCKETPAN_FORENSICS_FINE_BINS 0
#endif
constexpr uint32_t kForensicsBinUs = POCKETPAN_FORENSICS_FINE_BINS > 0
    ? static_cast<uint32_t>(POCKETPAN_FORENSICS_FINE_BINS) : 25u;
// Cover the 2666.7 us physical deadline plus margin.  Legacy 25 us keeps its
// historical 512-bin layout so old captures remain byte-comparable.
constexpr size_t kForensicsBins = POCKETPAN_FORENSICS_FINE_BINS > 0
    ? (3200u / kForensicsBinUs + 1u) : 512u;
// Diagnostic fixture owns synthesis while BLE, UI and I2S remain running.
// Each 22 s fixture aggregates 0.5 s ringing segments. No note-offs/restrikes
// in sustain segments. Counts are checked on every measured block.
struct Timing {
    uint64_t sum = 0;
    uint32_t count = 0, maximum = 0, deadline = 0;
    std::array<uint32_t, kForensicsBins> bins{};
    void add(uint32_t cycles) {
        const uint32_t us = (cycles + 239) / 240;
        sum += cycles; ++count; maximum = std::max(maximum, us);
        if (cycles >= 640000) ++deadline;
        ++bins[std::min<size_t>(us / kForensicsBinUs, bins.size() - 1)];
    }
    // Nearest-rank quantile, num/den (e.g. 99/100, 995/1000).  Returns the upper
    // edge of the containing bin in microseconds.
    uint32_t quantile(uint32_t num, uint32_t den) const {
        if (!count) return 0;
        const uint32_t rank = (count * num + den - 1) / den;
        uint32_t n = 0;
        for (size_t i = 0; i < bins.size(); ++i) {
            n += bins[i];
            if (n >= rank) return static_cast<uint32_t>(i + 1) * kForensicsBinUs;
        }
        return 0;
    }
    uint32_t p95() const { return quantile(95, 100); }
    uint32_t p99() const { return quantile(99, 100); }
    uint32_t p995() const { return quantile(995, 1000); }
    uint32_t p999() const { return quantile(999, 1000); }
};
struct Result {
    Timing steady, event;
    uint32_t badVoices = 0, bleLost = 0, hardClamp = 0, modalSat = 0;
    // Diagnostic counters for the M6.3.5 fast paths.  Read from the allocator at
    // fixture completion only; never touched by DSP processing.
    uint32_t stable8 = 0, attack = 0;
#ifdef CONFIG_POCKETPAN_DSP_PROFILE
    // Keep the sparse steady samples and every NoteOn block separate.  Event
    // blocks include deliberate trigger work, so blending them into the
    // steady average hides precisely the cost this fixture is meant to find.
    struct Profile {
        uint64_t phases[dsp::profile::Count]{};
        uint64_t total = 0;
        uint32_t count = 0;
    } steadyProfile, eventProfile;
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
    if (event && !roll) {
        results[id].hardClamp += synth.getHardClampCount();
        synth.reset(); // Reset cost deliberately excluded from event timing.
    }
#ifdef CONFIG_POCKETPAN_DSP_PROFILE
    // Sample steady render blocks sparsely to limit probe perturbation, but
    // profile every event block: normally none of the 188-block event
    // cadence coincides with the old modulo-32 sampling point.
    const bool profiled = event || block % 32 == 1;
    dsp::profile::enabled = profiled;
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
    if (profiled) {
        auto& profile = event ? r.eventProfile : r.steadyProfile;
        profile.total += elapsed;
        for (unsigned i = 0; i < dsp::profile::Count; ++i) profile.phases[i] += dsp::profile::cycles[i];
        ++profile.count;
    }
    dsp::profile::enabled = false;
#endif
    if (++block == CONFIG_POCKETPAN_FORENSICS_BLOCKS) {
        r.hardClamp += synth.getHardClampCount(); r.modalSat = synth.getModalInternalSaturationCount();
#if POCKETPAN_PAN_STABLE8_FASTPATH
        r.stable8 = synth.getVoiceAllocator().getPanStable8BlocksForTest();
#endif
#if POCKETPAN_ATTACK_VOICE_FASTPATH
        r.attack = synth.getVoiceAllocator().getAttackFastPathBlocksForTest();
#endif
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
            ESP_LOGI("forensics", "[CURVE] model=%s fixture=%u voices=%u class=%s n=%u avg_us=%.2f p95_us=%u p99_us=%u p995_us=%u p999_us=%u max_us=%u deadline=%u bad_voices=%u ble_lost=%u hard=%u sat=%u bin_us=%u",
                isPan(id) ? "PAN" : "BELL", id, counts[kindOf(id)], e ? "event" : "steady", (unsigned)t.count,
                t.count ? double(t.sum) / t.count / 240 : 0, (unsigned)t.p95(), (unsigned)t.p99(),
                (unsigned)t.p995(), (unsigned)t.p999(), (unsigned)t.maximum, (unsigned)t.deadline,
                (unsigned)r.badVoices, (unsigned)r.bleLost, (unsigned)r.hardClamp, (unsigned)r.modalSat,
                (unsigned)kForensicsBinUs);
            for (unsigned i = 0; i < t.bins.size(); ++i) if (t.bins[i])
                ESP_LOGI("forensics", "[HIST] fixture=%u class=%s lower_us=%u n=%u", id,
                    e ? "event" : "steady", (unsigned)(i * kForensicsBinUs), (unsigned)t.bins[i]);
        }
        {
#if POCKETPAN_PAN_STABLE8_FASTPATH
            const unsigned stable8 = (unsigned)r.stable8;
#else
            const unsigned stable8 = 0;
#endif
#if POCKETPAN_ATTACK_VOICE_FASTPATH
            const unsigned attack = (unsigned)r.attack;
#else
            const unsigned attack = 0;
#endif
            ESP_LOGI("forensics", "[FASTPATH] fixture=%u stable8=%u attack=%u",
                     id, stable8, attack);
        }
#ifdef CONFIG_POCKETPAN_DSP_PROFILE
        for (unsigned e = 0; e < 2; ++e) {
            const auto& profile = e ? r.eventProfile : r.steadyProfile;
            const char* profileClass = e ? "event" : "steady";
            ESP_LOGI("forensics", "[PROFILE_TOTAL] fixture=%u class=%s cycles_per_block=%.2f n=%u", id,
                profileClass, profile.count ? double(profile.total) / profile.count : 0, (unsigned)profile.count);
            // Count deliberately follows dsp::profile::Phase, including the
            // M6.3.2 trigger-path phases appended after the M6.3.1 values.
            for (unsigned i = 0; i < dsp::profile::Count; ++i)
                ESP_LOGI("forensics", "[PHASE] fixture=%u class=%s phase=%u cycles_per_block=%.2f n=%u", id,
                    profileClass, i, profile.count ? double(profile.phases[i]) / profile.count : 0,
                    (unsigned)profile.count);
        }
#endif
        ++logged;
    }
}
} // namespace pocketpan::forensics
#endif
