#include <cstdint>
#include <cstring>
#include <cstdio>

#include "hardware/board_config.h"
#include "hardware/audio_i2s.h"
#include "hardware/display.h"
#include "hardware/ble_midi.h"
#include "dsp/synth_engine.h"
#include "midi/midi_event.h"
#include "midi/midi_mapping.h"
#include "ui/ui_state.h"
#include "ui/ui_renderer.h"

#ifdef ESP_PLATFORM
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_heap_caps.h"

namespace {
constexpr const char* kTag = "pocket_pan_main";

pocketpan::dsp::SynthEngine sSynth;
pocketpan::hardware::AudioI2S sAudio;
pocketpan::hardware::Display sDisplay;
pocketpan::hardware::BleMidi sBleMidi;
pocketpan::midi::SpscMidiQueue<64> sMidiQueue;
pocketpan::ui::UiState sUiState;

// Real-Time Audio Callback - Runs strictly on Core 0 at high priority
void audioRenderCallback(void* userData, int32_t* outInterleaved, size_t frames) {
    // 1. Consume all queued MIDI events from Core 1
    pocketpan::midi::MidiEvent ev;
    while (sMidiQueue.pop(ev)) {
        sSynth.handleMidiEvent(ev);

        // Update telemetry for UI (non-blocking)
        sUiState.lastNoteNumber = ev.data1;
        sUiState.lastVelocity = ev.data2;
        sUiState.lastTimestamp13 = ev.timestamp13;
        sUiState.lastRawBytes[0] = ev.rawBytes[0];
        sUiState.lastRawBytes[1] = ev.rawBytes[1];
        sUiState.lastRawBytes[2] = ev.rawBytes[2];

        const char* name = pocketpan::midi::MidiMapping::noteToName(ev.data1);
        snprintf(sUiState.rootNoteName, sizeof(sUiState.rootNoteName), "%s", name);

        switch (ev.type) {
            case pocketpan::midi::MidiEventType::NoteOn:
                snprintf(sUiState.lastEventType, sizeof(sUiState.lastEventType), "NOTE ON");
                break;
            case pocketpan::midi::MidiEventType::NoteOff:
                snprintf(sUiState.lastEventType, sizeof(sUiState.lastEventType), "NOTE OFF");
                break;
            case pocketpan::midi::MidiEventType::PolyPressure:
                sUiState.lastPressure = ev.data2;
                snprintf(sUiState.lastEventType, sizeof(sUiState.lastEventType), "POLY AT");
                break;
            case pocketpan::midi::MidiEventType::ChannelPressure:
                sUiState.lastPressure = ev.data1;
                snprintf(sUiState.lastEventType, sizeof(sUiState.lastEventType), "CH AT");
                break;
            case pocketpan::midi::MidiEventType::ControlChange:
                snprintf(sUiState.lastEventType, sizeof(sUiState.lastEventType), "CC %u", ev.data1);
                break;
            default:
                snprintf(sUiState.lastEventType, sizeof(sUiState.lastEventType), "OTHER");
                break;
        }
    }

    // 2. Synthesize polyphonic audio block
    sSynth.renderBlock(outInterleaved, frames);
}

// UI Task - Runs strictly on Core 1 at ~30 Hz
void uiTaskLoop(void* param) {
    pocketpan::ui::UiRenderer renderer(sDisplay);

    uint32_t demoTick = 0;
    // D Kurd / D Celtic Handpan scale notes: D3, A3, Bb3, C4, D4, E4, F4, A4
    const uint8_t kHandpanScale[] = { 62, 69, 70, 72, 74, 76, 77, 81 };
    const size_t kScaleLen = sizeof(kHandpanScale) / sizeof(kHandpanScale[0]);
    size_t scaleIdx = 0;

    while (true) {
        // Collect real-time telemetry from audio engine
        const auto stats = sAudio.getStats();
        sUiState.cpuLoadPercent = stats.cpuLoadPercent;
        sUiState.underruns = stats.underruns;
        sUiState.activeVoices = static_cast<uint8_t>(sSynth.getVoiceAllocator().getActiveVoiceCount());
        sUiState.bleConnected = sBleMidi.isConnected();

        // Standalone bring-up demo: if BLE is not connected yet, trigger a gentle note every 800ms
        demoTick++;
        if (!sUiState.bleConnected && (demoTick % 25 == 0)) { // ~825 ms
            pocketpan::midi::MidiEvent demoEv;
            demoEv.type = pocketpan::midi::MidiEventType::NoteOn;
            demoEv.data1 = kHandpanScale[scaleIdx];
            demoEv.data2 = 85; // moderate velocity
            sMidiQueue.push(demoEv);

            scaleIdx = (scaleIdx + 1) % kScaleLen;
        }

        // Render TFT display
        renderer.render(sUiState);

        vTaskDelay(pdMS_TO_TICKS(pocketpan::board::ui::kRefreshPeriodMs));
    }
}

} // namespace

extern "C" void app_main(void) {
    ESP_LOGI(kTag, "===============================================");
    ESP_LOGI(kTag, "  Pocket Pan / Metal Modal Synthesizer v1.0.0");
    ESP_LOGI(kTag, "  Hardware: TENSTAR TS-ESP32-S3 (Feather TFT)");
    ESP_LOGI(kTag, "===============================================");

    // 1. Initialize Display on Core 1
    if (!sDisplay.init()) {
        ESP_LOGE(kTag, "Failed to initialize ST7789 display");
    }

    // 2. Initialize DSP Engine (48kHz, 8 voices, PAN preset)
    sSynth.init(static_cast<float>(pocketpan::board::audio::kSampleRate));

    // 3. Connect SPSC lock-free queue to BLE transport
    sBleMidi.setQueue(&sMidiQueue);

    // 4. Initialize I2S Audio Driver (TX-only, 48kHz, stereo, 32-bit slot, DMA)
    if (!sAudio.init(audioRenderCallback, nullptr) || !sAudio.start()) {
        ESP_LOGE(kTag, "Fatal: Audio I2S initialization failed!");
        return;
    }

    // 5. Initialize BLE MIDI Central on Core 1
    if (!sBleMidi.begin()) {
        ESP_LOGW(kTag, "BLE MIDI failed to begin; continuing with standalone engine");
    }

    // 6. Launch UI task pinned to Core 1
    xTaskCreatePinnedToCore(
        uiTaskLoop,
        "ui_tft_task",
        pocketpan::board::ui::kStackBytes,
        nullptr,
        pocketpan::board::ui::kTaskPriority,
        nullptr,
        pocketpan::board::ui::kTaskCore // Core 1
    );

    ESP_LOGI(kTag, "System started successfully. Audio on Core 0, UI & BLE on Core 1.");
}

#endif
