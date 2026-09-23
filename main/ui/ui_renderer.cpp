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

    // Underruns
    snprintf(lineBuf, sizeof(lineBuf), "UNDERRUN %u", static_cast<unsigned>(state.underruns));
    display_.drawText(140, 88, lineBuf,
                      state.underruns > 0 ? hardware::colors::Red : hardware::colors::LightGray, 1);

    // Footer tip
    display_.drawFastHLine(0, 115, hardware::Display::kWidth, hardware::colors::DarkGray);
    display_.drawText(12, 122, "PRESS PAD FOR CHOKE", hardware::colors::Gray, 1);
}

void UiRenderer::renderDiagnostic(const UiState& state) {
    display_.clear(0x0000); // Black

    // Header banner
    display_.fillRect(0, 0, hardware::Display::kWidth, 20, hardware::colors::Red);
    display_.drawText(8, 6, "MIDI DIAGNOSTIC MONITOR", hardware::colors::White, 1);
    display_.drawFastHLine(0, 20, hardware::Display::kWidth, hardware::colors::Gray);

    char buf[40];

    // Note Number & Name
    snprintf(buf, sizeof(buf), "NOTE   %u (%s)", state.lastNoteNumber, state.rootNoteName);
    display_.drawText(12, 28, buf, hardware::colors::Yellow, 1);

    // Velocity
    snprintf(buf, sizeof(buf), "VEL    %u", state.lastVelocity);
    display_.drawText(12, 44, buf, hardware::colors::White, 1);

    // Pressure
    snprintf(buf, sizeof(buf), "PRESS  %u", state.lastPressure);
    display_.drawText(12, 60, buf, hardware::colors::Cyan, 1);

    // Event Type
    snprintf(buf, sizeof(buf), "TYPE   %s", state.lastEventType);
    display_.drawText(12, 76, buf, hardware::colors::Green, 1);

    // 13-bit Timestamp
    snprintf(buf, sizeof(buf), "TS13   %u", state.lastTimestamp13);
    display_.drawText(12, 92, buf, hardware::colors::LightGray, 1);

    // Raw Bytes Hex
    snprintf(buf, sizeof(buf), "RAW    %02X %02X %02X",
             state.lastRawBytes[0], state.lastRawBytes[1], state.lastRawBytes[2]);
    display_.drawText(12, 108, buf, hardware::colors::Orange, 1);

    // Bottom status
    display_.drawFastHLine(0, 122, hardware::Display::kWidth, hardware::colors::DarkGray);
    display_.drawText(12, 125, state.bleConnected ? "BLE CONNECTED" : "WAITING BLE...",
                      state.bleConnected ? hardware::colors::Green : hardware::colors::Gray, 1);
}

} // namespace pocketpan::ui
