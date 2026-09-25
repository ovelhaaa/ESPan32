#pragma once

#include <atomic>
#include <cstdint>

namespace pocketpan::ui {

struct AudioTelemetrySnapshot {
    uint8_t activeVoices = 0, lastNote = 62, lastVelocity = 0, lastPressure = 0, lastEventType = 0;
    uint16_t lastTimestamp13 = 0;
    uint8_t lastRawBytes[3] = {0, 0, 0};
    uint32_t avgBlockTimeUs = 0, maxBlockTimeUs = 0, deadlineMisses = 0;
    uint32_t writeTimeouts = 0, txErrors = 0, shortWrites = 0;
    float cpuLoadPercent = 0.0f;
    float preLimiterPeak = 0.0f, postLimiterPeak = 0.0f;
    float currentGainReductionDb = 0.0f, maxGainReductionDb = 0.0f;
    uint32_t limiterActiveSamples = 0, hardClampCount = 0;
    uint32_t midiPushCount = 0, midiPopCount = 0, midiDrops = 0, midiHighWater = 0;
};

// Every field is atomic: publication and reads are data-race-free under the C++
// memory model. A generation counter lets readers reject mixed generations.
class AudioTelemetryPublisher {
public:
    void publish(const AudioTelemetrySnapshot& v) {
        generation_.fetch_add(1, std::memory_order_acq_rel);
#define STORE(name) name##_.store(v.name, std::memory_order_relaxed)
        STORE(activeVoices); STORE(lastNote); STORE(lastVelocity); STORE(lastPressure); STORE(lastEventType);
        STORE(lastTimestamp13); for (int i=0;i<3;++i) lastRawBytes_[i].store(v.lastRawBytes[i], std::memory_order_relaxed);
        STORE(avgBlockTimeUs); STORE(maxBlockTimeUs); STORE(deadlineMisses); STORE(writeTimeouts); STORE(txErrors); STORE(shortWrites);
        STORE(cpuLoadPercent); STORE(midiPushCount); STORE(midiPopCount); STORE(midiDrops); STORE(midiHighWater);
        STORE(preLimiterPeak); STORE(postLimiterPeak); STORE(currentGainReductionDb); STORE(maxGainReductionDb); STORE(limiterActiveSamples); STORE(hardClampCount);
#undef STORE
        generation_.fetch_add(1, std::memory_order_release);
    }
    bool read(AudioTelemetrySnapshot& v) const {
        for (int retry=0; retry<8; ++retry) {
            const uint32_t before=generation_.load(std::memory_order_acquire);
            if (!before || (before&1U)) continue;
#define LOAD(name) v.name = name##_.load(std::memory_order_relaxed)
            LOAD(activeVoices); LOAD(lastNote); LOAD(lastVelocity); LOAD(lastPressure); LOAD(lastEventType);
            LOAD(lastTimestamp13); for (int i=0;i<3;++i) v.lastRawBytes[i]=lastRawBytes_[i].load(std::memory_order_relaxed);
            LOAD(avgBlockTimeUs); LOAD(maxBlockTimeUs); LOAD(deadlineMisses); LOAD(writeTimeouts); LOAD(txErrors); LOAD(shortWrites);
            LOAD(cpuLoadPercent); LOAD(midiPushCount); LOAD(midiPopCount); LOAD(midiDrops); LOAD(midiHighWater);
            LOAD(preLimiterPeak); LOAD(postLimiterPeak); LOAD(currentGainReductionDb); LOAD(maxGainReductionDb); LOAD(limiterActiveSamples); LOAD(hardClampCount);
#undef LOAD
            // Keep every payload load before the final sequence observation on
            // weakly ordered cores (notably ESP32-S3). The first acquire pairs
            // with publication; this acquire fence supplies the read barrier.
            std::atomic_thread_fence(std::memory_order_acquire);
            if (before==generation_.load(std::memory_order_relaxed)) return true;
        }
        return false;
    }
private:
    mutable std::atomic<uint32_t> generation_{0};
#define FIELD(type,name) std::atomic<type> name##_{0}
    FIELD(uint8_t,activeVoices); FIELD(uint8_t,lastNote); FIELD(uint8_t,lastVelocity); FIELD(uint8_t,lastPressure); FIELD(uint8_t,lastEventType);
    FIELD(uint16_t,lastTimestamp13); std::atomic<uint8_t> lastRawBytes_[3]{};
    FIELD(uint32_t,avgBlockTimeUs); FIELD(uint32_t,maxBlockTimeUs); FIELD(uint32_t,deadlineMisses); FIELD(uint32_t,writeTimeouts);
    FIELD(uint32_t,txErrors); FIELD(uint32_t,shortWrites); FIELD(float,cpuLoadPercent); FIELD(uint32_t,midiPushCount);
    FIELD(uint32_t,midiPopCount); FIELD(uint32_t,midiDrops); FIELD(uint32_t,midiHighWater);
    FIELD(float,preLimiterPeak); FIELD(float,postLimiterPeak); FIELD(float,currentGainReductionDb); FIELD(float,maxGainReductionDb);
    FIELD(uint32_t,limiterActiveSamples); FIELD(uint32_t,hardClampCount);
#undef FIELD
};

} // namespace pocketpan::ui
