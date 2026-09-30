# M7.4 — MARIMBA listening candidate

Status: implementation and host qualification complete; listening decision OPEN.
A is the provisional runtime configuration. No candidate was selected by metrics or frozen.

## Implementation

Enum value 6, Count 7. BOOT long press cycles PAN / BELL / TONGUE / BOWL /
KALIMBA / GLASS / MARIMBA / PAN. Production short-press handling is unchanged.
Diagnostic button injection covers two full cycles and an extended hold. The preset
rectangle/tile is widened to 96 pixels to fit MARIMBA at scale 2.

One shared PreparedNote table is prepared before I2S starts, with boundary canaries.
Selection swaps the canonical table pointer; arbitrary host configs bypass the cache.
Six modes reuse Process6Safety (existing safety enabled), scalar fallback, mode fade and Nyquist pruning.
No new per-sample kernel, generic exciter parameter, realtime heap work, coefficient
calculation, or logging was added. Existing allocator and limiter calibration are unchanged.
Host SynthEngine size: 75,888 bytes, formerly 66,536 (+9,352 for table/canary).
Seven PreparedNote tables total 65,436 bytes.

## Modal configuration

| Mode | Ratio | Preset gain A/B | Base T60 seconds |
|---|---:|---:|---:|
| Fundamental | 1.000 | 1.000 | 2.400 |
| Second bar mode | 2.750 | .380 | .850 |
| Third mode | 5.400 | .200 | .380 |
| Fourth | 8.930 | .090 | .180 |
| Fifth | 13.300 | .040 | .090 |
| Sixth | 18.600 | .015 | .045 |

Detune/splitting zero. Resonator gain 1, damping depth .95, safety enabled.
Register position is logarithmic from 146.83 to 587.33 Hz. T60 multiplier
1.25 to .80 gives fundamental T60 approximately 3.00/2.46/1.92 seconds at D3/D4/D5.
Register gain 1.04 to .94; brightness 1 to .78. No per-note lookup table.
Soft coupling [1, .42, .18, .06, .02, .005]; hard [1, .92, .72, .48, .28, .12].
Upper velocity factor .12 to .85; hardness .12 to .65; silence threshold 1e-8.
Preset gains are multiplied by the existing coupling and register behavior.

## Exciter and A/B/C body investigation

Existing Strike: gain .20, noise .12, brightness 650–6500 Hz, velocity knee .85,
knee slope .38. Velocity controls amplitude and brightness. An initial .72 gain
caused roughly 10 dB cluster limiting; reducing MARIMBA gain across all candidates
removed that activity without changing shared calibration or selecting a winner.

- A: warm modal bar only; body and sympathetic OFF. Provisional firmware preset.
- B: same bar with subtle body, StrikeBus gain 16; modes (Hz, T60, gain)
  (210, .10, .30), (420, .065, .18); excitation .06, output .05, lowpass 900 Hz.
- C: upper gains ×1.18, exciter ceiling 8000 Hz, B body with output .035.
  Gains [1, .4484, .236, .1062, .0472, .0177]. Ratios/T60 unchanged.

B/C body reinforcement is a listening experiment; its usefulness is undecided.

## Host results and listening pack

Process6 ON and OFF: 9/9 suites pass. Prepared/unprepared PCM agrees for MIDI
24–96 × velocities 30/70/110/127, plus fallback, pressure, restrike and model switching.
MARIMBA fast/reference comparisons cover single, chord, cluster and roll.
Finite voice sample/energy observations are asserted each block; modal-bank float
output is checked each sample in register/Nyquist probes. PCM is integer-valued.
These observations do not claim exhaustive physical float-sample inspection.

27 fixtures × 3 candidates × raw/matched = 162 WAVs, six seconds each,
48 kHz stereo 16-bit PCM. Generated in the test working directory and copied to
`listening/m74`. All fixture metrics are retained in `qualification/m74_host_metrics.md`.
Reports include peak, RMS, linear crest ratio, pre-limiter peak, max/average GR,
GR threshold counts, clamps, saturation, maximum voices, steals/tails and nonfinite
observations. All candidates have zero limiter activity, clamps, modal saturation,
and nonfinite diagnostic observations. Steal burst reaches 8 voices, 4 steals, one tail.

Required names (each A/B/C and *_matched.wav):

- marimba_D3_v70_A/B/C.wav
- marimba_D4_v70_A/B/C.wav
- marimba_D4_v110_A/B/C.wav
- marimba_roll_A/B/C.wav
- marimba_chord_A/B/C.wav

Each trio shares its minimum RMS target, avoiding clipping on normalization.
All 162 file checksums were verified; maximum PCM RMS difference within a matched
trio is below 1e-6. See qualification/m74_wav_manifest.json.
Other files cover D3/A3/D4/A4/D5 at v30/70/110/127, cluster8, restrike100,
restrike250, roll_steady and steal_burst. Rolls have exact 70/100/120 ms intervals,
24 strikes, steady v90 and varying v40/65/90/115. No note-offs truncate decay.

`qualification/m74_spectra.json` retains register/velocity spectra. RMS increases
at every velocity step for every candidate/register. Fundamental FFT-bin error
is below 2 cents (1 Hz resolution). Upper/fundamental attack energy rises at
every velocity step across all registers; upper modes sum to at most .127 of fundamental
energy in this analysis. The fixed >2 kHz brightness proxy is unsuitable at D3,
where meaningful bar modes are below 2 kHz and its tiny values reach PCM quantization noise. D4 A attack mode-2/fundamental energy increases
approximately .022 to .076; mode 3 is weaker. D3–D5 retain six modes; MIDI96/108/127
retain 4/3/1. Metrics support technical plausibility, not a listening verdict.

Reproduce:

```powershell
cmake --build build-host -j 4
ctest --test-dir build-host --output-on-failure
cmake --build build-host-p6off -j 4
ctest --test-dir build-host-p6off --output-on-failure
python tests/analyze_m74.py build-host docs/qualification/m74_spectra.json
```

## Existing instrument regression

Captured before editing and asserted afterward, 292 exact cases per model:

| Model | Aggregate FNV-1a |
|---|---|
| PAN | 9e9801244165101d |
| BELL | 7c2dc6f37fb99f4b |
| TONGUE | 83f75567fb47ebaf |
| BOWL | 14772092e35bc4a7 |
| KALIMBA | e9bcabdd0b6e3bd2 |
| GLASS | 14e0dd6ba566e1cf |

All previous outputs remain bit-identical on this host/toolchain. Baseline/current
PreparedNote logs are retained in `qualification`.

## Hardware

M74 forensics measures single/chord4/cluster8/roll, IDs 0/1/2/3, 8192 blocks each,
Process6 ON, 5 us histogram bins, phase profiling OFF. FIXTURE_IO records avg/p99/max,
CPU load, deadlines and I2S errors/timeouts; CURVE and CALLBACK_CURVE distinguish
steady/attack/event/transition timing. Actual ESP32-S3 rev v0.2 on COM10, 240 MHz, 48 kHz/128 frames,
Candidate 25, Process6 ON, BLE disconnected (central scanning), UI and I2S active.
Raw evidence: [m74_esp32s3_raw.log](qualification/m74_esp32s3_raw.log), 16/16 class rows.

| Fixture | Avg block us | p99 us | Max us | CPU % | Deadline misses | I2S timeouts/errors/short |
|---|---:|---:|---:|---:|---:|---|
| single | 266 | 375 | 2776 | 9.97 | 1 | 0/0/0 |
| chord4 | 627 | 900 | 1545 | 23.51 | 0 | 0/0/0 |
| cluster8 | 1096 | 1625 | 2228 | 41.09 | 0 | 0/0/0 |
| roll | 279 | 750 | 1177 | 10.46 | 0 | 0/0/0 |

The one single-fixture miss is the first model-selection callback (2776 us):
inner selection/render 2556.26 us plus 219.74 us callback overhead. No playing
steady/attack/event callback missed a deadline in any fixture. This is an open
selection-boundary concern; the physical run is not an unconditional performance pass.
No I2S timeout/error/short write, hard clamp, saturation or wrong voice count occurred.
Cluster steady inner avg/p99/max: 1066.77/1180/1326 us; event: 1975/2145/2143 us.
Do not substitute historical Glass timing for MARIMBA. BLE-connected qualification remains pending.
During forensics, periodic AUDIO model labels follow UI selection (PAN); CURVE/FIXTURE_IO
identify the actual diagnostic synthesis model correctly as MARIMBA.

Reproduce with an isolated SDKCONFIG containing the production settings plus
CONFIG_POCKETPAN_POLYPHONY_FORENSICS=y, CONFIG_POCKETPAN_FORENSICS_BLOCKS=8192,
CONFIG_POCKETPAN_HARDWARE_QUALIFICATION_LOG=y and phase profiling disabled:

```powershell
idf.py -B build-m74-qual -D SDKCONFIG=<isolated-config-path> -D POCKETPAN_FORENSICS_M74=1 -D POCKETPAN_FORENSICS_FINE_BINS=5 -D POCKETPAN_FORENSICS_DISCONNECTED=1 build
# Flash COM10, then capture verbatim (output path must not exist):
python tests/capture_forensics.py docs/qualification/new_m74_raw.log --port COM10 --seconds 115 --rows 16 --reset
```


## BOOT qualification on the board

[m74_boot_raw.log](qualification/m74_boot_raw.log) records automated button injection
through the actual firmware handler: 14 long-press transitions complete two seven-model
cycles, including GLASS -> MARIMBA -> PAN twice with label MARIMBA. The extended hold
produces exactly one further transition. Short presses enter MIDI then Audio diagnostics
while the selected model remains PAN; the third press advances the existing diagnostic tone.
The log reports completion after the full schedule, not before its final cycle.
Internal free SRAM stays 95,783 bytes throughout and the initial PreparedNote canaries pass.
This checks the handler and label state on hardware; manual tactile/display visual inspection
remains pending. Normal production firmware is restored after diagnostics, booting into PAN.

## Before freeze

Listen to raw and RMS-matched A/B/C, especially bass and rolls. Decide whether body
improves wood character or adds box resonance; confirm upper modes avoid both sine-like
percussion and metallic wash. Check soft-strike audibility on the actual output with
the conservative .20 strike gain. Manual BOOT/display inspection and BLE-connected timing remain
qualification requirements before final V1 freeze.

## Files changed

- `main/dsp/instrument_model.h`, `instrument_model.cpp`: enum, cycle, label, registry/config.
- `main/dsp/modal_preset.h`: six wooden-bar modes.
- `main/dsp/synth_engine.h`, `synth_engine.cpp`: prepared table, initialization, pointer selection, canaries.
- `main/CMakeLists.txt`: MARIMBA smoke and M74 forensics options.
- `main/app/app_main.cpp`: smoke selection and seven-model BOOT diagnostic schedule.
- `main/app/polyphony_forensics.h`: four MARIMBA fixtures and retained I2S summaries.
- `main/diag/transient_qual.h`: generic instrument-name formatter handles MARIMBA.
- `main/ui/ui_renderer.cpp`: wider preset tile/rectangle.
- `tests/test_dsp.cpp`: deterministic candidate renders, metrics, safety and WAVs.
- `tests/test_prepared_note.cpp`: MARIMBA cache/pressure/fallback/switch coverage and prior-model hash guards.
- `tests/test_fastpath.cpp`: MARIMBA single/chord/cluster/roll comparisons.
- `tests/analyze_m74.py`: pitch/modal/velocity spectral qualification, no scoring.
- `docs/m74_marimba.md`, `docs/qualification/m74_*`, `listening/m74/README.md`: report/evidence/pack guide.

WAVs remain local ignored generated artifacts. No effects, sequencing or MIDI-layout changes.
