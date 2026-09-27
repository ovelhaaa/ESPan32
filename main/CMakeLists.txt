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

# M6.3.5 connected qualification promotes candidate 25 (20 + sympathetic
# coefficient cache + PAN stable-8 sustain path + attack-voice fast path) as the
# default: host-exact, I/O 0/0/0, and connected PAN cluster8 average -7.0% /
# p99 -6.6% / max -10.9% with event average -11.4% and CPU 66.7% -> 62.2%.
# Every earlier candidate (0, 4, 5, 9, 11, 12, 13, 14, 16, 17-24) remains
# selectable for controlled A/B and rollback.  See
# docs/m635_pan_attack_tail_isolation.md and docs/hardware/m635_candidate_comparison.md.
set(POCKETPAN_DSP_CANDIDATE 25 CACHE STRING "DSP candidates: 0 scalar, 4 fixed, 5 IRAM, 9 D+E, 11 microkernel, 12 PreparedNote, 13 both, 14 +sustain, 15/16 +sustain, 17-19 tail IRAM, 20 tail IRAM, 21 +attack, 22 +coeff-cache, 23 +stable8, 24 +attack-voice, 25 combo")
set(POCKETPAN_FORENSICS_CRITICAL_ONLY 0 CACHE STRING "Measure only the three failing fixtures")
set(POCKETPAN_FORENSICS_DISCONNECTED 0 CACHE STRING "Diagnostic BLE scan without connecting")
# M6.3.5 Phase C tail-source isolation (diagnostic builds only; 0 in production).
set(POCKETPAN_TAIL_NO_LCD_TRANSFER 0 CACHE STRING "U1: keep UI/framebuffer, skip LCD bitmap transfer")
set(POCKETPAN_TAIL_NO_UI 0 CACHE STRING "U2: suspend UI task; audio+BLE only")
target_compile_definitions(${COMPONENT_LIB} PRIVATE
    POCKETPAN_DSP_CANDIDATE=${POCKETPAN_DSP_CANDIDATE}
    POCKETPAN_FORENSICS_CRITICAL_ONLY=${POCKETPAN_FORENSICS_CRITICAL_ONLY}
    POCKETPAN_FORENSICS_DISCONNECTED=${POCKETPAN_FORENSICS_DISCONNECTED}
    POCKETPAN_TAIL_NO_LCD_TRANSFER=${POCKETPAN_TAIL_NO_LCD_TRANSFER}
    POCKETPAN_TAIL_NO_UI=${POCKETPAN_TAIL_NO_UI})


set_property(CACHE POCKETPAN_DSP_CANDIDATE PROPERTY STRINGS 0 4 5 9 11 12 13 14 15 16 17 18 19 20 21 22 23 24 25)
