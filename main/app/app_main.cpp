#include <stdio.h>
#include <string.h>
#include <algorithm>
#include <atomic>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "driver/gpio.h"

#include "hardware/board_config.h"
#include "hardware/audio_i2s.h"
#include "hardware/display.h"
#include "hardware/ble_midi.h"
#include "dsp/synth_engine.h"
#include "dsp/diagnostic_tone.h"
#include "midi/midi_event.h"
#include "midi/midi_mapping.h"
#include "ui/ui_state.h"
#include "ui/ui_renderer.h"
#include "ui/audio_telemetry.h"

namespace {
constexpr const char* kTag = "pocket_pan_main";

pocketpan::dsp::SynthEngine sSynth;
pocketpan::dsp::DiagnosticToneSource sDiagnosticTone;
pocketpan::hardware::AudioI2S sAudio;
pocketpan::hardware::Display sDisplay;
pocketpan::hardware::BleMidi sBleMidi;
pocketpan::midi::SpscMidiQueue<64> sBleMidiQueue;  // NimBLE producer -> audio consumer
pocketpan::midi::SpscMidiQueue<16> sDemoMidiQueue; // UI producer -> audio consumer
pocketpan::ui::AudioTelemetryPublisher sTelemetryPub;
pocketpan::ui::UiState sUiState;
// UI/Core 1 requests; audio/Core 0 consumes at the next block boundary.
std::atomic<bool> sSynthResetRequested{false};

// Static telemetry state maintained on Core 0
pocketpan::ui::AudioTelemetrySnapshot sAudioSnapshot{};
uint8_t sTelemetryDivider = 0;

// Real-Time Audio Callback - Runs strictly on Core 0 at high priority (zero formatting, zero heap, zero mutex)
void audioRenderCallback(void* userData, int32_t* outInterleaved, size_t frames) {
    // SynthEngine is owned exclusively by this Core 0 callback.
    if (sSynthResetRequested.exchange(false, std::memory_order_acq_rel)) {
        sSynth.killAllVoices();
    }

    // 1. Consume all queued MIDI events from Core 1
    pocketpan::midi::MidiEvent ev;
    auto consume = [&](auto& queue) { while (queue.pop(ev)) {
        // Diagnostic tones own audio TX. MIDI remains visible in telemetry but
        // intentionally cannot create hidden musical state while diagnostics run.
        if (sDiagnosticTone.isPan()) sSynth.handleMidiEvent(ev);

        // Record raw numeric event data for telemetry (no string formatting on Core 0)
        sAudioSnapshot.lastNote = ev.data1;
        sAudioSnapshot.lastVelocity = ev.data2;
        sAudioSnapshot.lastTimestamp13 = ev.timestamp13;
        sAudioSnapshot.lastRawBytes[0] = ev.rawBytes[0];
        sAudioSnapshot.lastRawBytes[1] = ev.rawBytes[1];
        sAudioSnapshot.lastRawBytes[2] = ev.rawBytes[2];
        sAudioSnapshot.lastEventType = static_cast<uint8_t>(ev.type);

        if (ev.type == pocketpan::midi::MidiEventType::PolyPressure) {
            sAudioSnapshot.lastPressure = ev.data2;
        } else if (ev.type == pocketpan::midi::MidiEventType::ChannelPressure) {
            sAudioSnapshot.lastPressure = ev.data1;
        }
    }};
    consume(sBleMidiQueue);
    consume(sDemoMidiQueue);

    // 2. One producer owns TX: diagnostics intentionally silence/reset PAN,
    // while MIDI is still consumed for its diagnostic display.
    if (sDiagnosticTone.isPan()) sSynth.renderBlock(outInterleaved, frames);
    else sDiagnosticTone.render(outInterleaved, frames, static_cast<float>(pocketpan::board::audio::kSampleRate));

    // 3. Publish at ~47 Hz (one in eight 128-frame blocks), above the 30 Hz UI
    // rate while avoiding needless cross-core atomic traffic on every block.
    if (++sTelemetryDivider < 8) return;
    sTelemetryDivider = 0;
    const auto stats = sAudio.getStats();
    sAudioSnapshot.activeVoices = static_cast<uint8_t>(sSynth.getVoiceAllocator().getActiveVoiceCount());
    sAudioSnapshot.avgBlockTimeUs = stats.avgBlockTimeUs;
    sAudioSnapshot.maxBlockTimeUs = stats.maxBlockTimeUs;
    sAudioSnapshot.deadlineMisses = stats.deadlineMisses;
    sAudioSnapshot.writeTimeouts = stats.writeTimeouts;
    sAudioSnapshot.txErrors = stats.txErrors;
    sAudioSnapshot.shortWrites = stats.shortWrites;
    sAudioSnapshot.cpuLoadPercent = stats.cpuLoadPercent;
    sAudioSnapshot.preLimiterPeak = sSynth.getPreLimiterPeak();
    sAudioSnapshot.postLimiterPeak = sSynth.getPostLimiterPeak();
    sAudioSnapshot.currentGainReductionDb = sSynth.getCurrentGainReductionDb();
    sAudioSnapshot.maxGainReductionDb = sSynth.getMaxGainReductionDb();
    sAudioSnapshot.averageGainReductionDb = sSynth.getAverageGainReductionDb();
    sAudioSnapshot.limiterActiveSamples = sSynth.getLimiterActiveSamples();
    sAudioSnapshot.gainReductionOver0p1DbSamples = sSynth.getGainReductionOver0p1DbSamples();
    sAudioSnapshot.gainReductionOver1DbSamples = sSynth.getGainReductionOver1DbSamples();
    sAudioSnapshot.hardClampCount = sSynth.getHardClampCount();
    sAudioSnapshot.bodyPeak = sSynth.getBodyPeak();
    sAudioSnapshot.bodyRms = sSynth.getBodyRms();
    sAudioSnapshot.bodyEnergy = sSynth.getBodyEnergy();
    sAudioSnapshot.sympatheticBusPeak = sSynth.getSympatheticBusPeak();
    sAudioSnapshot.sympatheticBusRms = sSynth.getSympatheticBusRms();
    sAudioSnapshot.sympatheticSafetyCount = sSynth.getSympatheticSafetyCount();

    sAudioSnapshot.midiPushCount = sBleMidiQueue.getPushCount() + sDemoMidiQueue.getPushCount();
    sAudioSnapshot.midiPopCount = sBleMidiQueue.getPopCount() + sDemoMidiQueue.getPopCount();
    sAudioSnapshot.midiDrops = sBleMidiQueue.getDrops() + sDemoMidiQueue.getDrops();
    sAudioSnapshot.midiHighWater = std::max(sBleMidiQueue.getHighWaterMark(), sDemoMidiQueue.getHighWaterMark());

    sTelemetryPub.publish(sAudioSnapshot);
}

// UI Task - Runs strictly on Core 1 at ~30 Hz
void uiTaskLoop(void* param) {
    pocketpan::ui::UiRenderer renderer(sDisplay);

    // Configure onboard BOOT button (GPIO0) for toggling between Status and MidiDiagnostic screens
    gpio_config_t bootBtnCfg = {};
    bootBtnCfg.pin_bit_mask = (1ULL << pocketpan::board::ui::kBootButtonGpio);
    bootBtnCfg.mode = GPIO_MODE_INPUT;
    bootBtnCfg.pull_up_en = GPIO_PULLUP_ENABLE;
    bootBtnCfg.pull_down_en = GPIO_PULLDOWN_DISABLE;
    bootBtnCfg.intr_type = GPIO_INTR_DISABLE;
    gpio_config(&bootBtnCfg);

    bool lastBootBtnState = true;
    uint32_t pressStartMs = 0;
    bool longPressHandled = false;
    uint32_t lastHeapUpdateMs = 0;
    uint32_t lastLogMs = 0;
    uint32_t demoTick = 0;

    // D Kurd / D Celtic Handpan scale notes: D3, A3, Bb3, C4, D4, E4, F4, A4
    const uint8_t kHandpanScale[] = { 50, 57, 58, 60, 62, 64, 65, 69 };
    const size_t kScaleLen = sizeof(kHandpanScale) / sizeof(kHandpanScale[0]);
    size_t scaleIdx = 0;

    while (true) {
        // 1. Check Boot Button to toggle screens
        const bool btnPressed = (gpio_get_level(static_cast<gpio_num_t>(pocketpan::board::ui::kBootButtonGpio)) == 0);
        const uint32_t nowMs = static_cast<uint32_t>(xTaskGetTickCount() * portTICK_PERIOD_MS);
        if (btnPressed && lastBootBtnState) { pressStartMs = nowMs; longPressHandled = false; }
        if (btnPressed && sUiState.mode == pocketpan::ui::UiScreenMode::AudioDiagnostic &&
            !longPressHandled && nowMs - pressStartMs >= 800) {
            // Publish PAN then request a Core 0 reset; never mutate SynthEngine here.
            sDiagnosticTone.setTone(pocketpan::dsp::DiagnosticTone::Pan);
            sSynthResetRequested.store(true, std::memory_order_release);
            sUiState.mode = pocketpan::ui::UiScreenMode::Status;
            longPressHandled = true;
        }
        if (!btnPressed && !lastBootBtnState && !longPressHandled) {
            if (sUiState.mode == pocketpan::ui::UiScreenMode::Status) sUiState.mode = pocketpan::ui::UiScreenMode::MidiDiagnostic;
            else if (sUiState.mode == pocketpan::ui::UiScreenMode::MidiDiagnostic) sUiState.mode = pocketpan::ui::UiScreenMode::AudioDiagnostic;
            else {
                const auto next = static_cast<pocketpan::dsp::DiagnosticTone>((static_cast<uint8_t>(sDiagnosticTone.tone()) + 1) % 7);
                const bool transition = sDiagnosticTone.isPan() != (next == pocketpan::dsp::DiagnosticTone::Pan);
                sDiagnosticTone.setTone(next);
                if (transition) sSynthResetRequested.store(true, std::memory_order_release);
            }
        }
        lastBootBtnState = !btnPressed;

        // 2. Read lock-free telemetry snapshot published from Core 0
        pocketpan::ui::AudioTelemetrySnapshot snap;
        if (sTelemetryPub.read(snap)) {
            sUiState.activeVoices = snap.activeVoices;
            sUiState.cpuLoadPercent = snap.cpuLoadPercent;
            sUiState.avgBlockTimeUs = snap.avgBlockTimeUs;
            sUiState.maxBlockTimeUs = snap.maxBlockTimeUs;
            sUiState.deadlineMisses = snap.deadlineMisses;
            sUiState.writeTimeouts = snap.writeTimeouts;
            sUiState.txErrors = snap.txErrors;
            sUiState.shortWrites = snap.shortWrites;
            sUiState.preLimiterPeak = snap.preLimiterPeak;
            sUiState.postLimiterPeak = snap.postLimiterPeak;
            sUiState.currentGainReductionDb = snap.currentGainReductionDb;
            sUiState.maxGainReductionDb = snap.maxGainReductionDb;
            sUiState.averageGainReductionDb = snap.averageGainReductionDb;
            sUiState.limiterActiveSamples = snap.limiterActiveSamples;
            sUiState.gainReductionOver0p1DbSamples = snap.gainReductionOver0p1DbSamples;
            sUiState.gainReductionOver1DbSamples = snap.gainReductionOver1DbSamples;
            sUiState.hardClampCount = snap.hardClampCount;

            sUiState.lastNoteNumber = snap.lastNote;
            sUiState.lastVelocity = snap.lastVelocity;
            sUiState.lastPressure = snap.lastPressure;
            sUiState.lastTimestamp13 = snap.lastTimestamp13;
            sUiState.lastRawBytes[0] = snap.lastRawBytes[0];
            sUiState.lastRawBytes[1] = snap.lastRawBytes[1];
            sUiState.lastRawBytes[2] = snap.lastRawBytes[2];

            sUiState.midiPushCount = snap.midiPushCount;
            sUiState.midiPopCount = snap.midiPopCount;
            sUiState.midiDrops = snap.midiDrops;
            sUiState.midiHighWater = snap.midiHighWater;

            // Perform note name and event string formatting on Core 1 (outside audio callback)
            const char* name = pocketpan::midi::MidiMapping::noteToName(snap.lastNote);
            snprintf(sUiState.rootNoteName, sizeof(sUiState.rootNoteName), "%s", name);

            const auto evType = static_cast<pocketpan::midi::MidiEventType>(snap.lastEventType);
            switch (evType) {
                case pocketpan::midi::MidiEventType::NoteOn:
                    snprintf(sUiState.lastEventType, sizeof(sUiState.lastEventType), "NOTE ON");
                    break;
                case pocketpan::midi::MidiEventType::NoteOff:
                    snprintf(sUiState.lastEventType, sizeof(sUiState.lastEventType), "NOTE OFF");
                    break;
                case pocketpan::midi::MidiEventType::PolyPressure:
                    snprintf(sUiState.lastEventType, sizeof(sUiState.lastEventType), "POLY AT");
                    break;
                case pocketpan::midi::MidiEventType::ChannelPressure:
                    snprintf(sUiState.lastEventType, sizeof(sUiState.lastEventType), "CH AT");
                    break;
                case pocketpan::midi::MidiEventType::ControlChange:
                    snprintf(sUiState.lastEventType, sizeof(sUiState.lastEventType), "CC %u", snap.lastNote);
                    break;
                case pocketpan::midi::MidiEventType::PitchBend:
                    snprintf(sUiState.lastEventType, sizeof(sUiState.lastEventType), "PITCH BEND");
                    break;
                default:
                    snprintf(sUiState.lastEventType, sizeof(sUiState.lastEventType), "NONE");
                    break;
            }
        }

        sUiState.bleConnected = sBleMidi.isConnected();
        sBleMidi.poll(); // low-rate BLE RSSI request; never called by audio task
        sUiState.bleIntervalUnits = sBleMidi.connectionIntervalUnits();
        sUiState.bleLatency = sBleMidi.connectionLatency();
        sUiState.bleRssi = sBleMidi.rssi();
        sUiState.bleReconnects = sBleMidi.reconnectCount();
        sUiState.bleSupervisionTimeout = sBleMidi.supervisionTimeout();
        sUiState.bleLastDisconnectReason = sBleMidi.lastDisconnectReason();
        snprintf(sUiState.diagnosticTone, sizeof(sUiState.diagnosticTone), "%s", sDiagnosticTone.name());
        if (nowMs - lastHeapUpdateMs >= 1000) {
            sUiState.internalHeapFree = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
            sUiState.largestInternalBlock = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
            lastHeapUpdateMs = nowMs;
        }
        const auto bleState = sBleMidi.state();
        const char* bleText = "BLE SCAN";
        if (bleState == pocketpan::hardware::BleMidiState::Connecting || bleState == pocketpan::hardware::BleMidiState::Connected) bleText = "BLE CONN";
        else if (bleState == pocketpan::hardware::BleMidiState::DiscoveringService || bleState == pocketpan::hardware::BleMidiState::DiscoveringCharacteristic || bleState == pocketpan::hardware::BleMidiState::DiscoveringCccd) bleText = "BLE DISC";
        else if (bleState == pocketpan::hardware::BleMidiState::Subscribing) bleText = "BLE SUB";
        else if (bleState == pocketpan::hardware::BleMidiState::Ready) bleText = "BLE OK";
        else if (bleState == pocketpan::hardware::BleMidiState::Error) bleText = "BLE ERR";
        snprintf(sUiState.bleStatus, sizeof(sUiState.bleStatus), "%s", bleText);

        // 3. Standalone bring-up demo: if BLE is not connected yet, trigger a gentle note every 800ms
        demoTick++;
        if (!sBleMidi.isMidiReady() && (demoTick % 25 == 0)) { // ~825 ms
            pocketpan::midi::MidiEvent demoEv;
            demoEv.type = pocketpan::midi::MidiEventType::NoteOn;
            demoEv.data1 = kHandpanScale[scaleIdx];
            demoEv.data2 = 75; // moderate acoustic strike
            demoEv.timestamp13 = 0;
            demoEv.rawBytes[0] = 0x90;
            demoEv.rawBytes[1] = demoEv.data1;
            demoEv.rawBytes[2] = demoEv.data2;
            sDemoMidiQueue.push(demoEv);

            scaleIdx = (scaleIdx + 1) % kScaleLen;
        }

        // 4. Render TFT display
        renderer.render(sUiState);

#ifdef CONFIG_POCKETPAN_HARDWARE_QUALIFICATION_LOG
        if (nowMs - lastLogMs >= 5000) {
            lastLogMs = nowMs;
            ESP_LOGI(kTag, "[AUDIO] blocks=%u avg_us=%u max_us=%u deadline=%u timeout=%u tx_error=%u short=%u",
                     (unsigned)sAudio.getStats().blocksProcessed, (unsigned)sUiState.avgBlockTimeUs, (unsigned)sUiState.maxBlockTimeUs,
                     (unsigned)sUiState.deadlineMisses, (unsigned)sUiState.writeTimeouts, (unsigned)sUiState.txErrors, (unsigned)sUiState.shortWrites);
            ESP_LOGI(kTag, "[MIDI] push=%u pop=%u drop=%u hwm=%u last=(n=%u v=%u t=%u hex=%02X%02X%02X)",
                     (unsigned)sUiState.midiPushCount, (unsigned)sUiState.midiPopCount, (unsigned)sUiState.midiDrops, (unsigned)sUiState.midiHighWater,
                     (unsigned)sUiState.lastNoteNumber, (unsigned)sUiState.lastVelocity, (unsigned)sUiState.lastEventType[0],
                     sUiState.lastRawBytes[0], sUiState.lastRawBytes[1], sUiState.lastRawBytes[2]);
            ESP_LOGI(kTag, "[BLE] state=%u interval_ms=%.2f latency=%u rssi=%d reconnects=%u last_disconnect=%u", (unsigned)bleState, sUiState.bleIntervalUnits * 1.25f, (unsigned)sUiState.bleLatency, sUiState.bleRssi, (unsigned)sUiState.bleReconnects, sUiState.bleLastDisconnectReason);
            ESP_LOGI(kTag, "[MEM] internal_free=%u largest_internal=%u", (unsigned)sUiState.internalHeapFree, (unsigned)sUiState.largestInternalBlock);
        }
#endif

        vTaskDelay(pdMS_TO_TICKS(pocketpan::board::ui::kRefreshPeriodMs));
    }
}

} // namespace

extern "C" void app_main(void) {
    ESP_LOGI(kTag, "===============================================");
    ESP_LOGI(kTag, "  Pocket Pan / Metal Modal Synthesizer v1.0.0");
    ESP_LOGI(kTag, "  Hardware: TENSTAR TS-ESP32-S3 (Feather TFT)");
    ESP_LOGI(kTag, "===============================================");

    // 1. Initialize DSP Engine (48kHz, 8 voices, PAN preset)
    sSynth.init(static_cast<float>(pocketpan::board::audio::kSampleRate));

    // 2. Connect SPSC lock-free queue to BLE transport
    sBleMidi.setQueue(&sBleMidiQueue);

    // 3. Initialize I2S Audio Driver FIRST (TX-only, 48kHz, stereo, 32-bit
    // slot, DMA). Bringing I2S up before the LCD/SPI2 matters on ESP32-S3:
    // both blocks share the GDMA controller, and the audio transport must
    // claim its channel and clock from a clean state. A stalled TX otherwise
    // manifests as 100% zero-byte writes that look like a dead DSP.
    if (!sAudio.init(audioRenderCallback, nullptr) || !sAudio.start()) {
        ESP_LOGE(kTag, "Fatal: Audio I2S initialization failed!");
        return;
    }

    // 4. Initialize BLE MIDI Central on Core 1
    if (!sBleMidi.begin()) {
        ESP_LOGW(kTag, "BLE MIDI Central failed to start advertising/scanning");
    }

    // 5. Initialize Display LAST, after the audio transport owns its GDMA path
    if (!sDisplay.init()) {
        ESP_LOGE(kTag, "Failed to initialize ST7789 display");
    }

    // 6. Start UI Task on Core 1
    xTaskCreatePinnedToCore(
        uiTaskLoop,
        "ui_task",
        pocketpan::board::ui::kStackBytes,
        nullptr,
        pocketpan::board::ui::kTaskPriority,
        nullptr,
        pocketpan::board::ui::kTaskCore // Core 1
    );

    ESP_LOGI(kTag, "Pocket Pan initialization complete. Realtime audio active.");
}
