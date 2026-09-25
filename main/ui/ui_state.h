#pragma once

#include <cstdint>
#include "hardware/board_config.h"

namespace pocketpan::ui {

enum class UiScreenMode : uint8_t {
    Status = 0,
    MidiDiagnostic,
    AudioDiagnostic
};

struct UiState {
    UiScreenMode mode = board::ui::kDefaultMidiDiagnostic ? UiScreenMode::MidiDiagnostic : UiScreenMode::Status;

    // Preset & note status
    char presetName[16] = "PAN";
    char rootNoteName[8] = "D3";
    uint8_t lastNoteNumber = 62;
    uint8_t lastVelocity = 0;
    uint8_t lastPressure = 0;
    char lastEventType[16] = "NONE";
    uint16_t lastTimestamp13 = 0;
    uint8_t lastRawBytes[3] = {0, 0, 0};

    // Engine & system metrics
    uint8_t activeVoices = 0;
    uint8_t maxVoices = 8;
    float cpuLoadPercent = 0.0f;
    uint32_t avgBlockTimeUs = 0;
    uint32_t maxBlockTimeUs = 0;
    uint32_t deadlineMisses = 0;
    uint32_t writeTimeouts = 0;
    uint32_t txErrors = 0;
    uint32_t shortWrites = 0;
    float preLimiterPeak = 0.0f;
    float postLimiterPeak = 0.0f;
    float currentGainReductionDb = 0.0f;
    float maxGainReductionDb = 0.0f;
    float averageGainReductionDb = 0.0f;
    uint32_t limiterActiveSamples = 0;
    uint32_t gainReductionOver0p1DbSamples = 0;
    uint32_t gainReductionOver1DbSamples = 0;
    uint32_t hardClampCount = 0;
    uint32_t internalHeapFree = 0;
    uint32_t largestInternalBlock = 0;
    char diagnosticTone[12] = "PAN";

    // MIDI Queue diagnostics
    uint32_t midiPushCount = 0;
    uint32_t midiPopCount = 0;
    uint32_t midiDrops = 0;
    uint32_t midiHighWater = 0;

    // BLE status
    bool bleConnected = false;
    char bleStatus[16] = "BLE SCAN";
    uint32_t bleReconnects = 0;
    uint16_t bleIntervalUnits = 0;
    uint16_t bleLatency = 0;
    int8_t bleRssi = 0;
    uint16_t bleSupervisionTimeout = 0;
    uint8_t bleLastDisconnectReason = 0;
};

} // namespace pocketpan::ui
