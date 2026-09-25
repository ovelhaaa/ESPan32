# CMake generated Testfile for 
# Source directory: C:/progs/ESPan/tests
# Build directory: C:/progs/ESPan/build-host-m5d
# 
# This file includes the relevant testing commands required for 
# testing this directory and lists subdirectories to be tested as well.
add_test(ble_midi_parser "C:/progs/ESPan/build-host-m5d/test_ble_midi_parser.exe")
set_tests_properties(ble_midi_parser PROPERTIES  _BACKTRACE_TRIPLES "C:/progs/ESPan/tests/CMakeLists.txt;9;add_test;C:/progs/ESPan/tests/CMakeLists.txt;0;")
add_test(dsp "C:/progs/ESPan/build-host-m5d/test_dsp.exe")
set_tests_properties(dsp PROPERTIES  _BACKTRACE_TRIPLES "C:/progs/ESPan/tests/CMakeLists.txt;15;add_test;C:/progs/ESPan/tests/CMakeLists.txt;0;")
add_test(telemetry "C:/progs/ESPan/build-host-m5d/test_telemetry.exe")
set_tests_properties(telemetry PROPERTIES  _BACKTRACE_TRIPLES "C:/progs/ESPan/tests/CMakeLists.txt;19;add_test;C:/progs/ESPan/tests/CMakeLists.txt;0;")
