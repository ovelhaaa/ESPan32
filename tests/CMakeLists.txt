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
 ${ROOT}/main/dsp/modal_resonator.cpp ${ROOT}/main/dsp/exciter.cpp ${ROOT}/main/dsp/modal_voice.cpp
 ${ROOT}/main/dsp/voice_allocator.cpp ${ROOT}/main/dsp/synth_engine.cpp ${ROOT}/main/midi/midi_mapping.cpp)
target_sources(test_dsp PRIVATE ${ROOT}/main/dsp/peak_limiter.cpp)
target_include_directories(test_dsp PRIVATE ${ROOT}/main)
add_test(NAME dsp COMMAND test_dsp)
add_executable(test_telemetry test_telemetry.cpp)
target_include_directories(test_telemetry PRIVATE ${ROOT}/main)
target_link_libraries(test_telemetry PRIVATE pthread)
add_test(NAME telemetry COMMAND test_telemetry)

# These tests intentionally use assert for compact host fixtures; keep the
# checks enabled even in the Release configuration used by CI.
foreach(target test_ble_midi_parser test_dsp test_telemetry)
  target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -UNDEBUG)
endforeach()
