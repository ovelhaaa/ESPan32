# M7.5 — VIBRAPHONE listening candidate

Historical milestone snapshot. Production is now [frozen VIBRAPHONE V1: BAR=C, MOTOR/TUBE=M1](m753_vibraphone_v1_freeze.md).

Status: implementation and host qualification complete; listening selection and V1 freeze OPEN. All seven hardware timing fixtures pass both disconnected and BLE MIDI connected. No candidate was selected automatically. The runtime uses provisional A with the motor ON.

## Implementation and files

`main/dsp/instrument_model.{h,cpp}` adds enum value 7, Count 8, config, name and the cycle PAN / BELL / TONGUE / BOWL / KALIMBA / GLASS / MARIMBA / VIBRAPHONE / PAN. BOOT handling uses the existing registry; short-press logic is unchanged. `main/ui/ui_renderer.cpp` expands the existing label tile to 124 pixels (120 pixels of text), ending before the root-note field at x140.

`main/dsp/modal_preset.h` adds the six-mode bank. `synth_engine.{h,cpp}` prepares the eighth canonical table before I2S, checks its canary, implements a global motor, precomputes canonical body states, retains fixed sample-rate infrastructure across selection, and counts nonfinite mix observations. `modal_voice.cpp`, `modal_resonator.{h,cpp}` and `exciter.h` eliminate discarded selection-time modal coefficients and explicitly restore the deterministic noise seed and saturation diagnostics at a model boundary.

`main/app/app_main.cpp`, `polyphony_forensics.h`, and `main/CMakeLists.txt` add instrument smoke, two bounded forensic batches and eight-model button qualification. `tests/test_dsp.cpp`, `test_prepared_note.cpp`, `test_fastpath.cpp`, `test_vibraphone_runtime.cpp`, `tests/CMakeLists.txt`, `analyze_m75.py` and `collect_m75.py` provide safety, cache/reference, golden hashes, switching, global phase, allocation, timing, spectral and listening-pack verification.

## Acoustic model

Research: [Beaton et al., prototype vibraphone bar measurements](https://pubs.aip.org/asa/jel/article/2/8/083201/2842502/Experimental-measurements-of-a-prototype) supports traditional tuning of the first three vertical flexural modes to 1:4:10. [Three-dimensional bar tuning](https://pubmed.ncbi.nlm.nih.gov/34241415/) describes untuned modes varying across the keyboard. The last three modes here are weak modeling approximations, not measured frequencies for a specific instrument. No doublets.

| Mode | Ratio | A gain | Base T60 s |
|---|---:|---:|---:|
| Fundamental | 1 | 1.000 | 5.50 |
| Tuned flexural | 4 | .620 | 3.00 |
| Tuned flexural | 10 | .200 | 1.25 |
| Upper colour | 16 | .065 | .65 |
| Upper colour | 22.4 | .025 | .32 |
| Upper transient | 29 | .010 | .16 |

Detune zero. Resonator master gain 1, damping depth .95, existing safety enabled. Register uses log frequency between 146.83 and 587.33 Hz; T60 scales 1.20 to .80, giving fundamental approximately 6.60/5.50/4.40 s at D3/D4/D5. Register gain 1.02 to .94, brightness 1 to .85. Soft coupling [1,.55,.24,.08,.025,.008], hard [1,.95,.72,.40,.20,.08]. Upper velocity blend .20 to .90. Hardness .10 to .62. Silence threshold 1e-8. Actual excitation gains combine the preset, coupling and register behavior.

The generic Strike exciter uses gain .16, noise .075, brightness bounds 800–9500 Hz, velocity knee .85 and slope .38. Compared with MARIMBA the hardness range is slightly softer and noise lower. Its rounded half-sine impulse and filtered 3–8 ms noise reuse existing machinery. Velocity controls amplitude, hardness, cutoff and upper-mode coupling. The configured upper cutoff is a bound; effective cutoff includes squared hardness and register brightness.

## Bar/body candidates — motor OFF

- A: pure/warm bar, table above; body and sympathetic OFF. Provisional runtime, no listening verdict.
- B: classic/projection experiment, 4x gain x1.35 (.837), higher gains x1.6 [.320,.104,.040,.016]; exciter upper bound 11500 Hz. Fixed generic body (Hz, gain, T60): (146.83,.32,.25), (293.66,.18,.15), excitation .08, output .12, lowpass 1100 Hz, StrikeBus gain 16.
- C: brighter bar, 4x gain x1.7 (1.054), higher gains x2.4 [.480,.156,.060,.024]; upper bound 13500 Hz, noise .10; B body with output .065. Ratios and decay unchanged.

The first C trial overemphasized the 4x mode; reducing that mode restored fundamental dominance without choosing a winner. These are substantial spectrum/exciter differences; listener evidence must decide their usefulness. Body modes are fixed frequencies: they cannot track every bar's resonator tube. This is deliberately a restrained generic-body investigation, not a faithful bank of individual tubes. Decide by listening whether B/C add useful depth or an unwanted separate pitch.

## Motor

One uint32 phase accumulator belongs to SynthEngine, advanced once per sample while VIBRAPHONE is selected, including silence and motor OFF. NoteOn/restrike does not reset it. Switching/reset clears the phase deterministically. A 257-float cosine shutter LUT is built once at startup; linear interpolation wraps via unsigned overflow. No sample-loop trigonometry, coefficients, allocation, logging, delay, pitch modulation or voice-local oscillators are added.

Output gain = 1 - depth * shutter, shutter in [0,1]. It approximates coherent instrument radiation from shutters; it is not a coupled bar/tube physical simulation. Applying it after the bar/body sum also makes the bar-only candidate usable. Modulation attenuates rather than boosting, before the existing limiter. Default 4.5 Hz/.32 depth gives gain .68–1.00 (mean .84). Qualification controls clamp rate 2–8 Hz, depth 0–.8 and reject nonfinite inputs. There is no new UI/MIDI motor control.

Three representative combinations: slow/subtle 2.5 Hz/.15, medium 4.5 Hz/.32, fast/strong 7 Hz/.55. The host runtime benchmark records seven paired trials with reversed ordering in `qualification/m75_motor_host_cost.txt`; this is host timing, not ESP32 timing. The hardware motor cost comparison uses dry single fixture 0 versus motor single fixture 6, detailed below.

## Host results and listening pack

Process6 ON and OFF each pass all 10 CTest suites. The old seven aggregate hashes are asserted; VIBRAPHONE covers MIDI 24–96 x velocities 30/70/110/127, 292 exact prepared/unprepared cases, fallback notes, pressure and restrikes. Fast/reference single/chord/cluster/roll PCM is exact. All 64 source/target instrument boundaries equal cold target selection after playing a source note. Motor output is exact across 128-frame versus 37-frame partitioning; depth zero equals dry PCM; MIDI strikes preserve global phase. Instrument selection, strikes and rendering perform zero host-observed heap allocations.

`qualification/m75_host_metrics.md` records 108 raw fixtures: 28 fixtures x three dry candidates, plus six fixtures x four motor configurations. All have zero hard clamps, modal saturation, limiter GR/counts and checked nonfinite observations. Reports include peak/RMS, attack/tail RMS, crest, pre peak, max/average GR, >.1/>1 dB counts, max voices, steals and tails. A/B/C eight-note clusters and rolls need no limiter. Voice sample/energy and mix diagnostics are checked per block; modal register probes check float output each sample. This is not an exhaustive inspection of every internal float operation.

The 60 register/velocity spectral rows in `qualification/m75_spectra.json` show strictly increasing RMS and upper/fundamental attack energy across v30/70/110/127 at D3/A3/D4/A4/D5 for every candidate. Every individual upper mode remains below fundamental attack energy. Pitch FFT error is below five cents at 1 Hz resolution. These checks establish technical plausibility, not sound quality.

`listening/m75` contains 216 eight-second, 48 kHz stereo 16-bit WAVs, raw and RMS matched. `qualification/m75_wav_manifest.json` records SHA-256, bytes, RMS and peak. The 34 comparison groups are matched independently: A/B/C groups and motor groups have their own minimum RMS targets, avoiding clipping. Maximum PCM RMS spread is below 5e-8.

Start with these matched comparisons, then check raw dynamics:

- vibraphone_D3_v70_A/B/C_matched.wav
- vibraphone_D4_v70_A/B/C_matched.wav
- vibraphone_D4_v110_A/B/C_matched.wav
- vibraphone_chord_A/B/C_matched.wav
- vibraphone_roll_A/B/C_matched.wav
- vibraphone_phrase_A/B/C_matched.wav

Motor pairs: each of D3_v70, D4_v70, D4_v110, chord, roll and phrase has `_motor_off.wav`, `_motor_on_slow_subtle.wav`, `_motor_on_medium.wav`, `_motor_on_fast_strong.wav`, and matched equivalents. Same event schedule and exciter seed across each comparison. Rolls have 24 strikes at 80/100/140 ms, steady v90 and varying v40/65/90/115. Phrase: eight sparse D-minor notes at 600 ms spacing, v65/80/95, no note-off truncation. Other fixtures include chord4, cluster8, 100/250 ms restrikes and a 12-note steal burst.

## Frozen regression hashes

| Instrument | Aggregate FNV-1a |
|---|---|
| PAN | 9e9801244165101d |
| BELL | 7c2dc6f37fb99f4b |
| TONGUE | 83f75567fb47ebaf |
| BOWL | 14772092e35bc4a7 |
| KALIMBA | e9bcabdd0b6e3bd2 |
| GLASS | 14e0dd6ba566e1cf |
| MARIMBA | 236d684f05f5f1d2 |
| VIBRAPHONE provisional | 433412365bf8f9c4 |

Previous seven match `qualification/m74_current_hashes.txt` exactly, including MARIMBA now explicitly asserted. Current ON/OFF logs are retained in `m75_current_hashes.txt` / `m75_p6off_hashes.txt`. Hash identity is toolchain/platform specific.

## Memory and switch architecture

PreparedNote 128 bytes, table 9348 bytes/model, eight tables 74784 bytes (previously 65436, +9348). Host SynthEngine 88080 bytes (previously 75888, +12192); actual Xtensa sizeof 87860. Additional state includes the motor table and eight small prepared canonical body objects. Each future canonical table adds another 9348 bytes plus its boundary canary and any model-specific state. The eighth table remains in internal storage; there is no cache redesign or PSRAM relocation.

`setInstrumentModel` resets voices/noise/body/limiter state, selects the config, swaps the prepared table pointer and installs already-prepared body coefficients. It no longer cold-initializes eight voices, body, limiter or fixed sample-rate infrastructure. Modal preset definitions are copied without calculating coefficients that trigger will replace. Arbitrary host configs still use generic coefficient generation outside rendering. Existing dynamic trigger/pressure behavior is unchanged. Constant register logs and the existing sympathetic configuration coefficient remain bounded selection work.

The initial seven-fixture forensic image exceeded DRAM by 9168 bytes due to retained histograms. Qualification therefore uses the original four-fixture SRAM budget: batch0 IDs 0–3, batch1 IDs 4–6. The same 5 us histograms, callback/inner distinction and 8192 blocks/fixture are retained; no timing diagnostic was suppressed.

## Hardware evidence

Actual ESP32-S3 rev v0.2, COM10, 240 MHz, 48 kHz / 128 frames, DSP candidate 25, Process6 ON, phase profiling OFF, UI and I2S active. Batch0 uses motor OFF for single/chord/cluster and ON for roll; batch1 motor ON for sustained phrase, model-switch probes and a matching single-note motor measurement. Each fixture has 8192 blocks. The phrase adds four distinct notes at 752 ms intervals and lets their tails overlap, with coherent phase. Switch probes perform MARIMBA -> VIBRAPHONE before the strike every 188 blocks; selection time remains inside the event/callback measurement.

Batch0 raw evidence is `qualification/m75_esp32s3_batch0_raw.log`; batch1 is `m75_esp32s3_batch1_raw.log`. The completed table is below; `qualification/m75_hardware_summary.md` and `m75_hardware_manifest.json` preserve extracted details. BLE remains scanning/disconnected during these forensic runs; connected results are appended below.

## Reproduce

```powershell
cmake --build build-host -j 4
ctest --test-dir build-host --output-on-failure
cmake --build build-host-p6off -j 4
ctest --test-dir build-host-p6off --output-on-failure
python tests/analyze_m75.py build-host docs/qualification/m75_spectra.json
python tests/collect_m75.py build-host docs/listening/m75 docs/qualification/m75_wav_manifest.json
```

Use the production SDKCONFIG plus hardware-qualification log, polyphony forensics, 8192 blocks, phase profiling OFF. Build with POCKETPAN_FORENSICS_M75=1, POCKETPAN_FORENSICS_FINE_BINS=5, POCKETPAN_FORENSICS_DISCONNECTED=1 and POCKETPAN_FORENSICS_M75_BATCH=0 / 1. Capture to fresh paths with `capture_forensics.py --port COM10 --reset --seconds 115 --rows 16` for batch0, 90 seconds / 12 rows for batch1. Production disables forensic and smoke switches; PAN remains the boot default.

## Listening decisions before V1 freeze

First assess the dry D3/D4 hard and medium strikes: pitch strength, soft attack, 4x audibility, subdued 10x colour, long metallic tail and separation from MARIMBA/GLASS/BELL. Then decide whether the fixed generic body adds depth without a separate pitch, and whether B/C remain acoustic at v127. Finally compare motor OFF and the three ON settings on chord/roll/phrase, using matched files for tone/modulation and raw files for real attenuation. Decide A/B/C and motor rate/depth by listening. No subjective convincingness or V1 freeze is claimed from metrics. Full tube coupling and audible pedal damping were outside this milestone's scope; audible quality remains a listening decision.

## Completed hardware results

| Fixture | Avg callback us | p95 us | p99 us | Max us | CPU % | Misses | I2S timeout/error/short |
|---|---:|---:|---:|---:|---:|---:|---|
| single motor OFF | 266 | 305 | 400 | 1210 | 9.97 | 0 | 0/0/0 |
| chord4 motor OFF | 632 | 695 | 900 | 1357 | 23.70 | 0 | 0/0/0 |
| cluster8 motor OFF | 1098 | 1160 | 1625 | 2193 | 41.17 | 0 | 0/0/0 |
| roll motor ON | 295 | 385 | 800 | 992 | 11.06 | 0 | 0/0/0 |
| sustained phrase motor ON | 628 | 685 | 875 | 1843 | 23.55 | 0 | 0/0/0 |
| repeated model switches | 282 | 320 | 400 | 1345 | 10.57 | 0 | 0/0/0 |
| single motor ON | 283 | 320 | 400 | 949 | 10.61 | 0 | 0/0/0 |

p95 combines the 5 us callback class histograms (8191 callback records/fixture); FIXTURE_IO p99 uses the existing transport histogram with 25 us bins. These are upper bin bounds. All 28 inner/callback class rows and seven I/O summaries pass deadlines; DSP clamp/saturation and corrected voice checks are zero. Phrase reached four voices, allowing natural expiration thereafter.

Motor steady inner avg OFF 247.90 us, ON 259.77 us: delta 11.87 us/128 frames (~0.45% of the 2666.7 us deadline). These are separate real-board runs, so the difference includes run variability; callback averages add about 17 us. The paired host medians were 3.81161 versus 3.86091 us/block (+.04930 us).

M7.4 selection boundary: callback 2776 us, inner 2556.26 us, one deadline miss. M7.5 first selection: callback 1210 us, inner 968.62 us, no miss. Repeated paired switches (44 event blocks) had max callback 1345 us including both selections, strike and render. This comparison crosses instruments (historic MARIMBA and new VIBRAPHONE); it demonstrates the boundary meets the deadline, not a precisely isolated speedup ratio.

The first batch1 capture, retained as `m75_esp32s3_batch1_initial_raw.log`, incorrectly flagged 1343 voice-count observations because it expected all four tails to remain permanently active. The final diagnostic verifies 1..notes-introduced active voices, tracks the observed maximum, and keeps exact-count checks on other fixtures. No synth audio or timing gate changed. The repeated capture passes; both logs are retained.

Startup after BLE init: batch0 internal free 60055 bytes, largest block 31744; batch1 final internal free 48019, largest 31744. These are diagnostic builds with retained histograms, before UI task creation; production memory is measured separately below.

The disconnected captures qualify scanning only (state=1, interval 0). The additional connected captures below qualify a MIDI-ready link. BLE scanning, LCD, UI and I2S were active, the disconnected flag intentionally declined discovered peripherals. Connected runs are recorded separately below.

## BOOT qualification

`qualification/m75_boot_raw.log` and `m75_boot_manifest.json` retain actual board evidence from the existing automated button injection through the normal UI handler. The 16 long presses traverse the eight-model cycle twice; one extended hold adds exactly one PAN -> BELL transition (17 total). Every reported label matches its selected model, including VIBRAPHONE. Three short presses preserve screen transitions 0 -> 1 -> 2 -> 0. Periodic AUDIO rows record zero deadline misses, I2S timeouts/errors/short writes through these boundaries. This verifies handler/label state, not a photographed pixel inspection of the LCD. Normal firmware is restored after qualification.

## Production memory and boot smoke

The normal candidate-25 firmware builds and boots with PAN default, all smoke/button/forensic flags OFF. `qualification/m75_production_smoke.log` / `m75_production_manifest.json` record actual internal SRAM after BLE startup: **79203 bytes free**, largest block **31744**. After UI startup and BLE MIDI subscription the periodic measurement is **83343 bytes free**, largest **31744**. The normal image has no externalized note cache and no memory allocation failure. PAN smoke shows zero deadline misses and I2S faults. BLE became ready at 11.25 ms connection interval, prompting a connected VIBRAPHONE qualification in addition to the disconnected logs.

## BLE MIDI connected qualification

Actual MIDI-ready connection, interval 11.25 ms, latency 0. UI/LCD/I2S active, 8192 blocks per fixture. Both captures contain CCCD subscription success and periodic state=8. All inner/callback class rows have zero deadline, voice-count, BLE-loss, clamp and saturation observations. There was no externally generated MIDI flood; the musical workload is the deterministic diagnostic fixture.

| Fixture | Avg callback us | p95 us | p99 us | Max us | CPU % | Misses | I2S timeout/error/short |
|---|---:|---:|---:|---:|---:|---:|---|
| single motor OFF | 267 | 295 | 350 | 1151 | 10.01 | 0 | 0/0/0 |
| chord4 motor OFF | 634 | 690 | 900 | 1548 | 23.77 | 0 | 0/0/0 |
| cluster8 motor OFF | 1099 | 1155 | 1625 | 2297 | 41.21 | 0 | 0/0/0 |
| roll motor ON | 291 | 365 | 800 | 1104 | 10.91 | 0 | 0/0/0 |
| sustained phrase motor ON | 628 | 690 | 850 | 1737 | 23.55 | 0 | 0/0/0 |
| repeated model switches | 279 | 305 | 375 | 1019 | 10.46 | 0 | 0/0/0 |
| single motor ON | 280 | 305 | 375 | 905 | 10.50 | 0 | 0/0/0 |

Raw logs: `qualification/m75_ble_batch0_raw.log` and `m75_ble_batch1_raw.log`; extracted fields/checksums: `m75_ble_hardware_manifest.json`. Initial connected selection callback 632 us; 44 repeated double-selection events max 1019 us including strike/render. Steady motor delta 11.69 us/128 frames between the two connected runs; separate-run variability applies.

Periodic AUDIO model labels track the UI selection (PAN) while forensics owns synthesis. CURVE and FIXTURE_IO identify the actual VIBRAPHONE model. This matches the M7.4 reporting convention.

Repeat connected captures by setting POCKETPAN_FORENSICS_DISCONNECTED=0; the diagnostic waits for MIDI-ready subscription before starting. The disconnected setting intentionally declines discovered MIDI peripherals; it is a controlled radio scenario, not evidence that a controller was unavailable.

Normal firmware was flashed again after the connected captures. `qualification/m75_production_final_flash.log` / `m75_production_final_smoke.log` confirm PAN boot, BLE MIDI ready, 83343 bytes steady internal free / 31744 largest, and zero logged deadline/I2S faults. All diagnostic/button/instrument-smoke flags are OFF. The physical board is left running this normal image.
