idf_component_register(
    SRCS
        "app/app_main.cpp"
        "hardware/audio_i2s.cpp"
        "hardware/display.cpp"
        "hardware/ble_midi.cpp"
        "dsp/modal_resonator.cpp"
        "dsp/body_resonator.cpp"
        "dsp/exciter.cpp"
        "dsp/modal_voice.cpp"
        "dsp/voice_allocator.cpp"
        "dsp/peak_limiter.cpp"
        "dsp/synth_engine.cpp"
        "dsp/instrument_model.cpp"
        "midi/midi_parser.cpp"
        "midi/midi_mapping.cpp"
        "ui/ui_renderer.cpp"
    INCLUDE_DIRS
        "."
        "app"
        "hardware"
        "dsp"
        "midi"
        "ui"
    PRIV_REQUIRES
        driver
        esp_driver_gpio
        esp_driver_i2s
        esp_lcd
        freertos
        esp_timer
        bt
        nvs_flash
        heap
)

target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)

# M6.3.4 promotes candidate 16 (M6.3.2 microkernel + PreparedNote cache plus the
# stable-sustain voice fast path) as the connected-validated baseline, then
# promotes candidate 20 (16 + accepted T4 tail-IRAM placement) as the default:
# it keeps the exact output and adds a reproducible ~6-9% steady p99 reduction
# with no average regression.  The scalar baseline (0), D+E (9),
# microkernel-only (11), PreparedNote-only (12), M6.3.2 baseline (13),
# sustain-only (14) and the tail/attack experiments (17-21) remain available for
# controlled A/B and rollback.
set(POCKETPAN_DSP_CANDIDATE 20 CACHE STRING "DSP candidates: 0 scalar, 4 fixed, 5 IRAM, 9 D+E, 11 microkernel, 12 PreparedNote, 13 both, 14 +sustain, 15/16 +sustain, 17-19 tail IRAM, 20 tail IRAM, 21 +attack, 22 +coeff-cache, 23 +stable8, 24 +attack-voice, 25 combo")
set(POCKETPAN_FORENSICS_CRITICAL_ONLY 0 CACHE STRING "Measure only the three failing fixtures")
set(POCKETPAN_FORENSICS_DISCONNECTED 0 CACHE STRING "Diagnostic BLE scan without connecting")
target_compile_definitions(${COMPONENT_LIB} PRIVATE
    POCKETPAN_DSP_CANDIDATE=${POCKETPAN_DSP_CANDIDATE}
    POCKETPAN_FORENSICS_CRITICAL_ONLY=${POCKETPAN_FORENSICS_CRITICAL_ONLY}
    POCKETPAN_FORENSICS_DISCONNECTED=${POCKETPAN_FORENSICS_DISCONNECTED})


set_property(CACHE POCKETPAN_DSP_CANDIDATE PROPERTY STRINGS 0 4 5 9 11 12 13 14 15 16 17 18 19 20 21 22 23 24 25)
