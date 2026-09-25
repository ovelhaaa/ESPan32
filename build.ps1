# Pocket Pan - Portable Build, Test, Flash, and Monitor Script
param(
    [ValidateSet("build", "flash", "monitor", "test", "clean")]
    [string]$Action = "build",
    [string]$Port = "COM3"
)

$ErrorActionPreference = "Stop"
$ScriptDir = $PSScriptRoot
Set-Location $ScriptDir

# Helper to verify or setup ESP-IDF environment
function Ensure-IdfEnv {
    if (Get-Command "idf.py" -ErrorAction SilentlyContinue) {
        return
    }

    # Try resolving from IDF_PATH
    if ($env:IDF_PATH -and (Test-Path "$env:IDF_PATH\export.ps1")) {
        Write-Host "Activating ESP-IDF from $env:IDF_PATH..." -ForegroundColor Yellow
        . "$env:IDF_PATH\export.ps1"
        return
    }

    # Try standard user home directories without hardcoded usernames
    $commonIdfPaths = @(
        "$HOME\esp\esp-idf",
        "$HOME\esp-idf",
        "C:\esp\esp-idf"
    )

    foreach ($candidate in $commonIdfPaths) {
        if (Test-Path "$candidate\export.ps1") {
            Write-Host "Found ESP-IDF at $candidate. Activating..." -ForegroundColor Yellow
            $env:IDF_PATH = $candidate
            . "$candidate\export.ps1"
            return
        }
    }

    Write-Error @"
[ERROR] 'idf.py' is not found in PATH and ESP-IDF could not be auto-detected.
Please activate your ESP-IDF environment first, for example:
    . <path-to-esp-idf>\export.ps1
Or set the IDF_PATH environment variable before running this script.
"@
}

# Helper to find a C++ host compiler
function Get-HostCompiler {
    if (Get-Command "clang++" -ErrorAction SilentlyContinue) {
        return "clang++"
    }
    if (Get-Command "g++" -ErrorAction SilentlyContinue) {
        return "g++"
    }
    Write-Error "[ERROR] No host C++ compiler (clang++ or g++) found in PATH for desktop testing."
}

switch ($Action) {
    "test" {
        $cxx = Get-HostCompiler
        Write-Host "=== Running Desktop Tests using $cxx ===" -ForegroundColor Cyan

        # 1. DSP Unit Tests
        Write-Host "--> Compiling and running DSP Unit Tests..." -ForegroundColor Green
        & $cxx -O3 -std=c++17 "$ScriptDir/tests/test_dsp.cpp" `
            "$ScriptDir/main/dsp/modal_resonator.cpp" `
            "$ScriptDir/main/dsp/body_resonator.cpp" `
            "$ScriptDir/main/dsp/exciter.cpp" `
            "$ScriptDir/main/dsp/modal_voice.cpp" `
            "$ScriptDir/main/dsp/voice_allocator.cpp" `
            "$ScriptDir/main/dsp/peak_limiter.cpp" `
            "$ScriptDir/main/dsp/synth_engine.cpp" `
            "$ScriptDir/main/midi/midi_mapping.cpp" `
            "$ScriptDir/main/midi/midi_parser.cpp" `
            -I "$ScriptDir/main" -o "$ScriptDir/tests/test_dsp.exe"
        if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
        & "$ScriptDir/tests/test_dsp.exe"
        if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

        # 2. BLE MIDI Parser Unit Tests
        Write-Host "--> Compiling and running BLE-MIDI Parser Unit Tests..." -ForegroundColor Green
        & $cxx -O3 -std=c++17 "$ScriptDir/tests/test_ble_midi_parser.cpp" `
            "$ScriptDir/main/midi/midi_parser.cpp" `
            -I "$ScriptDir/main" -o "$ScriptDir/tests/test_ble_midi_parser.exe"
        if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
        & "$ScriptDir/tests/test_ble_midi_parser.exe"
        if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

        Write-Host "=== All Desktop Unit Tests Passed Successfully! ===" -ForegroundColor Green
    }
    "clean" {
        Ensure-IdfEnv
        Write-Host "=== Cleaning Build Directory ===" -ForegroundColor Yellow
        idf.py fullclean
    }
    "build" {
        Ensure-IdfEnv
        Write-Host "=== Building Pocket Pan Firmware for TENSTAR TS-ESP32-S3 ===" -ForegroundColor Cyan
        idf.py build
    }
    "flash" {
        Ensure-IdfEnv
        Write-Host "=== Flashing Firmware to $Port ===" -ForegroundColor Cyan
        idf.py -p $Port flash
    }
    "monitor" {
        Ensure-IdfEnv
        Write-Host "=== Opening Serial Monitor on $Port ===" -ForegroundColor Cyan
        idf.py -p $Port monitor
    }
}
