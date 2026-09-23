#pragma once

#include <cstdint>

namespace pocketpan::board {

namespace audio {
constexpr int kSampleRate = 48000;
constexpr int kI2sBitsPerSlot = 32;
constexpr int kChannels = 2; // Stereo
constexpr int kDmaBufferCount = 6;
constexpr int kDmaBufferFrames = 128; // 128 frames stereo @ 32-bit = 1024 bytes per DMA buffer (~2.67 ms per block)
constexpr size_t kDmaBufferBytes = kDmaBufferFrames * kChannels * sizeof(int32_t); // 1024 bytes

namespace i2s {
// Validated pinout from orbit-echo / TENSTAR TS-ESP32-S3 (Adafruit Feather ESP32-S3 TFT clone)
constexpr int kBclkGpio = 11;
constexpr int kLrckGpio = 12;
constexpr int kDoutGpio = 6;  // I2S TX data out -> DIN on PCM5102
constexpr int kMclkGpio = 10; // Reserved. Driven to logic LOW to force PCM5102 internal PLL on SCK
constexpr int kDinGpio  = 13; // Reserved (RX disabled in MVP)
} // namespace i2s

namespace task {
constexpr int kCore = 0; // Strictly Core 0 for audio real-time path
constexpr int kPriority = 23; // High priority (configMAX_PRIORITIES - 2)
constexpr int kStackBytes = 6144;
} // namespace task
} // namespace audio

namespace tft {
constexpr uint16_t kWidth = 240;
constexpr uint16_t kHeight = 135;

namespace spi {
constexpr int kMosiGpio = 35;
constexpr int kSclkGpio = 36;
constexpr int kCsGpio   = 7;
} // namespace spi

constexpr int kDcGpio        = 39;
constexpr int kResetGpio     = 40;
constexpr int kBacklightGpio = 45;
constexpr int kPowerGpio     = 21; // TFT_I2C_POWER gate on Adafruit ESP32-S3 TFT Feather

// Display alignment specific to 240x135 ST7789 panel
constexpr int kGapX = 40;
constexpr int kGapY = 53;
constexpr int kSpiClockHz = 40 * 1000 * 1000; // 40 MHz SPI2
} // namespace tft

namespace ble {
// Standard MIDI over BLE UUIDs (MIDI Association Specification)
constexpr const char* kMidiServiceUuid = "03b80e5a-ede8-4b33-a751-6ce34ec4c700";
constexpr const char* kMidiCharUuid    = "7772e5db-3868-4112-a1a9-f2669d106bf3";
constexpr int kTaskCore = 1; // Strictly Core 1 for BLE / NimBLE Host
} // namespace ble

namespace ui {
constexpr uint32_t kRefreshPeriodMs = 33; // ~30 Hz
constexpr int kTaskCore = 1;              // Core 1
constexpr uint32_t kTaskPriority = 3;
constexpr uint32_t kStackBytes = 4096;
} // namespace ui

} // namespace pocketpan::board
