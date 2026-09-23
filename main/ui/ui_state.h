#pragma once

#include <cstdint>
#include <string>

namespace pocketpan::ui {

enum class UiScreenMode : uint8_t {
    Status = 0,
    MidiDiagnostic
};

struct UiState {
    UiScreenMode mode = UiScreenMode::Status;

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
    uint32_t underruns = 0;

    // BLE status
    bool bleConnected = false;
    char bleStatus[16] = "BLE SCAN";
};

} // namespace pocketpan::ui
