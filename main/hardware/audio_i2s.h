#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>

#include "board_config.h"

#ifdef ESP_PLATFORM
#include "driver/i2s_std.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#endif

namespace pocketpan::hardware {

struct AudioStats {
    uint32_t blocksProcessed = 0;
    uint32_t deadlineMisses = 0;
    uint32_t writeTimeouts = 0;
    uint32_t txErrors = 0;
    uint32_t shortWrites = 0;
    uint32_t maxBlockTimeUs = 0;
    uint32_t avgBlockTimeUs = 0;
    float cpuLoadPercent = 0.0f;
};

// Callback to render audio. outInterleaved has size: frames * 2 (stereo 32-bit signed ints)
using AudioRenderCallback = void (*)(void* userData, int32_t* outInterleaved, size_t frames);

class AudioI2S {
public:
    AudioI2S();
    ~AudioI2S();

    bool init(AudioRenderCallback callback, void* userData);
    bool start();
    void stop();
    void deinit();

    AudioStats getStats() const;

private:
#ifdef ESP_PLATFORM
    static void audioTaskEntry(void* arg);
    void audioTaskLoop();

    bool createTxChannel();
    bool recoverTxChannel();

    i2s_chan_handle_t txHandle_ = nullptr;
    TaskHandle_t taskHandle_ = nullptr;
    SemaphoreHandle_t stoppedSignal_ = nullptr;
    int32_t* txBuffer_ = nullptr;
#endif

    AudioRenderCallback callback_ = nullptr;
    void* userData_ = nullptr;
    std::atomic<bool> running_{false};
    bool initialized_ = false;

    struct AtomicAudioStats {
        std::atomic<uint32_t> blocksProcessed{0}, deadlineMisses{0}, writeTimeouts{0};
        std::atomic<uint32_t> txErrors{0}, shortWrites{0}, maxBlockTimeUs{0}, avgBlockTimeUs{0};
        std::atomic<float> cpuLoadPercent{0.0f};
    } stats_;
};

} // namespace pocketpan::hardware
