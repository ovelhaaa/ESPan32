#include "audio_i2s.h"

#ifdef ESP_PLATFORM
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "driver/gpio.h"
#include <cstring>

namespace pocketpan::hardware {

namespace {
constexpr const char* kTag = "AudioI2S";
}

AudioI2S::AudioI2S() = default;

AudioI2S::~AudioI2S() {
    deinit();
}

bool AudioI2S::init(AudioRenderCallback callback, void* userData) {
    if (initialized_) return true;

    callback_ = callback;
    userData_ = userData;
    stats_ = {};

    // 1. PCM5102 SCK clock requirement:
    // If PCM5102 SCK is wired to ESP32-S3 GPIO10, force GPIO10 strictly LOW with pulldown.
    // PCM5102 internal PLL generates system clock automatically when SCK is held LOW.
    gpio_config_t sck_cfg = {
        .pin_bit_mask = 1ULL << board::audio::i2s::kMclkGpio,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_ENABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&sck_cfg);
    gpio_set_level(static_cast<gpio_num_t>(board::audio::i2s::kMclkGpio), 0);

    // 2. Allocate I2S channel with modern driver/i2s_std.h API
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    chan_cfg.dma_desc_num = board::audio::kDmaBufferCount;   // 6 descriptors
    chan_cfg.dma_frame_num = board::audio::kDmaBufferFrames; // 128 stereo frames (1024 bytes/buffer)
    chan_cfg.auto_clear = true;

    esp_err_t err = i2s_new_channel(&chan_cfg, &txHandle_, nullptr);
    if (err != ESP_OK) {
        ESP_LOGE(kTag, "Failed to allocate I2S TX channel: %s", esp_err_to_name(err));
        return false;
    }

    // 3. Configure standard Philips I2S 48kHz, 32-bit slot, stereo
    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(board::audio::kSampleRate),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED, // MCLK is held LOW separately on GPIO10 for PCM5102 PLL
            .bclk = static_cast<gpio_num_t>(board::audio::i2s::kBclkGpio),
            .ws   = static_cast<gpio_num_t>(board::audio::i2s::kLrckGpio),
            .dout = static_cast<gpio_num_t>(board::audio::i2s::kDoutGpio),
            .din  = I2S_GPIO_UNUSED,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false,
            },
        },
    };

    err = i2s_channel_init_std_mode(txHandle_, &std_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(kTag, "Failed to initialize I2S standard mode: %s", esp_err_to_name(err));
        i2s_del_channel(txHandle_);
        txHandle_ = nullptr;
        return false;
    }

    // 4. Allocate DMA buffer in internal SRAM (128 frames * 2 channels * 4 bytes = 1024 bytes)
    txBuffer_ = static_cast<int32_t*>(heap_caps_malloc(board::audio::kDmaBufferBytes,
                                                       MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA));
    if (!txBuffer_) {
        // Fallback to general internal SRAM
        txBuffer_ = static_cast<int32_t*>(heap_caps_malloc(board::audio::kDmaBufferBytes,
                                                           MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    }
    if (!txBuffer_) {
        ESP_LOGE(kTag, "Failed to allocate audio TX buffer in internal SRAM");
        deinit();
        return false;
    }
    std::memset(txBuffer_, 0, board::audio::kDmaBufferBytes);

    stoppedSignal_ = xSemaphoreCreateBinary();
    if (!stoppedSignal_) {
        ESP_LOGE(kTag, "Failed to create stopped semaphore");
        deinit();
        return false;
    }

    initialized_ = true;
    ESP_LOGI(kTag, "I2S TX initialized (48kHz, 32-bit slot, stereo, 6x128 frames DMA = %u bytes/buf)",
             static_cast<unsigned>(board::audio::kDmaBufferBytes));
    return true;
}

bool AudioI2S::start() {
    if (!initialized_ || running_.load(std::memory_order_acquire)) {
        return initialized_;
    }

    esp_err_t err = i2s_channel_enable(txHandle_);
    if (err != ESP_OK) {
        ESP_LOGE(kTag, "Failed to enable I2S channel: %s", esp_err_to_name(err));
        return false;
    }

    running_.store(true, std::memory_order_release);
    BaseType_t ret = xTaskCreatePinnedToCore(
        audioTaskEntry,
        "audio_tx_task",
        board::audio::task::kStackBytes,
        this,
        board::audio::task::kPriority,
        &taskHandle_,
        board::audio::task::kCore // Strictly Core 0
    );

    if (ret != pdPASS) {
        ESP_LOGE(kTag, "Failed to create audio task on Core %d", board::audio::task::kCore);
        running_.store(false, std::memory_order_release);
        i2s_channel_disable(txHandle_);
        return false;
    }

    ESP_LOGI(kTag, "Audio task started on Core %d at priority %d",
             board::audio::task::kCore, board::audio::task::kPriority);
    return true;
}

void AudioI2S::stop() {
    if (!running_.load(std::memory_order_acquire)) return;

    running_.store(false, std::memory_order_release);
    if (taskHandle_ && stoppedSignal_) {
        xSemaphoreTake(stoppedSignal_, pdMS_TO_TICKS(500));
    }
    taskHandle_ = nullptr;

    if (txHandle_) {
        i2s_channel_disable(txHandle_);
    }
}

void AudioI2S::deinit() {
    stop();

    if (txHandle_) {
        i2s_del_channel(txHandle_);
        txHandle_ = nullptr;
    }

    if (txBuffer_) {
        heap_caps_free(txBuffer_);
        txBuffer_ = nullptr;
    }

    if (stoppedSignal_) {
        vSemaphoreDelete(stoppedSignal_);
        stoppedSignal_ = nullptr;
    }

    initialized_ = false;
}

AudioStats AudioI2S::getStats() const {
    return stats_;
}

void AudioI2S::audioTaskEntry(void* arg) {
    static_cast<AudioI2S*>(arg)->audioTaskLoop();
}

void AudioI2S::audioTaskLoop() {
    const size_t bytesPerBlock = board::audio::kDmaBufferBytes;
    const size_t framesPerBlock = board::audio::kDmaBufferFrames;
    const int64_t blockBudgetUs = static_cast<int64_t>((1000000ULL * framesPerBlock) / board::audio::kSampleRate); // ~2666 us
    const TickType_t txTimeoutTicks = pdMS_TO_TICKS(16); // ~16ms max wait before reporting underrun/error

    uint64_t totalProcessTimeUs = 0;
    uint32_t windowBlocks = 0;

    while (running_.load(std::memory_order_acquire)) {
        int64_t tStart = esp_timer_get_time();

        // 1. Render block into preallocated buffer (No heap allocation, strictly in internal SRAM)
        if (callback_) {
            callback_(userData_, txBuffer_, framesPerBlock);
        } else {
            std::memset(txBuffer_, 0, bytesPerBlock);
        }

        int64_t tRenderDone = esp_timer_get_time();
        uint32_t processTimeUs = static_cast<uint32_t>(tRenderDone - tStart);

        // Update real-time profiling stats
        if (processTimeUs > stats_.maxBlockTimeUs) {
            stats_.maxBlockTimeUs = processTimeUs;
        }
        totalProcessTimeUs += processTimeUs;
        windowBlocks++;
        if (windowBlocks >= 100) {
            stats_.avgBlockTimeUs = static_cast<uint32_t>(totalProcessTimeUs / windowBlocks);
            stats_.cpuLoadPercent = (static_cast<float>(stats_.avgBlockTimeUs) / static_cast<float>(blockBudgetUs)) * 100.0f;
            totalProcessTimeUs = 0;
            windowBlocks = 0;
        }

        // 2. Write to I2S DMA. This blocks until a DMA buffer is free, pacing the real-time audio loop.
        size_t bytesWritten = 0;
        esp_err_t err = i2s_channel_write(txHandle_, txBuffer_, bytesPerBlock, &bytesWritten, txTimeoutTicks);

        if (err != ESP_OK) {
            stats_.txErrors++;
            if (err == ESP_ERR_TIMEOUT) {
                stats_.underruns++;
            }
        } else if (bytesWritten != bytesPerBlock) {
            stats_.shortWrites++;
        }

        stats_.blocksProcessed++;
    }

    if (stoppedSignal_) {
        xSemaphoreGive(stoppedSignal_);
    }
    vTaskDelete(nullptr);
}

} // namespace pocketpan::hardware

#else

namespace pocketpan::hardware {
AudioI2S::AudioI2S() = default;
AudioI2S::~AudioI2S() = default;
bool AudioI2S::init(AudioRenderCallback, void*) { initialized_ = true; return true; }
bool AudioI2S::start() { running_.store(true); return true; }
void AudioI2S::stop() { running_.store(false); }
void AudioI2S::deinit() { initialized_ = false; }
AudioStats AudioI2S::getStats() const { return stats_; }
} // namespace pocketpan::hardware

#endif
