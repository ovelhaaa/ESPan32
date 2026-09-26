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

# Accepted default: fixed kernels plus bounded IRAM placement. Baseline 0,
# fixed-only 4 and IRAM-only 5 remain available for controlled A/B builds.
set(POCKETPAN_DSP_CANDIDATE 9 CACHE STRING "M6.3.1 kernel: 0 baseline, 4 fixed, 5 IRAM, 9 fixed + IRAM")
set(POCKETPAN_FORENSICS_CRITICAL_ONLY 0 CACHE STRING "Measure only the three failing fixtures")
set(POCKETPAN_FORENSICS_DISCONNECTED 0 CACHE STRING "Diagnostic BLE scan without connecting")
target_compile_definitions(${COMPONENT_LIB} PRIVATE
    POCKETPAN_DSP_CANDIDATE=${POCKETPAN_DSP_CANDIDATE}
    POCKETPAN_FORENSICS_CRITICAL_ONLY=${POCKETPAN_FORENSICS_CRITICAL_ONLY}
    POCKETPAN_FORENSICS_DISCONNECTED=${POCKETPAN_FORENSICS_DISCONNECTED})


set_property(CACHE POCKETPAN_DSP_CANDIDATE PROPERTY STRINGS 0 4 5 9)
