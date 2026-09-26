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

# Isolated experiments: 0 is the unchanged baseline. Changing these cache
# values rebuilds only this component; production defaults remain unchanged.
set(POCKETPAN_DSP_CANDIDATE 0 CACHE STRING "Isolated M6.3.1 DSP experiment")
set(POCKETPAN_FORENSICS_CRITICAL_ONLY 0 CACHE STRING "Measure only the three failing fixtures")
target_compile_definitions(${COMPONENT_LIB} PRIVATE
    POCKETPAN_DSP_CANDIDATE=${POCKETPAN_DSP_CANDIDATE}
    POCKETPAN_FORENSICS_CRITICAL_ONLY=${POCKETPAN_FORENSICS_CRITICAL_ONLY})

if(POCKETPAN_DSP_CANDIDATE STREQUAL "8")
    # Separate -O3 experiment; never enable fast-math or change global flags.
    set_source_files_properties(
        dsp/modal_resonator.cpp dsp/modal_voice.cpp dsp/voice_allocator.cpp
        dsp/synth_engine.cpp dsp/exciter.cpp dsp/body_resonator.cpp dsp/peak_limiter.cpp
        PROPERTIES COMPILE_OPTIONS "-O3")
endif()
