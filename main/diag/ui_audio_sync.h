#pragma once

#include <atomic>
#include <cstdint>
#include <cstddef>
#include <algorithm>

#ifdef ESP_PLATFORM
#include "esp_timer.h"
#endif

namespace pocketpan::diag {

// Phase H: Correlation markers between Core 0 audio blocks and Core 1 UI activity
struct UiAudioCorrelation {
    // Published by Core 1 / LCD ISR (lock-free)
    std::atomic<bool> uiDrawing{false};
    std::atomic<bool> lcdTransferActive{false};
    std::atomic<uint32_t> uiRenderSequence{0};

    // Incremented by Core 0 (audio callback)
    // Thresholds: >1733, >2000, >2400, >=2667 us
    struct ThresholdCounters {
        std::atomic<uint32_t> total{0};
        std::atomic<uint32_t> drawingOnly{0};
        std::atomic<uint32_t> lcdOnly{0};
        std::atomic<uint32_t> both{0};
        std::atomic<uint32_t> neither{0};

        void reset() {
            total.store(0, std::memory_order_relaxed);
            drawingOnly.store(0, std::memory_order_relaxed);
            lcdOnly.store(0, std::memory_order_relaxed);
            both.store(0, std::memory_order_relaxed);
            neither.store(0, std::memory_order_relaxed);
        }
    };

    ThresholdCounters over1733;
    ThresholdCounters over2000;
    ThresholdCounters over2400;
    ThresholdCounters over2667;

    inline void recordBlock(uint32_t durationUs) {
        if (durationUs <= 1733) return;
        const bool drawing = uiDrawing.load(std::memory_order_relaxed);
        const bool lcd = lcdTransferActive.load(std::memory_order_relaxed);

        auto tally = [&](ThresholdCounters& tc) {
            tc.total.fetch_add(1, std::memory_order_relaxed);
            if (drawing && lcd) tc.both.fetch_add(1, std::memory_order_relaxed);
            else if (drawing) tc.drawingOnly.fetch_add(1, std::memory_order_relaxed);
            else if (lcd) tc.lcdOnly.fetch_add(1, std::memory_order_relaxed);
            else tc.neither.fetch_add(1, std::memory_order_relaxed);
        };

        tally(over1733);
        if (durationUs > 2000) tally(over2000);
        if (durationUs > 2400) tally(over2400);
        if (durationUs >= 2667) tally(over2667);
    }

    void reset() {
        over1733.reset();
        over2000.reset();
        over2400.reset();
        over2667.reset();
    }
};

inline UiAudioCorrelation gUiAudioCorrelation;

// Phase I: Preallocated Rare-Stall Ring Buffer (64 entries, zero heap, zero formatting in callback)
struct RareStallRecord {
    uint32_t timestampMs = 0;
    uint32_t renderDurationUs = 0;
    uint8_t activeVoices = 0;
    uint8_t model = 0; // 0=PAN, 1=BELL, 2=TONGUE
    bool uiDrawing = false;
    bool lcdTransferActive = false;
    uint8_t bleState = 0;
    bool telemetryPublish = false;
    uint32_t lastMidiAgeMs = 0;
    uint32_t audioBlockSequence = 0;
};

class RareStallForensics {
public:
    static constexpr size_t kCapacity = 64;

    void setActiveVoices(uint8_t v) { cachedActiveVoices_.store(v, std::memory_order_relaxed); }
    void setModel(uint8_t m) { cachedModel_.store(m, std::memory_order_relaxed); }
    void setBleState(uint8_t s) { cachedBleState_.store(s, std::memory_order_relaxed); }
    void updateLastMidiMs(uint32_t ms) { lastMidiMs_.store(ms, std::memory_order_relaxed); }

    inline void record(uint32_t durationUs, uint32_t blockSeq) {
        if (durationUs <= 2000) return;
        const uint32_t idx = head_.fetch_add(1, std::memory_order_relaxed) % kCapacity;
        RareStallRecord& r = records_[idx];
#ifdef ESP_PLATFORM
        r.timestampMs = static_cast<uint32_t>(esp_timer_get_time() / 1000);
#else
        r.timestampMs = 0;
#endif
        r.renderDurationUs = durationUs;
        r.activeVoices = cachedActiveVoices_.load(std::memory_order_relaxed);
        r.model = cachedModel_.load(std::memory_order_relaxed);
        r.uiDrawing = gUiAudioCorrelation.uiDrawing.load(std::memory_order_relaxed);
        r.lcdTransferActive = gUiAudioCorrelation.lcdTransferActive.load(std::memory_order_relaxed);
        r.bleState = cachedBleState_.load(std::memory_order_relaxed);
        r.telemetryPublish = telemetryPublishActive_.load(std::memory_order_relaxed);
        const uint32_t lastMidi = lastMidiMs_.load(std::memory_order_relaxed);
        r.lastMidiAgeMs = (lastMidi > 0 && r.timestampMs >= lastMidi) ? (r.timestampMs - lastMidi) : 0;
        r.audioBlockSequence = blockSeq;
        totalRecorded_.fetch_add(1, std::memory_order_relaxed);
    }

    inline void record(uint32_t durationUs, uint8_t activeVoices, uint8_t model,
                       uint8_t bleState, uint32_t lastMidiMs, uint32_t blockSeq) {
        setActiveVoices(activeVoices);
        setModel(model);
        setBleState(bleState);
        updateLastMidiMs(lastMidiMs);
        record(durationUs, blockSeq);
    }

    uint32_t totalRecorded() const { return totalRecorded_.load(std::memory_order_relaxed); }

    size_t copyRecords(RareStallRecord* dest, size_t maxCount) const {
        const uint32_t count = totalRecorded_.load(std::memory_order_relaxed);
        const size_t toCopy = std::min(maxCount, std::min<size_t>(count, kCapacity));
        const uint32_t start = count > kCapacity ? (count - kCapacity) : 0;
        for (size_t i = 0; i < toCopy; ++i) {
            dest[i] = records_[(start + i) % kCapacity];
        }
        return toCopy;
    }

    void setTelemetryPublishActive(bool active) {
        telemetryPublishActive_.store(active, std::memory_order_relaxed);
    }

    void reset() {
        head_.store(0, std::memory_order_relaxed);
        totalRecorded_.store(0, std::memory_order_relaxed);
    }

private:
    RareStallRecord records_[kCapacity]{};
    std::atomic<uint32_t> head_{0};
    std::atomic<uint32_t> totalRecorded_{0};
    std::atomic<bool> telemetryPublishActive_{false};
    std::atomic<uint8_t> cachedActiveVoices_{0};
    std::atomic<uint8_t> cachedModel_{0};
    std::atomic<uint8_t> cachedBleState_{0};
    std::atomic<uint32_t> lastMidiMs_{0};
};

inline RareStallForensics gRareStallForensics;

} // namespace pocketpan::diag
