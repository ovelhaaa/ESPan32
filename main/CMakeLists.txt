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
        "diag"
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
set(POCKETPAN_FORENSICS_M76 0 CACHE STRING "M7.6 Mbira connected qualification")
set(POCKETPAN_FORENSICS_M77 0 CACHE STRING "M7.7 Udu connected qualification")
set(POCKETPAN_FORENSICS_M77_BATCH 0 CACHE STRING "Udu: 0 single/chord/cluster, 1 groove/switch")
target_compile_definitions(${COMPONENT_LIB} PRIVATE POCKETPAN_FORENSICS_M77=${POCKETPAN_FORENSICS_M77}
    POCKETPAN_FORENSICS_M77_BATCH=${POCKETPAN_FORENSICS_M77_BATCH})
set(POCKETPAN_FORENSICS_M76_BATCH 0 CACHE STRING "M7.6 qualification batch: 0 single/chord/cluster, 1 roll/groove/switch, 2 buzz-on single")
target_compile_definitions(${COMPONENT_LIB} PRIVATE POCKETPAN_FORENSICS_M76=${POCKETPAN_FORENSICS_M76}
    POCKETPAN_FORENSICS_M76_BATCH=${POCKETPAN_FORENSICS_M76_BATCH})

# M6.3.5 connected qualification promotes candidate 25 (20 + sympathetic
# coefficient cache + PAN stable-8 sustain path + attack-voice fast path) as the
# default: host-exact, I/O 0/0/0, and connected PAN cluster8 average -7.0% /
# p99 -6.6% / max -10.9% with event average -11.4% and CPU 66.7% -> 62.2%.
# Every earlier candidate (0, 4, 5, 9, 11, 12, 13, 14, 16, 17-24) remains
# selectable for controlled A/B and rollback.  See
# docs/m635_pan_attack_tail_isolation.md and docs/hardware/m635_candidate_comparison.md.
set(POCKETPAN_DSP_CANDIDATE 25 CACHE STRING "DSP candidates: 0 scalar, 4 fixed, 5 IRAM, 9 D+E, 11 microkernel, 12 PreparedNote, 13 both, 14 +sustain, 15/16 +sustain, 17-19 tail IRAM, 20 tail IRAM, 21 +attack, 22 +coeff-cache, 23 +stable8, 24 +attack-voice, 25 combo, 26 +headroom-table, 27 +velocity-LUT, 28 +register-precompute, 29 trigger-combo")
set(POCKETPAN_FORENSICS_CRITICAL_ONLY 0 CACHE STRING "Measure only the three failing fixtures")
set(POCKETPAN_FORENSICS_DISCONNECTED 0 CACHE STRING "Diagnostic BLE scan without connecting")
# M6.3.5 Phase C tail-source isolation (diagnostic builds only; 0 in production).
set(POCKETPAN_TAIL_NO_LCD_TRANSFER 0 CACHE STRING "U1: keep UI/framebuffer, skip LCD bitmap transfer")
set(POCKETPAN_TAIL_NO_UI 0 CACHE STRING "U2: suspend UI task; audio+BLE only")
# M6.3.6 Phase A UI scheduling isolation (diagnostic builds only; 0 in
# production).  Each variant keeps the UI task cadence and BLE behavior
# identical; only the work performed inside the loop changes.
set(POCKETPAN_UI_LOGIC_ONLY 0 CACHE STRING "U1b: keep UI/control/BLE logic cadence, skip renderer.render() and LCD update")
set(POCKETPAN_UI_NO_FULL_CLEAR 0 CACHE STRING "U3: keep rendering+LCD, skip unconditional full framebuffer clear (dirty-ish diagnostic)")
set(POCKETPAN_UI_DIRTY 0 CACHE STRING "M6.3.6 production candidate: event-driven dirty redraw (guarded, default off)")
# M6.3.7 adopts the urgent/telemetry redraw policy as production: musical and
# connection changes redraw immediately; telemetry-only changes redraw at most
# every 200 ms.  This closes the connected event deadline gate (0 misses) with
# no steady regression and an always-responsive control loop.
set(POCKETPAN_UI_POLL_SEPARATE 0 CACHE STRING "BLE-poll-on-dedicated-low-rate-task diagnostic")
set(POCKETPAN_UI_RENDER_DIVIDER 1 CACHE STRING "Render every Nth 30Hz UI loop (1 = full rate)")
set(POCKETPAN_UI_PARTIAL 1 CACHE STRING "M6.3.7 production: partial Status redraw + row-band transfer")
# M6.3.7 Phase D urgent/telemetry redraw policy (production default on).
set(POCKETPAN_UI_URGENT 1 CACHE STRING "M6.3.7: immediate redraw on urgent change, telemetry redraw at period")
set(POCKETPAN_UI_TELEMETRY_PERIOD_MS 200 CACHE STRING "Telemetry redraw period (ms) when only telemetry changed")
# M6.3.7 Phase A Core-1 workload instrumentation (diagnostic only, default off).
set(POCKETPAN_UI_TIMING 0 CACHE STRING "M6.3.7: log Core-1 state/format/render/LCD microseconds")
# Fine forensics histogram for M6.3.6: 5 us bins around the gate (default off).
set(POCKETPAN_FORENSICS_FINE_BINS 0 CACHE STRING "0=25us bins (legacy), 5=5us bins, 10=10us bins")
# M6.3.8 UI architecture: 0=F0 full fb + row bands, 1=F1 full fb + true rect, 2=F2 tile scratch + rect
set(POCKETPAN_UI_ARCH 0 CACHE STRING "0=F0 row band, 1=F1 full fb + true rect, 2=F2 tile scratch + rect")
set(POCKETPAN_UI_PSRAM 0 CACHE STRING "M6.3.8 F3 experiment: full framebuffer in PSRAM")
set(POCKETPAN_RARE_STALL_FORENSICS 0 CACHE STRING "M6.3.8 Phase I: rare stall ring buffer capture")
set(POCKETPAN_UI_AUDIO_CORRELATION 0 CACHE STRING "M6.3.8 UI/audio timing correlation diagnostics")
set(POCKETPAN_LCD_BENCHMARK 0 CACHE STRING "M6.3.8 LCD transfer benchmark on boot")
set(POCKETPAN_ACTIVE_SOAK 0 CACHE STRING "M6.3.9.1 Phase E: automated realistic musical soak workload")
# M6.3.9.2 transient burst closure diagnostics: lifetime overrun counters,
# consecutive-overrun streaks, timing debt, DMA headroom occupancy and
# per-overrun musical-transition classification.  Diagnostic builds only.
set(POCKETPAN_TRANSIENT_QUAL 0 CACHE STRING "M6.3.9.2: transient burst closure qualification instrumentation")
set(POCKETPAN_FORENSICS_M7 0 CACHE STRING "M7 qualification fixtures: PAN cluster8, BELL cluster8, TONGUE chord4, TONGUE cluster8")
# M7.0.2 controlled A/B: 1 keeps the unrolled 6-mode microkernel (production),
# 0 routes the Tongue 6-mode bank through the exact scalar reference recurrence.
set(POCKETPAN_PROCESS6_MICROKERNEL 1 CACHE STRING "M7.0.2: enable the unrolled 6-mode modal microkernel (0 = exact scalar fallback)")
set(POCKETPAN_BUTTON_QUAL 0 CACHE STRING "M7 BOOT button automated cycle qualification")
set(POCKETPAN_TONGUE_SMOKE 0 CACHE STRING "Boot directly into Tongue model for smoke qualification")
set(POCKETPAN_BOWL_SMOKE 0 CACHE STRING "Boot directly into Bowl model for smoke qualification")
set(POCKETPAN_KALIMBA_SMOKE 0 CACHE STRING "Boot directly into Kalimba model for smoke qualification")
set(POCKETPAN_MARIMBA_SMOKE 0 CACHE STRING "Boot directly into Marimba model")
set(POCKETPAN_FORENSICS_M75_BATCH 0 CACHE STRING "M7.5.1 batch: 0 single OFF/chord ON/cluster ON, 1 phrase ON/switch/single ON")
set(POCKETPAN_FORENSICS_M75 0 CACHE STRING "Vibraphone six-fixture hardware qualification")
set(POCKETPAN_VIBRAPHONE_SMOKE 0 CACHE STRING "Boot directly into Vibraphone")
set(POCKETPAN_FORENSICS_M74 0 CACHE STRING "Marimba single/chord/cluster/roll hardware qualification")
set(POCKETPAN_GLASS_SMOKE 0 CACHE STRING "Boot directly into Glass model for smoke qualification")

target_compile_definitions(${COMPONENT_LIB} PRIVATE
    POCKETPAN_DSP_CANDIDATE=${POCKETPAN_DSP_CANDIDATE}
    POCKETPAN_FORENSICS_CRITICAL_ONLY=${POCKETPAN_FORENSICS_CRITICAL_ONLY}
    POCKETPAN_FORENSICS_DISCONNECTED=${POCKETPAN_FORENSICS_DISCONNECTED}
    POCKETPAN_TAIL_NO_LCD_TRANSFER=${POCKETPAN_TAIL_NO_LCD_TRANSFER}
    POCKETPAN_TAIL_NO_UI=${POCKETPAN_TAIL_NO_UI}
    POCKETPAN_UI_LOGIC_ONLY=${POCKETPAN_UI_LOGIC_ONLY}
    POCKETPAN_UI_NO_FULL_CLEAR=${POCKETPAN_UI_NO_FULL_CLEAR}
    POCKETPAN_UI_DIRTY=${POCKETPAN_UI_DIRTY}
    POCKETPAN_UI_POLL_SEPARATE=${POCKETPAN_UI_POLL_SEPARATE}
    POCKETPAN_UI_RENDER_DIVIDER=${POCKETPAN_UI_RENDER_DIVIDER}
    POCKETPAN_UI_PARTIAL=${POCKETPAN_UI_PARTIAL}
    POCKETPAN_UI_URGENT=${POCKETPAN_UI_URGENT}
    POCKETPAN_UI_TELEMETRY_PERIOD_MS=${POCKETPAN_UI_TELEMETRY_PERIOD_MS}
    POCKETPAN_UI_TIMING=${POCKETPAN_UI_TIMING}
    POCKETPAN_FORENSICS_FINE_BINS=${POCKETPAN_FORENSICS_FINE_BINS}
    POCKETPAN_UI_ARCH=${POCKETPAN_UI_ARCH}
    POCKETPAN_UI_PSRAM=${POCKETPAN_UI_PSRAM}
    POCKETPAN_RARE_STALL_FORENSICS=${POCKETPAN_RARE_STALL_FORENSICS}
    POCKETPAN_UI_AUDIO_CORRELATION=${POCKETPAN_UI_AUDIO_CORRELATION}
    POCKETPAN_LCD_BENCHMARK=${POCKETPAN_LCD_BENCHMARK}
    POCKETPAN_ACTIVE_SOAK=${POCKETPAN_ACTIVE_SOAK}
    POCKETPAN_TRANSIENT_QUAL=${POCKETPAN_TRANSIENT_QUAL}
    POCKETPAN_FORENSICS_M7=${POCKETPAN_FORENSICS_M7}
    POCKETPAN_BUTTON_QUAL=${POCKETPAN_BUTTON_QUAL}
    POCKETPAN_TONGUE_SMOKE=${POCKETPAN_TONGUE_SMOKE}
    POCKETPAN_BOWL_SMOKE=${POCKETPAN_BOWL_SMOKE}
    POCKETPAN_KALIMBA_SMOKE=${POCKETPAN_KALIMBA_SMOKE}
    POCKETPAN_GLASS_SMOKE=${POCKETPAN_GLASS_SMOKE}
    POCKETPAN_MARIMBA_SMOKE=${POCKETPAN_MARIMBA_SMOKE}
    POCKETPAN_FORENSICS_M75_BATCH=${POCKETPAN_FORENSICS_M75_BATCH}
    POCKETPAN_FORENSICS_M75=${POCKETPAN_FORENSICS_M75}
    POCKETPAN_VIBRAPHONE_SMOKE=${POCKETPAN_VIBRAPHONE_SMOKE}
    POCKETPAN_FORENSICS_M74=${POCKETPAN_FORENSICS_M74}
    POCKETPAN_PROCESS6_MICROKERNEL=${POCKETPAN_PROCESS6_MICROKERNEL})

set_property(CACHE POCKETPAN_DSP_CANDIDATE PROPERTY STRINGS 0 4 5 9 11 12 13 14 15 16 17 18 19 20 21 22 23 24 25 26 27 28 29)
