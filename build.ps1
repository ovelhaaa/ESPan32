# Pocket Pan - Build and Test Script for Windows (PowerShell)
param(
    [ValidateSet("build", "flash", "monitor", "test", "clean")]
    [string]$Action = "build",
    [string]$Port = "COM3"
)

$ErrorActionPreference = "Stop"

$ScriptDir = $PSScriptRoot
Set-Location $ScriptDir

# Environment paths
$IdfPath = if ($env:IDF_PATH) { $env:IDF_PATH } else { "C:\Users\devx\esp\esp-idf" }
$PythonEnv = "C:\Users\devx\.espressif\python_env\idf5.3_py3.14_env"
$XtensaBin = "C:\Users\devx\.espressif\tools\xtensa-esp-elf\esp-13.2.0_20240530\xtensa-esp-elf\bin"
$CMakeBin = "C:\Users\devx\.espressif\tools\cmake\3.24.0\bin"
$NinjaBin = "C:\Users\devx\.espressif\tools\ninja\1.11.1"
$IdfExe = "C:\Users\devx\.espressif\tools\idf-exe\1.0.3"

$env:IDF_PATH = $IdfPath
$env:IDF_PYTHON_ENV_PATH = $PythonEnv
$env:PATH = "$XtensaBin;$CMakeBin;$NinjaBin;$IdfExe;$PythonEnv\Scripts;$IdfPath\tools;" + $env:PATH

switch ($Action) {
    "test" {
        Write-Host "=== Compiling and Running Desktop DSP Unit Tests ===" -ForegroundColor Cyan
        & clang++ -O3 -std=c++17 "$ScriptDir/tests/test_dsp.cpp" "$ScriptDir/main/dsp/modal_resonator.cpp" "$ScriptDir/main/dsp/exciter.cpp" "$ScriptDir/main/dsp/modal_voice.cpp" "$ScriptDir/main/dsp/voice_allocator.cpp" "$ScriptDir/main/dsp/synth_engine.cpp" "$ScriptDir/main/midi/midi_mapping.cpp" "$ScriptDir/main/midi/midi_parser.cpp" -I "$ScriptDir/main" -o "$ScriptDir/tests/test_dsp.exe"
        & "$ScriptDir/tests/test_dsp.exe" | Out-Host
    }
    "clean" {
        Write-Host "=== Cleaning Build Directory ===" -ForegroundColor Yellow
        idf.py fullclean
    }
    "build" {
        Write-Host "=== Building Pocket Pan Firmware for TENSTAR TS-ESP32-S3 ===" -ForegroundColor Cyan
        idf.py build
    }
    "flash" {
        Write-Host "=== Flashing Firmware to $Port ===" -ForegroundColor Cyan
        idf.py -p $Port flash
    }
    "monitor" {
        Write-Host "=== Opening Serial Monitor on $Port ===" -ForegroundColor Cyan
        idf.py -p $Port monitor
    }
}
