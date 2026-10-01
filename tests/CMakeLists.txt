cmake_minimum_required(VERSION 3.16)
project(ESPan32HostTests LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
enable_testing()
set(ROOT ${CMAKE_CURRENT_LIST_DIR}/..)
add_executable(test_ble_midi_parser test_ble_midi_parser.cpp ${ROOT}/main/midi/midi_parser.cpp)
target_include_directories(test_ble_midi_parser PRIVATE ${ROOT}/main)
add_test(NAME ble_midi_parser COMMAND test_ble_midi_parser)
add_executable(test_dsp test_dsp.cpp
 ${ROOT}/main/dsp/modal_resonator.cpp ${ROOT}/main/dsp/body_resonator.cpp ${ROOT}/main/dsp/exciter.cpp ${ROOT}/main/dsp/modal_voice.cpp
 ${ROOT}/main/dsp/voice_allocator.cpp ${ROOT}/main/dsp/synth_engine.cpp ${ROOT}/main/dsp/instrument_model.cpp ${ROOT}/main/midi/midi_mapping.cpp)
target_sources(test_dsp PRIVATE ${ROOT}/main/dsp/peak_limiter.cpp)
target_include_directories(test_dsp PRIVATE ${ROOT}/main)
set(POCKETPAN_DSP_CANDIDATE 25 CACHE STRING "DSP candidates: 0 scalar, 4 fixed, 5 IRAM, 9 D+E, 11 microkernel, 12 PreparedNote, 13 both, 14 +sustain, 15/16 +sustain, 17-19 tail IRAM, 20 tail IRAM, 21 +attack, 22 +coeff-cache, 23 +stable8, 24 +attack-voice, 25 combo, 26 +headroom-table, 27 +velocity-LUT, 28 +register-precompute, 29 trigger-combo")
# M7.0.2 controlled A/B: 1 = production unrolled 6-mode microkernel, 0 = exact
# scalar reference fallback for the Tongue bank.  The host oracles must be
# bit-exact for both.
set(POCKETPAN_PROCESS6_MICROKERNEL 1 CACHE STRING "M7.0.2: enable the unrolled 6-mode modal microkernel")
add_compile_definitions(POCKETPAN_PROCESS6_MICROKERNEL=${POCKETPAN_PROCESS6_MICROKERNEL})
target_compile_definitions(test_dsp PRIVATE POCKETPAN_DSP_CANDIDATE=${POCKETPAN_DSP_CANDIDATE})
add_test(NAME dsp COMMAND test_dsp)
add_executable(test_prepared_note test_prepared_note.cpp
 ${ROOT}/main/dsp/modal_resonator.cpp ${ROOT}/main/dsp/body_resonator.cpp ${ROOT}/main/dsp/exciter.cpp ${ROOT}/main/dsp/modal_voice.cpp
 ${ROOT}/main/dsp/voice_allocator.cpp ${ROOT}/main/dsp/synth_engine.cpp ${ROOT}/main/dsp/instrument_model.cpp ${ROOT}/main/midi/midi_mapping.cpp)
target_sources(test_prepared_note PRIVATE ${ROOT}/main/dsp/peak_limiter.cpp)
target_include_directories(test_prepared_note PRIVATE ${ROOT}/main)
target_compile_definitions(test_prepared_note PRIVATE POCKETPAN_DSP_CANDIDATE=${POCKETPAN_DSP_CANDIDATE})
add_test(NAME prepared_note COMMAND test_prepared_note)
add_executable(test_telemetry test_telemetry.cpp)
target_include_directories(test_telemetry PRIVATE ${ROOT}/main)
target_link_libraries(test_telemetry PRIVATE pthread)
add_test(NAME telemetry COMMAND test_telemetry)
add_executable(test_modal_kernel test_modal_kernel.cpp ${ROOT}/main/dsp/modal_resonator.cpp)
target_include_directories(test_modal_kernel PRIVATE ${ROOT}/main)
target_compile_definitions(test_modal_kernel PRIVATE
  POCKETPAN_DSP_CANDIDATE=${POCKETPAN_DSP_CANDIDATE}
  POCKETPAN_MODAL_DIFFERENTIAL_TEST=1)
add_test(NAME modal_kernel COMMAND test_modal_kernel ${CMAKE_CURRENT_LIST_DIR}/m631_modal_kernel_hashes.txt)

add_executable(test_fastpath test_fastpath.cpp
 ${ROOT}/main/dsp/modal_resonator.cpp ${ROOT}/main/dsp/body_resonator.cpp ${ROOT}/main/dsp/exciter.cpp ${ROOT}/main/dsp/modal_voice.cpp
 ${ROOT}/main/dsp/voice_allocator.cpp ${ROOT}/main/dsp/synth_engine.cpp ${ROOT}/main/dsp/instrument_model.cpp ${ROOT}/main/midi/midi_mapping.cpp)
target_sources(test_fastpath PRIVATE ${ROOT}/main/dsp/peak_limiter.cpp)
target_include_directories(test_fastpath PRIVATE ${ROOT}/main)
target_compile_definitions(test_fastpath PRIVATE POCKETPAN_DSP_CANDIDATE=${POCKETPAN_DSP_CANDIDATE})
add_test(NAME fastpath COMMAND test_fastpath)

add_executable(test_exciter_fastpath test_exciter_fastpath.cpp ${ROOT}/main/dsp/exciter.cpp)
target_include_directories(test_exciter_fastpath PRIVATE ${ROOT}/main)
target_compile_definitions(test_exciter_fastpath PRIVATE
  POCKETPAN_DSP_CANDIDATE=${POCKETPAN_DSP_CANDIDATE}
  POCKETPAN_EXCITER_DIFFERENTIAL_TEST=1)
add_test(NAME exciter_fastpath COMMAND test_exciter_fastpath)

add_executable(test_m635_fastpath test_m635_fastpath.cpp
 ${ROOT}/main/dsp/modal_resonator.cpp ${ROOT}/main/dsp/body_resonator.cpp ${ROOT}/main/dsp/exciter.cpp ${ROOT}/main/dsp/modal_voice.cpp
 ${ROOT}/main/dsp/voice_allocator.cpp ${ROOT}/main/dsp/synth_engine.cpp ${ROOT}/main/dsp/instrument_model.cpp ${ROOT}/main/midi/midi_mapping.cpp)
target_sources(test_m635_fastpath PRIVATE ${ROOT}/main/dsp/peak_limiter.cpp)
target_include_directories(test_m635_fastpath PRIVATE ${ROOT}/main)
target_compile_definitions(test_m635_fastpath PRIVATE POCKETPAN_DSP_CANDIDATE=${POCKETPAN_DSP_CANDIDATE})
add_test(NAME m635_fastpath COMMAND test_m635_fastpath)

add_executable(test_m637_trigger test_m637_trigger.cpp
 ${ROOT}/main/dsp/modal_resonator.cpp ${ROOT}/main/dsp/exciter.cpp ${ROOT}/main/dsp/modal_voice.cpp
 ${ROOT}/main/dsp/instrument_model.cpp ${ROOT}/main/midi/midi_mapping.cpp)
target_include_directories(test_m637_trigger PRIVATE ${ROOT}/main)
target_compile_definitions(test_m637_trigger PRIVATE
  POCKETPAN_DSP_CANDIDATE=${POCKETPAN_DSP_CANDIDATE}
  POCKETPAN_TRIGGER_DIFFERENTIAL_TEST=1)
add_test(NAME m637_trigger COMMAND test_m637_trigger)

# These tests intentionally use assert for compact host fixtures; keep the
# checks enabled even in the Release configuration used by CI.
foreach(target test_ble_midi_parser test_dsp test_prepared_note test_telemetry test_modal_kernel test_fastpath test_exciter_fastpath test_m635_fastpath test_m637_trigger)
  target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -UNDEBUG)
endforeach()

set_property(CACHE POCKETPAN_DSP_CANDIDATE PROPERTY STRINGS 0 4 5 9 11 12 13 14 15 16 17 18 19 20 21 22 23 24 25 26 27 28 29)

add_executable(test_vibraphone_runtime test_vibraphone_runtime.cpp
 ${ROOT}/main/dsp/modal_resonator.cpp ${ROOT}/main/dsp/body_resonator.cpp
 ${ROOT}/main/dsp/exciter.cpp ${ROOT}/main/dsp/modal_voice.cpp
 ${ROOT}/main/dsp/voice_allocator.cpp ${ROOT}/main/dsp/synth_engine.cpp
 ${ROOT}/main/dsp/instrument_model.cpp ${ROOT}/main/dsp/peak_limiter.cpp
 ${ROOT}/main/midi/midi_mapping.cpp)
target_include_directories(test_vibraphone_runtime PRIVATE ${ROOT}/main)
target_compile_definitions(test_vibraphone_runtime PRIVATE POCKETPAN_DSP_CANDIDATE=${POCKETPAN_DSP_CANDIDATE})
target_compile_options(test_vibraphone_runtime PRIVATE -Wall -Wextra -Wpedantic -UNDEBUG)
add_test(NAME vibraphone_runtime COMMAND test_vibraphone_runtime)

add_executable(test_mbira test_mbira.cpp
 ${ROOT}/main/dsp/modal_resonator.cpp ${ROOT}/main/dsp/body_resonator.cpp
 ${ROOT}/main/dsp/exciter.cpp ${ROOT}/main/dsp/modal_voice.cpp
 ${ROOT}/main/dsp/voice_allocator.cpp ${ROOT}/main/dsp/synth_engine.cpp
 ${ROOT}/main/dsp/instrument_model.cpp ${ROOT}/main/dsp/peak_limiter.cpp
 ${ROOT}/main/midi/midi_mapping.cpp)
target_include_directories(test_mbira PRIVATE ${ROOT}/main)
target_compile_definitions(test_mbira PRIVATE POCKETPAN_DSP_CANDIDATE=${POCKETPAN_DSP_CANDIDATE})
target_compile_options(test_mbira PRIVATE -Wall -Wextra -Wpedantic -UNDEBUG)
add_test(NAME mbira COMMAND test_mbira)

add_executable(test_udu test_udu.cpp
 ${ROOT}/main/dsp/modal_resonator.cpp ${ROOT}/main/dsp/body_resonator.cpp
 ${ROOT}/main/dsp/exciter.cpp ${ROOT}/main/dsp/modal_voice.cpp
 ${ROOT}/main/dsp/voice_allocator.cpp ${ROOT}/main/dsp/synth_engine.cpp
 ${ROOT}/main/dsp/instrument_model.cpp ${ROOT}/main/dsp/peak_limiter.cpp
 ${ROOT}/main/midi/midi_mapping.cpp)
target_include_directories(test_udu PRIVATE ${ROOT}/main)
target_compile_definitions(test_udu PRIVATE POCKETPAN_DSP_CANDIDATE=${POCKETPAN_DSP_CANDIDATE})
target_compile_options(test_udu PRIVATE -Wall -Wextra -Wpedantic -UNDEBUG)
add_test(NAME udu COMMAND test_udu)

add_executable(test_udu_dynamic test_udu_dynamic.cpp)
get_target_property(UDU_SOURCES test_udu SOURCES)
list(REMOVE_ITEM UDU_SOURCES test_udu.cpp)
target_sources(test_udu_dynamic PRIVATE ${UDU_SOURCES})
target_include_directories(test_udu_dynamic PRIVATE ${ROOT}/main)
target_compile_definitions(test_udu_dynamic PRIVATE POCKETPAN_DSP_CANDIDATE=${POCKETPAN_DSP_CANDIDATE})
target_compile_options(test_udu_dynamic PRIVATE -Wall -Wextra -Wpedantic -UNDEBUG)
add_test(NAME udu_dynamic COMMAND test_udu_dynamic)
