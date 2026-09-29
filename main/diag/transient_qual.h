#pragma once
// M6.3.9.2 — Transient burst closure diagnostics.
//
// Diagnostic-only instrumentation.  Compiled out entirely unless
// POCKETPAN_TRANSIENT_QUAL=1 (see main/CMakeLists.txt).  Runs on the audio
// task (Core 0) with fixed-size numeric state: no strings, no heap, no locks.
//
// It answers one question: do callbacks that exceed the nominal 2.6667 ms block
// period accumulate into real DMA starvation, or are they isolated bursts the
// configured I2S pipeline absorbs?  To do that it tracks, independently of any
// resettable model telemetry:
//   * lifetime callback/overrun counters (never reset on PAN/BELL switch)
//   * maximum consecutive overrun streak
//   * a mathematically valid CPU timing-debt accumulator that recovers
//   * buffered-headroom occupancy in 1/1000 blocks, derived from the actual
//     DMA consumption rate (48 kHz block clock), not from a guess
//   * a fixed ring buffer of overrun records classified by musical transition
//
// The occupancy model: each audio loop produces exactly one block; the DMA
// consumes one block every 8000/3 us.  Therefore
//     occupancy = blocksProduced - elapsedUs * 3 / 8000
// is the number of blocks still buffered for the hardware.  It ramps to the
// driver's descriptor depth (6 x 128 frames) and falls only when the DSP falls
// behind real time.  Its minimum over the run is the true minimum headroom.

#include <atomic>
#include <cstdint>
#include <cstddef>
#include <algorithm>

#ifndef POCKETPAN_TRANSIENT_QUAL
#define POCKETPAN_TRANSIENT_QUAL 0
#endif

#if POCKETPAN_TRANSIENT_QUAL

#ifdef ESP_PLATFORM
#include "esp_timer.h"
#include "esp_log.h"
#endif

namespace pocketpan::diag::transient {

// 128 frames @ 48 kHz = 2666.667 us, expressed as 8000/3 to keep integer math.
inline constexpr uint32_t kBlockPeriodUs = 2667;
inline constexpr int64_t kBlockPeriodUsNumerator = 8000;
inline constexpr int64_t kBlockPeriodUsDenominator = 3;

// Actual I2S TX descriptor depth from board_config.h (kDmaBufferCount = 6).
// Each descriptor holds exactly one 128-frame block, so the hardware pipeline
// can buffer up to six blocks (16.0 ms) ahead of the playback head.  The write
// path blocks when no descriptor is free, which is the steady state here; a
// blocking write therefore anchors the queue at capacity immediately after it
// returns.  The relative occupancy tracked below is referenced to that anchor.
inline constexpr int32_t kDescriptorCapacity = 6;
inline constexpr int32_t kEdgeHeadroomMilli = kDescriptorCapacity * 1000;

enum class Transition : uint8_t {
    Other = 0,
    V1to2 = 1,
    V2to4 = 2,
    V4to6 = 3,
    V6to8 = 4,
    Restrike = 5,
    Pressure = 6,
    ModelSwitch = 7,
    Count = 8,
};

inline constexpr const char* kTransitionNames[static_cast<size_t>(Transition::Count)] = {
    "other", "1->2", "2->4", "4->6", "6->8", "restrike", "pressure", "model_switch"
};

// Written by the audio render callback (Core 0), read by the audio task loop
// (Core 0) in the same iteration.  No cross-core access.
struct BlockContext {
    uint8_t model = 0;            // 0=PAN, 1=BELL, 2=TONGUE
    uint8_t voicesBefore = 0;
    uint8_t voicesAfter = 0;
    uint8_t noteOns = 0;
    uint8_t noteOffs = 0;
    uint8_t polyPressure = 0;
    uint8_t channelPressure = 0;
    uint8_t midiConsumed = 0;
    bool exciterActive = false;
    bool modelChange = false;
    bool synthReset = false;
    bool telemetryPublished = false;
    uint32_t midiDispatchUs = 0;
    uint32_t renderUs = 0;
};

inline BlockContext gContext;

// Fixed-size overrun record.  Ring buffer, append-only from Core 0.
struct OverrunRecord {
    uint32_t timestampMs = 0;
    uint32_t blockSeq = 0;
    uint32_t callbackUs = 0;
    uint32_t renderUs = 0;
    uint32_t midiDispatchUs = 0;
    uint32_t prevCallbackUs = 0;
    uint32_t nextCallbackUs = 0;
    int32_t debtBeforeUs = 0;
    int32_t debtAfterUs = 0;
    int32_t occupancyMilliBlocks = 0;
    uint8_t model = 0;
    uint8_t voicesBefore = 0;
    uint8_t voicesAfter = 0;
    uint8_t noteOns = 0;
    uint8_t noteOffs = 0;
    uint8_t polyPressure = 0;
    uint8_t channelPressure = 0;
    uint8_t streak = 0;
    uint8_t transition = 0;
    bool telemetryPublished = false;
    bool exciterActive = false;
};

class QualState {
public:
    static constexpr size_t kOverrunCapacity = 256;

    // ---- context, set by the audio render callback -------------------------
    inline void beginCallback(uint32_t nowUs) {
        contextStartUs_ = nowUs;
    }
    inline void endCallback(uint32_t nowUs) {
        gContext.renderUs = static_cast<uint32_t>(nowUs - contextStartUs_);
    }

    // ---- per-loop accounting, called by the audio task after each write ----
    // nowUs       : esp_timer timestamp taken at loop start
    // callbackUs  : full audioRenderCallback duration
    // writeUs     : i2s_channel_write() duration
    inline void onBlock(uint32_t nowUs, uint32_t callbackUs, uint32_t writeUs, uint32_t blockSeq) {
        if (!started_) {
            started_ = true;
            startUs_ = nowUs;
            produced_ = 0;
            lastCallbackUs_ = callbackUs;
        }

        // Fill the "next callback" field of a record captured on the previous
        // block, now that the following callback duration is known.
        if (pendingNextIdx_ >= 0) {
            records_[pendingNextIdx_].nextCallbackUs = callbackUs;
            pendingNextIdx_ = -1;
        }

        produced_++;
        lifetimeCallbacks_++;
        lifetimeWriteSumUs_ += writeUs;
        if (callbackUs > lifetimeMaxCallbackUs_) lifetimeMaxCallbackUs_ = callbackUs;

        // Occupancy in 1/1000 blocks, respecting the true DMA block clock.
        const int64_t elapsedUs = static_cast<int64_t>(nowUs - startUs_);
        const int64_t consumedMilli = (elapsedUs * kBlockPeriodUsDenominator * 1000) / kBlockPeriodUsNumerator;
        const int32_t occupancyMilli = static_cast<int32_t>(produced_ * 1000 - consumedMilli);
        if (occupancyMilli < minOccupancyMilli_) minOccupancyMilli_ = occupancyMilli;
        if (occupancyMilli > maxOccupancyMilli_) maxOccupancyMilli_ = occupancyMilli;

        // CPU timing debt: cumulative render work in excess of the real-time
        // budget.  Clamped at zero when the DSP has caught up, so it recovers.
        if (renderDebtUs_ < 0) renderDebtUs_ = 0;
        renderDebtUs_ += static_cast<int32_t>(callbackUs) - static_cast<int32_t>(kBlockPeriodUs);
        if (renderDebtUs_ < 0) renderDebtUs_ = 0;
        if (renderDebtUs_ > maxRenderDebtUs_) maxRenderDebtUs_ = renderDebtUs_;

        if (pendingRecovery_) {
            if (renderDebtUs_ == 0) {
                const uint32_t blocks = blockSeq - recoveryStartBlock_;
                if (blocks > maxRecoveryBlocks_) maxRecoveryBlocks_ = blocks;
                pendingRecovery_ = false;
            }
        }

        const bool overrun = callbackUs >= kBlockPeriodUs;
        if (overrun) {
            ++lifetimeOverruns_;
            ++currentStreak_;
            if (currentStreak_ > maxStreak_) maxStreak_ = currentStreak_;
            captureOverrun(nowUs, callbackUs, blockSeq, occupancyMilli);
            pendingRecovery_ = true;
            recoveryStartBlock_ = blockSeq;
        } else {
            currentStreak_ = 0;
        }

        lastCallbackUs_ = callbackUs;
    }

    // ---- accessors (UI task, Core 1) ---------------------------------------
    uint32_t lifetimeCallbacks() const { return lifetimeCallbacks_; }
    uint32_t lifetimeOverruns() const { return lifetimeOverruns_; }
    uint32_t lifetimeMaxCallbackUs() const { return lifetimeMaxCallbackUs_; }
    uint32_t maxStreak() const { return maxStreak_; }
    uint32_t maxRenderDebtUs() const { return maxRenderDebtUs_; }
    int32_t minOccupancyMilli() const { return minOccupancyMilli_; }
    int32_t maxOccupancyMilli() const { return maxOccupancyMilli_; }
    // Absolute minimum buffered headroom in 1/1000 blocks.  The relative
    // occupancy is anchored to the saturated (blocking-write) baseline, so the
    // worst transient buffer consumption is (max - min) and the remaining depth
    // is the descriptor capacity minus that consumption.
    int32_t effectiveMinHeadroomMilli() const {
        return kEdgeHeadroomMilli + minOccupancyMilli_ - maxOccupancyMilli_;
    }
    int32_t maxTransientConsumptionMilli() const {
        return maxOccupancyMilli_ - minOccupancyMilli_;
    }
    uint32_t maxRecoveryBlocks() const { return maxRecoveryBlocks_; }
    uint64_t lifetimeWriteSumUs() const { return lifetimeWriteSumUs_; }
    uint32_t overrunCount() const { return overrunHead_.load(std::memory_order_relaxed); }

    const uint32_t* transitionCounts() const { return transitionCounts_; }

    bool copyOverrun(size_t index, OverrunRecord& out) const {
        const uint32_t total = overrunHead_.load(std::memory_order_relaxed);
        if (index >= total || (total > kOverrunCapacity && index < total - kOverrunCapacity)) {
            return false;
        }
        out = records_[index % kOverrunCapacity];
        return true;
    }

private:
    inline void captureOverrun(uint32_t nowUs, uint32_t callbackUs, uint32_t blockSeq, int32_t occupancyMilli) {
        const uint32_t idx = overrunHead_.fetch_add(1, std::memory_order_relaxed) % kOverrunCapacity;
        OverrunRecord& r = records_[idx];
        r.timestampMs = static_cast<uint32_t>(nowUs / 1000);
        r.blockSeq = blockSeq;
        r.callbackUs = callbackUs;
        r.renderUs = gContext.renderUs;
        r.midiDispatchUs = gContext.midiDispatchUs;
        r.prevCallbackUs = lastCallbackUs_;
        r.nextCallbackUs = 0;
        r.debtBeforeUs = renderDebtUs_ - (static_cast<int32_t>(callbackUs) - static_cast<int32_t>(kBlockPeriodUs));
        r.debtAfterUs = renderDebtUs_;
        r.occupancyMilliBlocks = occupancyMilli;
        r.model = gContext.model;
        r.voicesBefore = gContext.voicesBefore;
        r.voicesAfter = gContext.voicesAfter;
        r.noteOns = gContext.noteOns;
        r.noteOffs = gContext.noteOffs;
        r.polyPressure = gContext.polyPressure;
        r.channelPressure = gContext.channelPressure;
        r.streak = currentStreak_;
        r.transition = static_cast<uint8_t>(classify(gContext));
        r.telemetryPublished = gContext.telemetryPublished;
        r.exciterActive = gContext.exciterActive;
        pendingNextIdx_ = static_cast<int>(idx);
        transitionCounts_[r.transition]++;
    }

    static Transition classify(const BlockContext& c) {
        if (c.modelChange) return Transition::ModelSwitch;
        if (c.noteOns == 0) {
            if (c.polyPressure > 0 || c.channelPressure > 0) return Transition::Pressure;
            return Transition::Other;
        }
        if (c.voicesAfter <= c.voicesBefore) return Transition::Restrike;
        switch (c.voicesAfter) {
            case 1: return Transition::Other;      // 0 -> 1 single note
            case 2: return Transition::V1to2;
            case 3: return Transition::Other;
            case 4: return Transition::V2to4;
            case 5: return Transition::Other;
            case 6: return Transition::V4to6;
            case 7: return Transition::Other;
            case 8: return Transition::V6to8;
            default: return Transition::Other;
        }
    }

    // ---- state (Core 0 writer, Core 1 reader) ------------------------------
    uint32_t contextStartUs_ = 0;
    bool started_ = false;
    uint32_t startUs_ = 0;
    uint64_t produced_ = 0;
    uint32_t lastCallbackUs_ = 0;

    int32_t renderDebtUs_ = 0;
    int32_t maxRenderDebtUs_ = 0;
    int32_t minOccupancyMilli_ = 1000000;
    int32_t maxOccupancyMilli_ = -1000000;
    uint32_t currentStreak_ = 0;
    uint32_t maxStreak_ = 0;
    bool pendingRecovery_ = false;
    uint32_t recoveryStartBlock_ = 0;
    uint32_t maxRecoveryBlocks_ = 0;

    uint32_t lifetimeCallbacks_ = 0;
    uint32_t lifetimeOverruns_ = 0;
    uint32_t lifetimeMaxCallbackUs_ = 0;
    uint64_t lifetimeWriteSumUs_ = 0;

    uint32_t transitionCounts_[static_cast<size_t>(Transition::Count)]{};

    int pendingNextIdx_ = -1;
    OverrunRecord records_[kOverrunCapacity]{};
    std::atomic<uint32_t> overrunHead_{0};
};

inline QualState gQual;

inline void logSummary() {
#ifdef ESP_PLATFORM
    const int32_t minOcc = gQual.minOccupancyMilli();
    ESP_LOGI("m6392",
        "[QUAL] callbacks=%u overruns=%u max_cb_us=%u max_streak=%u max_debt_us=%u "
        "min_occ_milliblocks=%d max_occ_milliblocks=%d eff_headroom_milliblocks=%d "
        "max_transient_consumption_milliblocks=%d max_recovery_blocks=%u write_avg_us=%.1f",
        (unsigned)gQual.lifetimeCallbacks(), (unsigned)gQual.lifetimeOverruns(),
        (unsigned)gQual.lifetimeMaxCallbackUs(), (unsigned)gQual.maxStreak(),
        (unsigned)gQual.maxRenderDebtUs(), (int)minOcc, (int)gQual.maxOccupancyMilli(),
        (int)gQual.effectiveMinHeadroomMilli(), (int)gQual.maxTransientConsumptionMilli(),
        (unsigned)gQual.maxRecoveryBlocks(),
        gQual.lifetimeCallbacks() ? double(gQual.lifetimeWriteSumUs()) / gQual.lifetimeCallbacks() : 0.0);
    const uint32_t* tc = gQual.transitionCounts();
    ESP_LOGI("m6392",
        "[QUAL_BY_TRANSITION] other=%u 1to2=%u 2to4=%u 4to6=%u 6to8=%u restrike=%u pressure=%u model_switch=%u",
        (unsigned)tc[0], (unsigned)tc[1], (unsigned)tc[2], (unsigned)tc[3],
        (unsigned)tc[4], (unsigned)tc[5], (unsigned)tc[6], (unsigned)tc[7]);
#endif
}

inline void logNewOverruns() {
#ifdef ESP_PLATFORM
    static size_t logged = 0;
    const uint32_t total = gQual.overrunCount();
    while (logged < total) {
        OverrunRecord r;
        if (gQual.copyOverrun(logged, r)) {
            const char* tname = r.transition < static_cast<uint8_t>(Transition::Count)
                ? kTransitionNames[r.transition] : "?";
            ESP_LOGW("m6392",
                "[OVERRUN] seq=%u cb_us=%u prev=%u next=%u render_us=%u midi_us=%u model=%s "
                "v=%u->%u on=%u off=%u poly=%u ch=%u streak=%u debt_before=%d debt_after=%d occ_milliblocks=%d telem=%d exc=%d transition=%s",
                (unsigned)r.blockSeq, (unsigned)r.callbackUs, (unsigned)r.prevCallbackUs,
                (unsigned)r.nextCallbackUs, (unsigned)r.renderUs, (unsigned)r.midiDispatchUs,
                (r.model == 0 ? "PAN" : (r.model == 1 ? "BELL" : "TONGUE")), (unsigned)r.voicesBefore, (unsigned)r.voicesAfter,
                (unsigned)r.noteOns, (unsigned)r.noteOffs, (unsigned)r.polyPressure,
                (unsigned)r.channelPressure, (unsigned)r.streak,
                (int)r.debtBeforeUs, (int)r.debtAfterUs, (int)r.occupancyMilliBlocks,
                r.telemetryPublished ? 1 : 0, r.exciterActive ? 1 : 0, tname);
        }
        ++logged;
    }
#endif
}

} // namespace pocketpan::diag::transient

#endif // POCKETPAN_TRANSIENT_QUAL
