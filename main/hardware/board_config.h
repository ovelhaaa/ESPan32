#pragma once

#include <cstdint>
#include <cstddef>

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
constexpr int kPriority = 12; // RT above UI(3)/NimBLE, below LWIP(18)/WiFi/IPC; 6x128 DMA (~16ms) needs no max prio
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

// Display alignment specific to the 240x135 ST7789 panel.
// MADCTL = MV|MY (swap_xy + mirror_y) == Adafruit rotation 1, whose reference
// offsets are x=rowstart=(320-240)/2=40 and y=colstart2=(240-135)/2=52.
// Using 53 here shifted the image by one row and exposed an unwritten GRAM
// line at the top of the panel (random colors).
constexpr int kGapX = 40;
constexpr int kGapY = 52;
constexpr int kSpiClockHz = 40 * 1000 * 1000; // 40 MHz SPI2
} // namespace tft

namespace ble {
// Standard MIDI over BLE UUIDs (MIDI Association Specification)
// Service: 03B80E5A-EDE8-4B33-A751-6CE34EC4C700
// Characteristic: 7772E5DB-3868-4112-A1A9-F2669D106BF3
constexpr const char* kMidiServiceUuid = "03b80e5a-ede8-4b33-a751-6ce34ec4c700";
constexpr const char* kMidiCharUuid    = "7772e5db-3868-4112-a1a9-f2669d106bf3";
constexpr uint16_t kCccdUuid16         = 0x2902;

// Little-endian 128-bit byte representation for NimBLE BLE_UUID128_INIT
constexpr uint8_t kMidiServiceUuidBytes[16] = {
    0x00, 0xc7, 0xc4, 0x4e, 0xe3, 0x6c, 0x51, 0xa7,
    0x33, 0x4b, 0xe8, 0xed, 0x5a, 0x0e, 0xb8, 0x03
};

constexpr uint8_t kMidiCharUuidBytes[16] = {
    0xf3, 0x6b, 0x10, 0x9d, 0x66, 0xf2, 0xa9, 0xa1,
    0x12, 0x41, 0x68, 0x38, 0xdb, 0xe5, 0x72, 0x77
};

constexpr int kTaskCore = 1; // Strictly Core 1 for BLE / NimBLE Host
// The disconnect-retry task calls back into the NimBLE host (`ble_gap_disc`,
// `ble_hs_id_infer_auto`) plus ESP_LOG. 2048 bytes overflowed and panicked on
// the first retry; size it like the host task and keep it off the audio core.
constexpr uint32_t kRetryTaskStackBytes = 4096;
constexpr uint32_t kRetryTaskPriority = 3;
} // namespace ble

namespace ui {
constexpr uint32_t kRefreshPeriodMs = 33; // ~30 Hz
constexpr int kTaskCore = 1;              // Core 1
constexpr uint32_t kTaskPriority = 3;
constexpr uint32_t kStackBytes = 4096;
constexpr int kBootButtonGpio = 0;        // Onboard BOOT button toggles diagnostic
#ifdef CONFIG_POCKETPAN_MIDI_DIAGNOSTIC_DEFAULT
constexpr bool kDefaultMidiDiagnostic = true;
#else
constexpr bool kDefaultMidiDiagnostic = false;
#endif
} // namespace ui

} // namespace pocketpan::board
