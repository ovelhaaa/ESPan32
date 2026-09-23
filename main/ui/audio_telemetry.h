#pragma once

#include <atomic>
#include <cstdint>
#include <cstring>

namespace pocketpan::ui {

struct AudioTelemetrySnapshot {
    uint8_t activeVoices = 0;
    uint8_t lastNote = 62;
    uint8_t lastVelocity = 0;
    uint8_t lastPressure = 0;
    uint8_t lastEventType = 0; // 0=None, 1=NoteOn, 2=NoteOff, 3=PolyPressure, 4=ChannelPressure, 5=CC, 6=PitchBend, 7=Other
    uint16_t lastTimestamp13 = 0;
    uint8_t lastRawBytes[3] = {0, 0, 0};

    uint32_t avgBlockTimeUs = 0;
    uint32_t maxBlockTimeUs = 0;
    uint32_t deadlineMisses = 0;
    uint32_t writeTimeouts = 0;
    uint32_t txErrors = 0;
    uint32_t shortWrites = 0;
    float cpuLoadPercent = 0.0f;

    uint32_t midiPushCount = 0;
    uint32_t midiPopCount = 0;
    uint32_t midiDrops = 0;
    uint32_t midiHighWater = 0;
};

// Lock-Free Double-Buffered Telemetry Publisher
class AudioTelemetryPublisher {
public:
    AudioTelemetryPublisher() {
        seq_.store(0, std::memory_order_relaxed);
    }

    // Called exclusively by Core 0 (real-time audio path)
    void publish(const AudioTelemetrySnapshot& snap) {
        const uint32_t s = seq_.load(std::memory_order_relaxed);
        const uint32_t nextWriteIdx = (s & 1) ^ 1;
        snapshots_[nextWriteIdx] = snap;
        // Increment sequence: even indicates stable buffer ready
        seq_.store(s + 1, std::memory_order_release);
    }

    // Called by Core 1 (UI task)
    bool read(AudioTelemetrySnapshot& out) const {
        for (int retry = 0; retry < 3; ++retry) {
            const uint32_t s1 = seq_.load(std::memory_order_acquire);
            if (s1 == 0) return false; // Not yet published
            const uint32_t readIdx = (s1 & 1); // Read most recently completed buffer
            out = snapshots_[readIdx];
            const uint32_t s2 = seq_.load(std::memory_order_acquire);
            if (s1 == s2) return true;
        }
        return false;
    }

private:
    std::atomic<uint32_t> seq_{0};
    AudioTelemetrySnapshot snapshots_[2]{};
};

} // namespace pocketpan::ui
