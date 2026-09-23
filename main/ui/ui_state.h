#pragma once

#include <cstdint>
#include "hardware/board_config.h"

namespace pocketpan::ui {

enum class UiScreenMode : uint8_t {
    Status = 0,
    MidiDiagnostic
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

    // MIDI Queue diagnostics
    uint32_t midiPushCount = 0;
    uint32_t midiPopCount = 0;
    uint32_t midiDrops = 0;
    uint32_t midiHighWater = 0;

    // BLE status
    bool bleConnected = false;
    char bleStatus[16] = "BLE SCAN";
};

} // namespace pocketpan::ui
