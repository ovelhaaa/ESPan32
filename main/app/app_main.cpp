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
#include "polyphony_forensics.h"
#include "dsp/diagnostic_tone.h"
#include "midi/midi_event.h"
#include "midi/midi_mapping.h"
#include "ui/ui_state.h"
#include "ui/ui_renderer.h"
#include "ui/audio_telemetry.h"
#include "diag/ui_audio_sync.h"
#include "diag/active_soak.h"
#include "diag/transient_qual.h"
#include "esp_timer.h"

namespace {
constexpr const char* kTag = "pocket_pan_main";

#if defined(POCKETPAN_UI_TIMING) && POCKETPAN_UI_TIMING
// M6.3.7 Phase A: Core-1 workload split, diagnostic only (compile-off).
// state = telemetry read + string formatting; render = framebuffer draw + LCD
// submit, itself split by the renderer into framebuffer and LCD microseconds.
struct UiTiming {
    uint64_t stateSum = 0, renderSum = 0, fbSum = 0, lcdSum = 0;
    uint32_t count = 0, stateMax = 0, renderMax = 0, fbMax = 0, lcdMax = 0;
};
UiTiming sUiTiming;
#endif

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
// UI/Core 1 selects the next model; Core 0 performs the complete model reset
// at an audio block boundary so no DSP state crosses instrument models.
#if defined(POCKETPAN_TONGUE_SMOKE) && POCKETPAN_TONGUE_SMOKE
std::atomic<pocketpan::dsp::InstrumentModel> sSelectedInstrumentModel{pocketpan::dsp::InstrumentModel::Tongue};
#elif defined(POCKETPAN_BOWL_SMOKE) && POCKETPAN_BOWL_SMOKE
std::atomic<pocketpan::dsp::InstrumentModel> sSelectedInstrumentModel{pocketpan::dsp::InstrumentModel::Bowl};
#else
std::atomic<pocketpan::dsp::InstrumentModel> sSelectedInstrumentModel{pocketpan::dsp::InstrumentModel::Pan};
#endif
std::atomic<bool> sModelChangeRequested{false};

inline pocketpan::dsp::InstrumentModel selectedInstrumentModel() {
    return sSelectedInstrumentModel.load(std::memory_order_acquire);
}
inline void requestInstrumentModel(pocketpan::dsp::InstrumentModel model) {
    sSelectedInstrumentModel.store(model, std::memory_order_release);
    sModelChangeRequested.store(true, std::memory_order_release);
}

// Static telemetry state maintained on Core 0
pocketpan::ui::AudioTelemetrySnapshot sAudioSnapshot{};
uint8_t sTelemetryDivider = 0;

#if defined(POCKETPAN_TAIL_NO_UI) && POCKETPAN_TAIL_NO_UI
#if defined(CONFIG_POCKETPAN_POLYPHONY_FORENSICS)
// M6.3.5 Phase C U2 support: the UI task is suspended, so a minimal task keeps
// publishing the forensics curves.  It performs no rendering and no LCD I/O.
void forensicsLogTask(void* param) {
    (void)param;
    while (true) {
        // The UI task normally publishes `ready`; with UI suspended this task
        // must, or the fixture would never leave its silence gate.
        pocketpan::forensics::ready.store(POCKETPAN_FORENSICS_DISCONNECTED
            ? !sBleMidi.isConnected() : sBleMidi.isMidiReady(), std::memory_order_release);
        pocketpan::forensics::logCompleted();
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}
#endif
#endif

#if defined(POCKETPAN_UI_POLL_SEPARATE) && POCKETPAN_UI_POLL_SEPARATE
// M6.3.6 Phase A (§8) diagnostic: BLE polling moves to its own low-rate Core 1
// task so the UI task can run with rendering disabled while the BLE stack keeps
// exactly the same poll cadence.  Connection/advertising behavior is untouched.
void blePollTask(void* param) {
    (void)param;
    while (true) {
        sBleMidi.poll();
        vTaskDelay(pdMS_TO_TICKS(pocketpan::board::ui::kRefreshPeriodMs));
    }
}
#endif

// Real-Time Audio Callback - Runs strictly on Core 0 at high priority (zero formatting, zero heap, zero mutex)
void audioRenderCallback(void* userData, int32_t* outInterleaved, size_t frames) {
#ifdef CONFIG_POCKETPAN_POLYPHONY_FORENSICS
    const int64_t tCb0 = esp_timer_get_time();
#endif
#if POCKETPAN_TRANSIENT_QUAL
    const int64_t tqStart = esp_timer_get_time();
    uint8_t qualNoteOns = 0, qualNoteOffs = 0, qualPoly = 0, qualChan = 0;
    const uint8_t qualVoicesBefore = static_cast<uint8_t>(sSynth.getVoiceAllocator().getActiveVoiceCount());
#endif
    // SynthEngine is owned exclusively by this Core 0 callback.
    const bool modelChangeReq = sModelChangeRequested.exchange(false, std::memory_order_acq_rel);
    if (modelChangeReq) {
        const auto model = selectedInstrumentModel();
        sSynth.setInstrumentModel(model);
        sAudio.resetTimingStats();
#if defined(POCKETPAN_RARE_STALL_FORENSICS) && POCKETPAN_RARE_STALL_FORENSICS
        pocketpan::diag::gRareStallForensics.setModel(static_cast<uint8_t>(model));
#endif
    }
    const bool synthResetReq = sSynthResetRequested.exchange(false, std::memory_order_acq_rel);
    if (synthResetReq) {
        sSynth.killAllVoices();
    }
#ifdef CONFIG_POCKETPAN_POLYPHONY_FORENSICS
    const int64_t tCb1 = esp_timer_get_time();
#endif

    // 1. Consume all queued MIDI events from Core 1
#ifdef CONFIG_POCKETPAN_POLYPHONY_FORENSICS
    const uint8_t midiDepth = static_cast<uint8_t>(std::min<size_t>(255, sBleMidiQueue.size() + sDemoMidiQueue.size()));
    uint8_t midiConsumed = 0;
#endif
    pocketpan::midi::MidiEvent ev;
    auto consume = [&](auto& queue) { while (queue.pop(ev)) {
#ifdef CONFIG_POCKETPAN_POLYPHONY_FORENSICS
        ++midiConsumed;
#endif
#if POCKETPAN_TRANSIENT_QUAL
        switch (ev.type) {
            case pocketpan::midi::MidiEventType::NoteOn: ++qualNoteOns; break;
            case pocketpan::midi::MidiEventType::NoteOff: ++qualNoteOffs; break;
            case pocketpan::midi::MidiEventType::PolyPressure: ++qualPoly; break;
            case pocketpan::midi::MidiEventType::ChannelPressure: ++qualChan; break;
            default: break;
        }
#endif
        // Diagnostic tones own audio TX. MIDI remains visible in telemetry but
        // intentionally cannot create hidden musical state while diagnostics run.
        #ifndef CONFIG_POCKETPAN_POLYPHONY_FORENSICS
        if (sDiagnosticTone.isPan()) sSynth.handleMidiEvent(ev);
#endif
#if defined(POCKETPAN_RARE_STALL_FORENSICS) && POCKETPAN_RARE_STALL_FORENSICS
        pocketpan::diag::gRareStallForensics.updateLastMidiMs(static_cast<uint32_t>(esp_timer_get_time() / 1000));
#endif

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
#if POCKETPAN_TRANSIENT_QUAL
    const int64_t tqAfterMidi = esp_timer_get_time();
#endif

#ifdef CONFIG_POCKETPAN_POLYPHONY_FORENSICS
    const int64_t tCb2 = esp_timer_get_time();
    pocketpan::forensics::setCallbackPreContext(modelChangeReq, synthResetReq, midiDepth, midiConsumed);
#endif

    // 2. One producer owns TX: diagnostics intentionally silence/reset PAN,
    // while MIDI is still consumed for its diagnostic display.
    #ifdef CONFIG_POCKETPAN_POLYPHONY_FORENSICS
    pocketpan::forensics::render(sSynth, outInterleaved, frames);
#else
    if (sDiagnosticTone.isPan()) sSynth.renderBlock(outInterleaved, frames);
    else sDiagnosticTone.render(outInterleaved, frames, static_cast<float>(pocketpan::board::audio::kSampleRate));
#endif

#if POCKETPAN_TRANSIENT_QUAL
    const int64_t tqAfterRender = esp_timer_get_time();
#endif

#ifdef CONFIG_POCKETPAN_POLYPHONY_FORENSICS
    const int64_t tCb3 = esp_timer_get_time();
#endif

    // 3. Publish at ~47 Hz (one in eight 128-frame blocks), above the 30 Hz UI
    // rate while avoiding needless cross-core atomic traffic on every block.
#ifdef CONFIG_POCKETPAN_POLYPHONY_FORENSICS
    bool telemPublished = false;
#endif
    if (++sTelemetryDivider >= 8) {
        sTelemetryDivider = 0;
#ifdef CONFIG_POCKETPAN_POLYPHONY_FORENSICS
        telemPublished = true;
#endif
        const auto stats = sAudio.getStats();
        sAudioSnapshot.activeVoices = static_cast<uint8_t>(sSynth.getVoiceAllocator().getActiveVoiceCount());
        sAudioSnapshot.avgBlockTimeUs = stats.avgBlockTimeUs;
        sAudioSnapshot.p99BlockTimeUs = stats.p99BlockTimeUs;
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

#ifdef CONFIG_POCKETPAN_POLYPHONY_FORENSICS
    const int64_t tCb4 = esp_timer_get_time();
    pocketpan::forensics::recordPostContext(
        telemPublished,
        static_cast<uint32_t>(tCb1 - tCb0),
        static_cast<uint32_t>(tCb2 - tCb1),
        static_cast<uint32_t>(tCb3 - tCb2),
        static_cast<uint32_t>(tCb4 - tCb3)
    );
#endif

#if POCKETPAN_TRANSIENT_QUAL
    {
        auto& ctx = pocketpan::diag::transient::gContext;
        ctx.model = static_cast<uint8_t>(selectedInstrumentModel());
        ctx.voicesBefore = qualVoicesBefore;
        ctx.voicesAfter = static_cast<uint8_t>(sSynth.getVoiceAllocator().getActiveVoiceCount());
        ctx.noteOns = qualNoteOns;
        ctx.noteOffs = qualNoteOffs;
        ctx.polyPressure = qualPoly;
        ctx.channelPressure = qualChan;
        ctx.midiConsumed = static_cast<uint8_t>(qualNoteOns + qualNoteOffs + qualPoly + qualChan);
        ctx.exciterActive = sSynth.hasActiveExciter();
        ctx.modelChange = modelChangeReq;
        ctx.synthReset = synthResetReq;
        ctx.telemetryPublished = (sTelemetryDivider == 0);
        ctx.midiDispatchUs = static_cast<uint32_t>(tqAfterMidi - tqStart);
        ctx.renderUs = static_cast<uint32_t>(tqAfterRender - tqAfterMidi);
        (void)tqStart;
    }
#endif
}

// UI Task - Runs strictly on Core 1 at ~30 Hz
void uiTaskLoop(void* param) {
#if defined(POCKETPAN_TAIL_NO_UI) && POCKETPAN_TAIL_NO_UI
    // M6.3.5 Phase C, diagnostic variant U2 only: the UI task goes inactive
    // after startup.  Audio and BLE remain fully operational; the dedicated
    // low-rate logger task below still emits the forensics results.
    (void)param;
    vTaskSuspend(nullptr);
#endif
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
    uint32_t renderTick = 0;

    // D Kurd / D Celtic Handpan scale notes: D3, A3, Bb3, C4, D4, E4, F4, A4
    const uint8_t kHandpanScale[] = { 50, 57, 58, 60, 62, 64, 65, 69 };
    const size_t kScaleLen = sizeof(kHandpanScale) / sizeof(kHandpanScale[0]);
    size_t scaleIdx = 0;

    while (true) {
        bool btnPressed = (gpio_get_level(static_cast<gpio_num_t>(pocketpan::board::ui::kBootButtonGpio)) == 0);
#if defined(POCKETPAN_BUTTON_QUAL) && POCKETPAN_BUTTON_QUAL
        static uint32_t sQualTick = 0;
        sQualTick++;
        if (sQualTick > 90) { // Start ~3s after boot
            const uint32_t rel = sQualTick - 90;
            // 1. Three short presses (120ms press = 4 ticks, 300ms release = 10 ticks)
            if (rel >= 1 && rel <= 4) btnPressed = true;
            else if (rel >= 15 && rel <= 18) btnPressed = true;
            else if (rel >= 29 && rel <= 32) btnPressed = true;
            // 2. Cycle 1: PAN -> BELL (1000ms = 33 ticks, 50..83)
            else if (rel >= 50 && rel <= 83) btnPressed = true;
            // 3. Cycle 2: BELL -> TONGUE (1000ms = 33 ticks, 100..133)
            else if (rel >= 100 && rel <= 133) btnPressed = true;
            // 4. Cycle 3: TONGUE -> BOWL (1000ms = 33 ticks, 150..183)
            else if (rel >= 150 && rel <= 183) btnPressed = true;
            // 5. Cycle 4: BOWL -> PAN (1000ms = 33 ticks, 200..233)
            else if (rel >= 200 && rel <= 233) btnPressed = true;
            // 6. Extended hold test: 2500ms (75 ticks, 250..325) PAN -> BELL without repeat cycling
            else if (rel >= 250 && rel <= 325) btnPressed = true;
            // 7. Cycle 6: BELL -> TONGUE (1000ms = 33 ticks, 340..373)
            else if (rel >= 340 && rel <= 373) btnPressed = true;
            // 8. Cycle 7: TONGUE -> BOWL (1000ms = 33 ticks, 390..423)
            else if (rel >= 390 && rel <= 423) btnPressed = true;
            // 9. Cycle 8: BOWL -> PAN (1000ms = 33 ticks, 440..473)
            else if (rel >= 440 && rel <= 473) btnPressed = true;

            static pocketpan::dsp::InstrumentModel sPrevModel = pocketpan::dsp::InstrumentModel::Pan;
            static pocketpan::ui::UiScreenMode sPrevMode = pocketpan::ui::UiScreenMode::Status;
            const auto curModel = selectedInstrumentModel();
            if (curModel != sPrevModel) {
                ESP_LOGI(kTag, "[BOOT_QUAL] TRANSITION: %s -> %s (label='%s' mode=%d rel_tick=%u)",
                         pocketpan::dsp::instrumentModelName(sPrevModel),
                         pocketpan::dsp::instrumentModelName(curModel),
                         sUiState.presetName, static_cast<int>(sUiState.mode), (unsigned)rel);
                sPrevModel = curModel;
            }
            if (sUiState.mode != sPrevMode) {
                ESP_LOGI(kTag, "[BOOT_QUAL] SCREEN_MODE: %d -> %d (model=%s rel_tick=%u)",
                         static_cast<int>(sPrevMode), static_cast<int>(sUiState.mode),
                         pocketpan::dsp::instrumentModelName(curModel), (unsigned)rel);
                sPrevMode = sUiState.mode;
            }
            if (rel == 500) {
                ESP_LOGI(kTag, "[BOOT_QUAL] COMPLETE: 3 short presses + 7 long cycles verified");
            }
        }
#endif
        const uint32_t nowMs = static_cast<uint32_t>(xTaskGetTickCount() * portTICK_PERIOD_MS);
        if (btnPressed && lastBootBtnState) { pressStartMs = nowMs; longPressHandled = false; }
        if (btnPressed && !longPressHandled && nowMs - pressStartMs >= 800) {
            const auto next = pocketpan::dsp::nextInstrumentModel(selectedInstrumentModel());
            requestInstrumentModel(next);
            // A model switch must be audible, never hidden behind diagnostic playback.
            sDiagnosticTone.setTone(pocketpan::dsp::DiagnosticTone::Pan);
            sUiState.mode = pocketpan::ui::UiScreenMode::Status;
            snprintf(sUiState.presetName, sizeof(sUiState.presetName), "%s", pocketpan::dsp::instrumentModelName(next));
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

#if defined(POCKETPAN_UI_TIMING) && POCKETPAN_UI_TIMING
        const int64_t tState0 = esp_timer_get_time();
#endif

        // 2. Read lock-free telemetry snapshot published from Core 0
        pocketpan::ui::AudioTelemetrySnapshot snap;
        if (sTelemetryPub.read(snap)) {
            sUiState.activeVoices = snap.activeVoices;
            sUiState.cpuLoadPercent = snap.cpuLoadPercent;
            sUiState.avgBlockTimeUs = snap.avgBlockTimeUs;
            sUiState.p99BlockTimeUs = snap.p99BlockTimeUs;
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
#ifdef CONFIG_POCKETPAN_POLYPHONY_FORENSICS
        pocketpan::forensics::ready.store(POCKETPAN_FORENSICS_DISCONNECTED
            ? !sBleMidi.isConnected() : sBleMidi.isMidiReady(), std::memory_order_release);
        pocketpan::forensics::logCompleted();
#endif
#if !(defined(POCKETPAN_UI_POLL_SEPARATE) && POCKETPAN_UI_POLL_SEPARATE)
        sBleMidi.poll(); // low-rate BLE RSSI request; never called by audio task
#endif
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

#if defined(POCKETPAN_ACTIVE_SOAK) && POCKETPAN_ACTIVE_SOAK
        // M6.3.9.1 Phase E: automated realistic musical soak workload
        pocketpan::diag::gActiveSoakGenerator.tick(
            sDemoMidiQueue,
            sSelectedInstrumentModel,
            sModelChangeRequested,
            sUiState.presetName,
            sizeof(sUiState.presetName)
        );
#else
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
#endif

#if defined(POCKETPAN_UI_TIMING) && POCKETPAN_UI_TIMING
        const int64_t tState1 = esp_timer_get_time();
#endif

        // 4. Render TFT display.  M6.3.6 Phase A U1b (diagnostic only): the
        // control/telemetry/BLE logic above still runs at the same cadence, but
        // framebuffer drawing and Display::update() are skipped so the tail can
        // be attributed to rendering rather than to task scheduling.
#if !(defined(POCKETPAN_UI_LOGIC_ONLY) && POCKETPAN_UI_LOGIC_ONLY)
        // M6.3.6 Phase C (§35): control/telemetry/BLE stay at the 30 Hz loop
        // cadence; the visual redraw can run at a lower fixed cadence without
        // coupling input latency to the framebuffer cost.
#if defined(POCKETPAN_UI_URGENT) && POCKETPAN_UI_URGENT
        // M6.3.7 Phase D: immediate redraw on urgent (musical/connection/screen)
        // changes, otherwise at most one redraw per telemetry period.
        if (renderer.shouldRender(sUiState, nowMs, POCKETPAN_UI_TELEMETRY_PERIOD_MS))
            renderer.render(sUiState);
#elif defined(POCKETPAN_UI_RENDER_DIVIDER) && POCKETPAN_UI_RENDER_DIVIDER > 1
        if ((++renderTick % POCKETPAN_UI_RENDER_DIVIDER) == 0)
            renderer.render(sUiState);
#else
        renderer.render(sUiState);
#endif
#else
        (void)renderer;
#endif

#if defined(POCKETPAN_UI_TIMING) && POCKETPAN_UI_TIMING
        {
            const int64_t tRender1 = esp_timer_get_time();
            const uint32_t stateUs = static_cast<uint32_t>(tState1 - tState0);
            const uint32_t renderUs = static_cast<uint32_t>(tRender1 - tState1);
            sUiTiming.stateSum += stateUs;
            sUiTiming.renderSum += renderUs;
            ++sUiTiming.count;
            if (stateUs > sUiTiming.stateMax) sUiTiming.stateMax = stateUs;
            if (renderUs > sUiTiming.renderMax) sUiTiming.renderMax = renderUs;
#if !(defined(POCKETPAN_UI_LOGIC_ONLY) && POCKETPAN_UI_LOGIC_ONLY)
            const uint32_t fbUs = renderer.lastFramebufferUs();
            const uint32_t lcdUs = renderer.lastLcdUs();
            sUiTiming.fbSum += fbUs;
            sUiTiming.lcdSum += lcdUs;
            if (fbUs > sUiTiming.fbMax) sUiTiming.fbMax = fbUs;
            if (lcdUs > sUiTiming.lcdMax) sUiTiming.lcdMax = lcdUs;
#endif
        }
#endif

#ifdef CONFIG_POCKETPAN_HARDWARE_QUALIFICATION_LOG
        if (nowMs - lastLogMs >= 5000) {
            lastLogMs = nowMs;
#if defined(POCKETPAN_TRANSIENT_QUAL) && POCKETPAN_TRANSIENT_QUAL
            // M6.3.9.2 lifetime qualification counters and per-overrun records.
            // These are intentionally never reset on PAN/BELL model switches.
            pocketpan::diag::transient::logNewOverruns();
            pocketpan::diag::transient::logSummary();
#endif
#if defined(POCKETPAN_UI_DIRTY) && POCKETPAN_UI_DIRTY
            ESP_LOGI(kTag, "[UI] dirty_render=%u dirty_skip=%u",
                     (unsigned)renderer.dirtyRenderCountForTest(),
                     (unsigned)renderer.dirtySkipCountForTest());
#endif
            ESP_LOGI(kTag, "[AUDIO] model=%s blocks=%u avg_us=%u p99_us=%u max_us=%u cpu_load=%.1f deadline=%u timeout=%u tx_error=%u short=%u",
                     pocketpan::dsp::instrumentModelName(selectedInstrumentModel()), (unsigned)sAudio.getStats().blocksProcessed, (unsigned)sUiState.avgBlockTimeUs, (unsigned)sUiState.p99BlockTimeUs, (unsigned)sUiState.maxBlockTimeUs, sUiState.cpuLoadPercent,
                     (unsigned)sUiState.deadlineMisses, (unsigned)sUiState.writeTimeouts, (unsigned)sUiState.txErrors, (unsigned)sUiState.shortWrites);
            ESP_LOGI(kTag, "[MIDI] push=%u pop=%u drop=%u hwm=%u last=(n=%u v=%u t=%u hex=%02X%02X%02X)",
                     (unsigned)sUiState.midiPushCount, (unsigned)sUiState.midiPopCount, (unsigned)sUiState.midiDrops, (unsigned)sUiState.midiHighWater,
                     (unsigned)sUiState.lastNoteNumber, (unsigned)sUiState.lastVelocity, (unsigned)sUiState.lastEventType[0],
                     sUiState.lastRawBytes[0], sUiState.lastRawBytes[1], sUiState.lastRawBytes[2]);
            ESP_LOGI(kTag, "[BLE] state=%u interval_ms=%.2f latency=%u rssi=%d reconnects=%u last_disconnect=%u", (unsigned)bleState, sUiState.bleIntervalUnits * 1.25f, (unsigned)sUiState.bleLatency, sUiState.bleRssi, (unsigned)sUiState.bleReconnects, sUiState.bleLastDisconnectReason);
            ESP_LOGI(kTag, "[MEM] internal_free=%u largest_internal=%u", (unsigned)sUiState.internalHeapFree, (unsigned)sUiState.largestInternalBlock);
#if defined(POCKETPAN_UI_AUDIO_CORRELATION) && POCKETPAN_UI_AUDIO_CORRELATION
            {
                auto& corr = pocketpan::diag::gUiAudioCorrelation;
                ESP_LOGI(kTag, "[UICORR] >1733(tot=%u draw=%u lcd=%u both=%u none=%u) >2000(tot=%u draw=%u lcd=%u both=%u none=%u)",
                         (unsigned)corr.over1733.total.load(std::memory_order_relaxed),
                         (unsigned)corr.over1733.drawingOnly.load(std::memory_order_relaxed),
                         (unsigned)corr.over1733.lcdOnly.load(std::memory_order_relaxed),
                         (unsigned)corr.over1733.both.load(std::memory_order_relaxed),
                         (unsigned)corr.over1733.neither.load(std::memory_order_relaxed),
                         (unsigned)corr.over2000.total.load(std::memory_order_relaxed),
                         (unsigned)corr.over2000.drawingOnly.load(std::memory_order_relaxed),
                         (unsigned)corr.over2000.lcdOnly.load(std::memory_order_relaxed),
                         (unsigned)corr.over2000.both.load(std::memory_order_relaxed),
                         (unsigned)corr.over2000.neither.load(std::memory_order_relaxed));
                ESP_LOGI(kTag, "[UIMETRIC] arch=%d field_updates=%u total_transfers=%u total_pixels=%llu last_sub_us=%u last_dma_us=%u",
                         POCKETPAN_UI_ARCH, (unsigned)renderer.fieldUpdateCount(),
                         (unsigned)sDisplay.totalTransferCount(),
                         (unsigned long long)sDisplay.totalPixelsTransferred(),
                         (unsigned)sDisplay.lastSubmitDurationUs(),
                         (unsigned)sDisplay.lastTransferDurationUs());
            }
#endif
#if defined(POCKETPAN_RARE_STALL_FORENSICS) && POCKETPAN_RARE_STALL_FORENSICS
            {
                pocketpan::diag::RareStallRecord stalls[4];
                size_t nStalls = pocketpan::diag::gRareStallForensics.copyRecords(stalls, 4);
                for (size_t i = 0; i < nStalls; ++i) {
                    ESP_LOGI(kTag, "[RARESTALL] block=%u dur=%uus voices=%u model=%s uiDraw=%d lcdDma=%d ble=%u telem=%d midiAge=%ums",
                             (unsigned)stalls[i].audioBlockSequence, (unsigned)stalls[i].renderDurationUs,
                             stalls[i].activeVoices, pocketpan::dsp::instrumentModelName(static_cast<pocketpan::dsp::InstrumentModel>(stalls[i].model)),
                             stalls[i].uiDrawing, stalls[i].lcdTransferActive,
                             stalls[i].bleState, stalls[i].telemetryPublish,
                             (unsigned)stalls[i].lastMidiAgeMs);
                }
            }
#endif
#if defined(POCKETPAN_UI_URGENT) && POCKETPAN_UI_URGENT
            ESP_LOGI(kTag, "[UIREDRAW] urgent=%u telemetry=%u skip=%u",
                     (unsigned)renderer.urgentRenderCountForTest(),
                     (unsigned)renderer.telemetryRenderCountForTest(),
                     (unsigned)renderer.skipRenderCountForTest());
#endif
#if defined(POCKETPAN_UI_TIMING) && POCKETPAN_UI_TIMING
            if (sUiTiming.count) {
                ESP_LOGI(kTag, "[UITIME] n=%u state_avg=%.1f state_max=%u render_avg=%.1f render_max=%u fb_avg=%.1f fb_max=%u lcd_avg=%.1f lcd_max=%u",
                         (unsigned)sUiTiming.count,
                         double(sUiTiming.stateSum) / sUiTiming.count, (unsigned)sUiTiming.stateMax,
                         double(sUiTiming.renderSum) / sUiTiming.count, (unsigned)sUiTiming.renderMax,
                         double(sUiTiming.fbSum) / sUiTiming.count, (unsigned)sUiTiming.fbMax,
                         double(sUiTiming.lcdSum) / sUiTiming.count, (unsigned)sUiTiming.lcdMax);
                sUiTiming = UiTiming{};
            }
#endif
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

#ifdef CONFIG_POCKETPAN_POLYPHONY_FORENSICS
    ESP_LOGI(kTag, "[FORENSICS] candidate=%d profile=%d cpu_mhz=%d optimization_perf=%d process6=%d fine_bins=%d critical_only=%d",
             POCKETPAN_DSP_CANDIDATE,
#ifdef CONFIG_POCKETPAN_DSP_PROFILE
             1,
#else
             0,
#endif
             CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ,
#ifdef CONFIG_COMPILER_OPTIMIZATION_PERF
             1,
#else
             0,
#endif
             POCKETPAN_PROCESS6_MICROKERNEL, POCKETPAN_FORENSICS_FINE_BINS,
             POCKETPAN_FORENSICS_CRITICAL_ONLY);
#endif
    // 1. Initialize DSP Engine (48kHz, 8 voices, PAN preset)
    sSynth.init(static_cast<float>(pocketpan::board::audio::kSampleRate));
#if defined(POCKETPAN_TONGUE_SMOKE) && POCKETPAN_TONGUE_SMOKE
    sSynth.setInstrumentModel(pocketpan::dsp::InstrumentModel::Tongue);
#elif defined(POCKETPAN_BOWL_SMOKE) && POCKETPAN_BOWL_SMOKE
    sSynth.setInstrumentModel(pocketpan::dsp::InstrumentModel::Bowl);
#endif

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
#ifdef CONFIG_POCKETPAN_POLYPHONY_FORENSICS
    pocketpan::forensics::setAudioInstance(&sAudio);
#endif

    // 4. Initialize Display (after audio claims GDMA, before BLE allocates internal RAM)
    if (!sDisplay.init()) {
        ESP_LOGE(kTag, "Failed to initialize ST7789 display");
    } else {
#if defined(POCKETPAN_LCD_BENCHMARK) && POCKETPAN_LCD_BENCHMARK
        sDisplay.benchmarkTransfers();
#endif
    }
#if POCKETPAN_PREPARED_NOTE_CACHE
    ESP_LOGI(kTag, "[CANARY] PreparedNote canaries OK=%d", sSynth.verifyPreparedNoteCanaries());
#endif

    // 5. Initialize BLE MIDI Central on Core 1
    if (!sBleMidi.begin()) {
        ESP_LOGW(kTag, "BLE MIDI Central failed to start advertising/scanning");
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

#if defined(POCKETPAN_TAIL_NO_UI) && POCKETPAN_TAIL_NO_UI
#if defined(CONFIG_POCKETPAN_POLYPHONY_FORENSICS)
    xTaskCreatePinnedToCore(forensicsLogTask, "fx_log", 8192, nullptr, 4, nullptr, 1);
#endif
#endif

#if defined(POCKETPAN_UI_POLL_SEPARATE) && POCKETPAN_UI_POLL_SEPARATE
    xTaskCreatePinnedToCore(blePollTask, "ble_poll", 4096, nullptr,
                            pocketpan::board::ui::kTaskPriority, nullptr, 1);
#endif

    ESP_LOGI(kTag, "Pocket Pan initialization complete. Realtime audio active.");
}