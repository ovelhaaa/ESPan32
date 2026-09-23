#include "ui_renderer.h"
#include <cstdio>

namespace pocketpan::ui {

void UiRenderer::render(const UiState& state) {
    if (state.mode == UiScreenMode::MidiDiagnostic) {
        renderDiagnostic(state);
    } else {
        renderStatus(state);
    }
    display_.update();
}

void UiRenderer::renderStatus(const UiState& state) {
    display_.clear(hardware::colors::Background);

    // Header banner
    display_.fillRect(0, 0, hardware::Display::kWidth, 20, hardware::colors::DarkGray);
    display_.drawText(8, 6, "POCKET PAN", hardware::colors::Cyan, 1);
    display_.drawText(175, 6, state.bleConnected ? "BLE OK" : "BLE SCAN",
                      state.bleConnected ? hardware::colors::Green : hardware::colors::Orange, 1);

    display_.drawFastHLine(0, 20, hardware::Display::kWidth, hardware::colors::Gray);

    // Active Preset and Note info
    display_.drawText(12, 28, "PRESET", hardware::colors::Gray, 1);
    display_.drawText(12, 40, state.presetName, hardware::colors::White, 2);

    display_.drawText(140, 28, "ROOT", hardware::colors::Gray, 1);
    display_.drawText(140, 40, state.rootNoteName, hardware::colors::Yellow, 2);

    display_.drawFastHLine(8, 62, hardware::Display::kWidth - 16, hardware::colors::DarkGray);

    // Stats Grid
    char lineBuf[32];

    // Voices
    snprintf(lineBuf, sizeof(lineBuf), "VOICES  %u/%u", state.activeVoices, state.maxVoices);
    display_.drawText(12, 70, lineBuf, hardware::colors::LightGray, 1);

    // Audio Rate
    display_.drawText(140, 70, "AUDIO   48K", hardware::colors::LightGray, 1);

    // CPU Load
    snprintf(lineBuf, sizeof(lineBuf), "CPU     %2.0f%%", state.cpuLoadPercent);
    display_.drawText(12, 88, lineBuf,
                      state.cpuLoadPercent > 75.0f ? hardware::colors::Red : hardware::colors::LightGray, 1);

    // Deadline Misses
    snprintf(lineBuf, sizeof(lineBuf), "DLINE   %u", static_cast<unsigned>(state.deadlineMisses));
    display_.drawText(140, 88, lineBuf,
                      state.deadlineMisses > 0 ? hardware::colors::Red : hardware::colors::LightGray, 1);

    // Footer tip
    display_.drawFastHLine(0, 115, hardware::Display::kWidth, hardware::colors::DarkGray);
    display_.drawText(12, 122, "BOOT BTN: TOGGLE DIAG", hardware::colors::Gray, 1);
}

void UiRenderer::renderDiagnostic(const UiState& state) {
    display_.clear(0x0000); // Black

    // Header banner
    display_.fillRect(0, 0, hardware::Display::kWidth, 18, hardware::colors::Red);
    display_.drawText(8, 5, "MIDI DIAGNOSTIC MONITOR", hardware::colors::White, 1);
    display_.drawFastHLine(0, 18, hardware::Display::kWidth, hardware::colors::Gray);

    char buf[40];

    // Note Number & Name & Velocity
    snprintf(buf, sizeof(buf), "NOTE %u (%s)  VEL %u", state.lastNoteNumber, state.rootNoteName, state.lastVelocity);
    display_.drawText(10, 24, buf, hardware::colors::Yellow, 1);

    // Pressure & Event Type
    snprintf(buf, sizeof(buf), "PRESS %u  TYPE %s", state.lastPressure, state.lastEventType);
    display_.drawText(10, 40, buf, hardware::colors::Cyan, 1);

    // 13-bit Timestamp & Raw Bytes Hex
    snprintf(buf, sizeof(buf), "TS13 %u  HEX %02X %02X %02X",
             state.lastTimestamp13, state.lastRawBytes[0], state.lastRawBytes[1], state.lastRawBytes[2]);
    display_.drawText(10, 56, buf, hardware::colors::LightGray, 1);

    // MIDI Queue HighWater & Drops
    snprintf(buf, sizeof(buf), "QUEUE HWM %u  DROP %u",
             static_cast<unsigned>(state.midiHighWater), static_cast<unsigned>(state.midiDrops));
    display_.drawText(10, 72, buf, state.midiDrops > 0 ? hardware::colors::Red : hardware::colors::Green, 1);

    // Audio Engine stats
    snprintf(buf, sizeof(buf), "DSP %uus (CPU %2.0f%%) DL %u",
             static_cast<unsigned>(state.avgBlockTimeUs), state.cpuLoadPercent, static_cast<unsigned>(state.deadlineMisses));
    display_.drawText(10, 88, buf, state.deadlineMisses > 0 ? hardware::colors::Red : hardware::colors::LightGray, 1);

    // Bottom status
    display_.drawFastHLine(0, 114, hardware::Display::kWidth, hardware::colors::DarkGray);
    snprintf(buf, sizeof(buf), "%s  (BOOT: TOGGLE)",
             state.bleConnected ? "BLE CONNECTED" : "WAITING BLE...");
    display_.drawText(10, 120, buf,
                      state.bleConnected ? hardware::colors::Green : hardware::colors::Gray, 1);
}

} // namespace pocketpan::ui
