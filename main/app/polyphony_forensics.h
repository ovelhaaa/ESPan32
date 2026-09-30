#pragma once
#include "sdkconfig.h"
#ifdef CONFIG_POCKETPAN_POLYPHONY_FORENSICS
#include "dsp/synth_engine.h"
#include "dsp/dsp_profile.h"
#include "hardware/audio_i2s.h"
#include "esp_cpu.h"
#include "esp_log.h"
#include "esp_timer.h"
#include <array>
#include <atomic>
#include <algorithm>

namespace pocketpan::forensics {
inline hardware::AudioI2S* sAudioInstance = nullptr;
inline void setAudioInstance(hardware::AudioI2S* audio) {
    sAudioInstance = audio;
}
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

// M6.3.9.1 Phase A: lock-free publication of current block class
enum class ForensicsBlockClass : uint8_t {
    Idle = 0,
    FixtureTransition = 1,
    Event = 2,
    AttackTail = 3,
    TrueSteady = 4
};

inline std::atomic<ForensicsBlockClass> sCurrentBlockClass{ForensicsBlockClass::Idle};
inline ForensicsBlockClass getCurrentBlockClass() {
    return sCurrentBlockClass.load(std::memory_order_relaxed);
}

inline constexpr unsigned kClassCount = 4;
// Class 0: true_steady
// Class 1: attack_tail
// Class 2: event
// Class 3: fixture_transition
inline constexpr const char* kForensicsClassNames[kClassCount] = {
    "true_steady",
    "attack_tail",
    "event",
    "fixture_transition"
};

inline unsigned classToIndex(ForensicsBlockClass cls) {
    switch (cls) {
        case ForensicsBlockClass::TrueSteady: return 0;
        case ForensicsBlockClass::AttackTail: return 1;
        case ForensicsBlockClass::Event: return 2;
        case ForensicsBlockClass::FixtureTransition: return 3;
        default: return 0;
    }
}

inline const char* classToName(ForensicsBlockClass cls) {
    switch (cls) {
        case ForensicsBlockClass::TrueSteady: return "true_steady";
        case ForensicsBlockClass::AttackTail: return "attack_tail";
        case ForensicsBlockClass::Event: return "event";
        case ForensicsBlockClass::FixtureTransition: return "fixture_transition";
        case ForensicsBlockClass::Idle: return "idle";
        default: return "unknown";
    }
}

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
    void addUs(uint32_t us) {
        const uint64_t cycles = static_cast<uint64_t>(us) * 240;
        sum += cycles; ++count; maximum = std::max(maximum, us);
        if (us >= 2667) ++deadline;
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
    std::array<Timing, kClassCount> innerTiming{};
    std::array<Timing, kClassCount> callbackTiming{};
    uint32_t badVoices = 0, bleLost = 0, hardClamp = 0, modalSat = 0;
    // Diagnostic counters for the M6.3.5 fast paths.  Read from the allocator at
    // fixture completion only; never touched by DSP processing.
    uint32_t stable8 = 0, attack = 0;

    Timing& timing(unsigned c) { return innerTiming[c]; }
    const Timing& timing(unsigned c) const { return innerTiming[c]; }
    Timing& callback(unsigned c) { return callbackTiming[c]; }
    const Timing& callback(unsigned c) const { return callbackTiming[c]; }

#ifdef CONFIG_POCKETPAN_DSP_PROFILE
    struct Profile {
        uint64_t phases[dsp::profile::Count]{};
        uint64_t total = 0;
        uint32_t count = 0;
    } profileData[kClassCount];
    Profile& profile(unsigned c) { return profileData[c]; }
    const Profile& profile(unsigned c) const { return profileData[c]; }
#endif
};

// M6.3.9.1 Phase B: capture any callback >= 2667 us in a preallocated ring buffer
struct OverrunRecord {
    uint32_t blockSequence;
    uint32_t fullCallbackUs;
    uint32_t internalRenderUs;
    uint8_t blockClass;
    uint8_t fixtureId;
    uint8_t model;
    uint8_t activeVoices;
    bool exciterActive;
    uint8_t midiQueueDepth;
    uint8_t midiEventsConsumed;
    bool telemetryPublished;
    bool modelChangeRequested;
    bool synthResetRequested;
};

class OverrunRingBuffer {
public:
    static constexpr size_t kCapacity = 32;

    void record(const OverrunRecord& rec) {
        const size_t idx = count_.fetch_add(1, std::memory_order_relaxed) % kCapacity;
        records_[idx] = rec;
    }

    size_t count() const {
        return count_.load(std::memory_order_relaxed);
    }

    bool get(size_t index, OverrunRecord& out) const {
        const size_t total = count_.load(std::memory_order_relaxed);
        if (index >= total || (total > kCapacity && index < total - kCapacity)) {
            return false;
        }
        out = records_[index % kCapacity];
        return true;
    }

    void reset() {
        count_.store(0, std::memory_order_relaxed);
    }

private:
    std::atomic<size_t> count_{0};
    std::array<OverrunRecord, kCapacity> records_{};
};

inline OverrunRingBuffer gOverrunBuffer;

// M6.3.9.1 Phase C: callback overhead breakdown
struct CallbackOverheadStats {
    uint64_t modelResetSumUs = 0;
    uint64_t queueConsumeSumUs = 0;
    uint64_t renderSumUs = 0;
    uint64_t telemetrySumUs = 0;
    uint32_t count = 0;
    uint32_t telemPublishCount = 0;
    uint32_t telemMaxUs = 0;
    uint32_t queueMaxUs = 0;
    uint32_t modelResetMaxUs = 0;

    void add(uint32_t mrUs, uint32_t qcUs, uint32_t rUs, uint32_t telUs, bool published) {
        modelResetSumUs += mrUs;
        queueConsumeSumUs += qcUs;
        renderSumUs += rUs;
        telemetrySumUs += telUs;
        modelResetMaxUs = std::max(modelResetMaxUs, mrUs);
        queueMaxUs = std::max(queueMaxUs, qcUs);
        telemMaxUs = std::max(telemMaxUs, telUs);
        count++;
        if (published) telemPublishCount++;
    }
};

struct CallbackBlockContext {
    bool modelChangeRequested = false;
    bool synthResetRequested = false;
    uint8_t midiQueueDepth = 0;
    uint8_t midiEventsConsumed = 0;
    bool telemetryPublished = false;
    uint32_t internalRenderUs = 0;
    ForensicsBlockClass blockClass = ForensicsBlockClass::Idle;
    uint8_t fixtureId = 0;
    uint8_t model = 0;
    uint8_t activeVoices = 0;
    bool exciterActive = false;
};
inline CallbackBlockContext sCurrentBlockContext;

inline std::atomic<unsigned> completed{0};
inline std::atomic<bool> ready{false};
inline unsigned fixture = 0, block = 0;
#if defined(POCKETPAN_FORENSICS_M7) && POCKETPAN_FORENSICS_M7
inline constexpr unsigned fixtureCount = 4;
#elif defined(CONFIG_POCKETPAN_DSP_PROFILE)
inline constexpr unsigned fixtureCount = POCKETPAN_FORENSICS_CRITICAL_ONLY ? 3 : 19;
#else
inline constexpr unsigned fixtureCount = POCKETPAN_FORENSICS_CRITICAL_ONLY ? 3 : 16;
#endif

inline Result results[fixtureCount]{};
inline CallbackOverheadStats gCallbackOverhead[fixtureCount]{};

inline dsp::InstrumentModel modelOf(unsigned id) {
#if defined(POCKETPAN_FORENSICS_M7) && POCKETPAN_FORENSICS_M7
    switch (id) {
        case 0: return dsp::InstrumentModel::Pan;
        case 1: return dsp::InstrumentModel::Bowl;
        case 2: return dsp::InstrumentModel::Kalimba;
        case 3: return dsp::InstrumentModel::Kalimba;
        default: return dsp::InstrumentModel::Pan;
    }
#else
    return (id < 8 || id == 16) ? dsp::InstrumentModel::Pan : dsp::InstrumentModel::Bell;
#endif
}

inline unsigned kindOf(unsigned id) {
#if defined(POCKETPAN_FORENSICS_M7) && POCKETPAN_FORENSICS_M7
    switch (id) {
        case 0: return 5; // cluster8
        case 1: return 5; // cluster8
        case 2: return 6; // chord4
        case 3: return 5; // cluster8
        default: return 5;
    }
#else
    return id >= 16 ? 5 : id % 8;
#endif
}

inline bool isPan(unsigned id) { return modelOf(id) == dsp::InstrumentModel::Pan; }

inline unsigned fixtureId(unsigned index) {
#if defined(POCKETPAN_FORENSICS_M7) && POCKETPAN_FORENSICS_M7
    return index;
#else
    constexpr unsigned critical[] = {5, 14, 13};
    return POCKETPAN_FORENSICS_CRITICAL_ONLY ? critical[index] : index;
#endif
}
inline constexpr unsigned counts[8] = {0, 1, 2, 4, 6, 8, 4, 1};
inline constexpr uint8_t cluster[8] = {50, 52, 54, 56, 57, 59, 61, 62};
inline constexpr uint8_t chord[4] = {50, 57, 62, 69};

inline void setCallbackPreContext(bool modelChange, bool synthReset, uint8_t depth, uint8_t consumed) {
    sCurrentBlockContext.modelChangeRequested = modelChange;
    sCurrentBlockContext.synthResetRequested = synthReset;
    sCurrentBlockContext.midiQueueDepth = depth;
    sCurrentBlockContext.midiEventsConsumed = consumed;
}

inline void recordPostContext(bool telemetryPub, uint32_t modelResetUs, uint32_t queueUs, uint32_t renderUs, uint32_t telemUs) {
    sCurrentBlockContext.telemetryPublished = telemetryPub;
    if (fixture < fixtureCount) {
        gCallbackOverhead[fixture].add(modelResetUs, queueUs, renderUs, telemUs, telemetryPub);
    }
}

inline void onCallbackComplete(uint32_t processTimeUs, uint32_t blockSequence) {
    if (fixture >= fixtureCount) return;
    const auto cls = sCurrentBlockContext.blockClass;
    if (cls == ForensicsBlockClass::Idle) return;

    const unsigned classIdx = classToIndex(cls);
    results[fixture].callback(classIdx).addUs(processTimeUs);

    if (processTimeUs >= 2667) {
        OverrunRecord rec;
        rec.blockSequence = blockSequence;
        rec.fullCallbackUs = processTimeUs;
        rec.internalRenderUs = sCurrentBlockContext.internalRenderUs;
        rec.blockClass = static_cast<uint8_t>(cls);
        rec.fixtureId = sCurrentBlockContext.fixtureId;
        rec.model = sCurrentBlockContext.model;
        rec.activeVoices = sCurrentBlockContext.activeVoices;
        rec.exciterActive = sCurrentBlockContext.exciterActive;
        rec.midiQueueDepth = sCurrentBlockContext.midiQueueDepth;
        rec.midiEventsConsumed = sCurrentBlockContext.midiEventsConsumed;
        rec.telemetryPublished = sCurrentBlockContext.telemetryPublished;
        rec.modelChangeRequested = sCurrentBlockContext.modelChangeRequested;
        rec.synthResetRequested = sCurrentBlockContext.synthResetRequested;
        gOverrunBuffer.record(rec);
    }
}

inline void render(dsp::SynthEngine& synth, int32_t* output, size_t frames) {
    if (fixture == fixtureCount || (!block && !ready.load(std::memory_order_acquire))) {
        sCurrentBlockClass.store(ForensicsBlockClass::Idle, std::memory_order_relaxed);
        sCurrentBlockContext.blockClass = ForensicsBlockClass::Idle;
        std::fill(output, output + frames * 2, 0); return;
    }
    const unsigned id = fixtureId(fixture);
    const unsigned kind = kindOf(id);
    const bool roll = kind == 7;
    const bool isTransition = (block == 0);
    const bool event = !isTransition && (roll ? ((block - 1) % 38 == 0) : ((block - 1) % 188 == 0));

    ForensicsBlockClass currentClass;
    if (isTransition) {
        currentClass = ForensicsBlockClass::FixtureTransition;
    } else if (event) {
        currentClass = ForensicsBlockClass::Event;
    } else if (synth.hasActiveExciter()) {
        currentClass = ForensicsBlockClass::AttackTail;
    } else {
        currentClass = ForensicsBlockClass::TrueSteady;
    }
    sCurrentBlockClass.store(currentClass, std::memory_order_relaxed);
    const unsigned timingClass = classToIndex(currentClass);

    uint32_t elapsed = 0;
#ifdef CONFIG_POCKETPAN_DSP_PROFILE
    // Declared in function scope: the sparse-sampling decision is taken in the
    // render branch below but consumed again after the transition/render join.
    bool profiled = false;
#endif

    if (isTransition) {
        const uint32_t start = esp_cpu_get_cycle_count();
        synth.setInstrumentModel(modelOf(id));
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
        synth.reset();
        if (sAudioInstance) sAudioInstance->resetTimingStats();
        synth.renderBlock(output, frames);
        elapsed = esp_cpu_get_cycle_count() - start;
    } else {
        if (event && !roll) {
            results[fixture].hardClamp += synth.getHardClampCount();
            synth.reset(); // Reset cost deliberately excluded from event timing.
        }
#ifdef CONFIG_POCKETPAN_DSP_PROFILE
        // Sample steady render blocks sparsely to limit probe perturbation, but
        // profile every event block: normally none of the 188-block event
        // cadence coincides with the old modulo-32 sampling point.
        profiled = event || block % 32 == 1;
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
        elapsed = esp_cpu_get_cycle_count() - start;
    }

    auto& r = results[fixture];
    r.timing(timingClass).add(elapsed);
    if (!isTransition && synth.getVoiceAllocator().getActiveVoiceCount() != counts[kind]) ++r.badVoices;
    if (!ready.load(std::memory_order_acquire)) ++r.bleLost;

    sCurrentBlockContext.internalRenderUs = (elapsed + 239) / 240;
    sCurrentBlockContext.blockClass = currentClass;
    sCurrentBlockContext.fixtureId = static_cast<uint8_t>(id);
    sCurrentBlockContext.model = static_cast<uint8_t>(modelOf(id));
    sCurrentBlockContext.activeVoices = static_cast<uint8_t>(synth.getVoiceAllocator().getActiveVoiceCount());
    sCurrentBlockContext.exciterActive = synth.hasActiveExciter();

#ifdef CONFIG_POCKETPAN_DSP_PROFILE
    if (!isTransition && profiled) {
        auto& prof = r.profile(timingClass);
        prof.total += elapsed;
        for (unsigned i = 0; i < dsp::profile::Count; ++i) prof.phases[i] += dsp::profile::cycles[i];
        ++prof.count;
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

inline void logOverruns() {
    static size_t loggedOverruns = 0;
    const size_t totalOverruns = gOverrunBuffer.count();
    while (loggedOverruns < totalOverruns) {
        OverrunRecord rec;
        if (gOverrunBuffer.get(loggedOverruns, rec)) {
            ESP_LOGW("forensics", "[OVERRUN] seq=%u callback_us=%u inner_us=%u class=%s fixture=%u model=%s voices=%u exciter=%d midi_depth=%u midi_consumed=%u telem=%d model_req=%d reset_req=%d",
                (unsigned)rec.blockSequence, (unsigned)rec.fullCallbackUs, (unsigned)rec.internalRenderUs,
                classToName(static_cast<ForensicsBlockClass>(rec.blockClass)),
                (unsigned)rec.fixtureId, dsp::instrumentModelName(static_cast<dsp::InstrumentModel>(rec.model)),
                (unsigned)rec.activeVoices, rec.exciterActive ? 1 : 0,
                (unsigned)rec.midiQueueDepth, (unsigned)rec.midiEventsConsumed,
                rec.telemetryPublished ? 1 : 0, rec.modelChangeRequested ? 1 : 0, rec.synthResetRequested ? 1 : 0);
        }
        ++loggedOverruns;
    }
}

inline void logCompleted() {
    logOverruns();
    static unsigned logged = 0;
    const unsigned done = completed.load(std::memory_order_acquire);
    while (logged < done) {
        const unsigned id = fixtureId(logged);
        const auto& r = results[logged];
        for (unsigned c = 0; c < kClassCount; ++c) {
            const auto& t = r.timing(c);
            ESP_LOGI("forensics", "[CURVE] model=%s fixture=%u voices=%u class=%s n=%u avg_us=%.2f p95_us=%u p99_us=%u p995_us=%u p999_us=%u max_us=%u deadline=%u bad_voices=%u ble_lost=%u hard=%u sat=%u bin_us=%u",
                dsp::instrumentModelName(modelOf(id)), id, counts[kindOf(id)], kForensicsClassNames[c], (unsigned)t.count,
                t.count ? double(t.sum) / t.count / 240.0 : 0.0, (unsigned)t.p95(), (unsigned)t.p99(),
                (unsigned)t.p995(), (unsigned)t.p999(), (unsigned)t.maximum, (unsigned)t.deadline,
                (unsigned)r.badVoices, (unsigned)r.bleLost, (unsigned)r.hardClamp, (unsigned)r.modalSat,
                (unsigned)kForensicsBinUs);
            for (size_t i = 0; i < t.bins.size(); ++i) if (t.bins[i])
                ESP_LOGI("forensics", "[HIST] fixture=%u class=%s lower_us=%u n=%u", id,
                    kForensicsClassNames[c], (unsigned)(i * kForensicsBinUs), (unsigned)t.bins[i]);

            const auto& cb = r.callback(c);
            const double innerAvg = t.count ? double(t.sum) / t.count / 240.0 : 0.0;
            const double cbAvg = cb.count ? double(cb.sum) / cb.count / 240.0 : 0.0;
            const double overhead = cbAvg - innerAvg;
            ESP_LOGI("forensics", "[CALLBACK_CURVE] model=%s fixture=%u voices=%u class=%s n=%u avg_us=%.2f p95_us=%u p99_us=%u p995_us=%u p999_us=%u max_us=%u deadline=%u overhead_avg_us=%.2f",
                dsp::instrumentModelName(modelOf(id)), id, counts[kindOf(id)], kForensicsClassNames[c], (unsigned)cb.count,
                cbAvg, (unsigned)cb.p95(), (unsigned)cb.p99(),
                (unsigned)cb.p995(), (unsigned)cb.p999(), (unsigned)cb.maximum, (unsigned)cb.deadline,
                overhead);
            for (size_t i = 0; i < cb.bins.size(); ++i) if (cb.bins[i])
                ESP_LOGI("forensics", "[CALLBACK_HIST] fixture=%u class=%s lower_us=%u n=%u", id,
                    kForensicsClassNames[c], (unsigned)(i * kForensicsBinUs), (unsigned)cb.bins[i]);
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
        // Callback overhead breakdown
        const auto& oh = gCallbackOverhead[logged];
        if (oh.count > 0) {
            ESP_LOGI("forensics", "[OVERHEAD] fixture=%u blocks=%u model_reset_avg_us=%.2f model_reset_max_us=%u queue_avg_us=%.2f queue_max_us=%u render_avg_us=%.2f telem_avg_us=%.2f telem_max_us=%u telem_publishes=%u",
                id, (unsigned)oh.count,
                double(oh.modelResetSumUs) / oh.count, (unsigned)oh.modelResetMaxUs,
                double(oh.queueConsumeSumUs) / oh.count, (unsigned)oh.queueMaxUs,
                double(oh.renderSumUs) / oh.count,
                double(oh.telemetrySumUs) / oh.count, (unsigned)oh.telemMaxUs,
                (unsigned)oh.telemPublishCount);
        }
#ifdef CONFIG_POCKETPAN_DSP_PROFILE
        for (unsigned c = 0; c < kClassCount; ++c) {
            const auto& prof = r.profile(c);
            const char* profileClass = kForensicsClassNames[c];
            if (!prof.count) continue;
            ESP_LOGI("forensics", "[PROFILE_TOTAL] fixture=%u class=%s cycles_per_block=%.2f n=%u", id,
                profileClass, double(prof.total) / prof.count, (unsigned)prof.count);
            // Count deliberately follows dsp::profile::Phase, including the
            // M6.3.2 trigger-path phases appended after the M6.3.1 values.
            for (unsigned i = 0; i < dsp::profile::Count; ++i)
                ESP_LOGI("forensics", "[PHASE] fixture=%u class=%s phase=%u cycles_per_block=%.2f n=%u", id,
                    profileClass, i, double(prof.phases[i]) / prof.count,
                    (unsigned)prof.count);
        }
#endif
        ++logged;
    }
}
} // namespace pocketpan::forensics
#endif
