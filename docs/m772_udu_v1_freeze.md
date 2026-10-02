# M7.7.2 — UDU V1 FROZEN

Character: **B**. Opening: **velocity-driven CENTERED**. Restrike: **R1 / RETAIN**.
Size mapping: **compressed**. The accepted M7.7.1 DSP values and arithmetic are
unchanged. All ten instruments are now covered by frozen aggregate oracles.

`main/dsp/udu.h` is the canonical source of exact float literals. Historical
M7.7/M7.7.1 listening packs preserve alternatives; they are not production choices.

| Parameter | Frozen value |
|---|---|
| Cavity fundamentals at MIDI 60 / PARTIAL | 105 Hz, 105 × 2.31 Hz |
| Cavity T60 | .42 s, .42 × .29 s |
| Cavity gains | 1, .17 |
| Shell frequencies | 460, 790, 1230, 1970 Hz |
| Shell gains | .12, .075, .045, .025 |
| Shell T60 | .085, .055, .038, .025 s |
| Cavity / shell drive gains | 1 / 1 |
| Output gain | .0016 |
| Hand slow / fast | 2.8 ms / .35 ms |
| Click low-pass / DC pole | 4200 Hz / 8 Hz |
| Transient duration | 40 ms |
| R1 threshold | cavity energy < 1e-9 |
| UduCache | 5928 bytes, 49 sizes, separate from PreparedNote |
| UduVoice | 124 bytes ESP32-S3 (136 host bytes) |

For `v = clamp(velocity / 127, 0, 1)`, opening is the existing piecewise formula:

```text
v <= .3: .5 * v
v <= .6: .15 + (v - .3) * (.35 / .3)
otherwise: .5 + (v - .6) * 1.25
```

Its anchors are (0,0), (.3,.15), (.6,.5), (.8,.75), (1,1). The cavity
recurrence coefficients and normalized injection interpolate only at a fresh
strike between CLOSED → PARTIAL → OPEN, with PARTIAL an exact knot. Endpoint
frequency scales are .72 / 1 / 1.18; T60 scales are 1.30 / 1 / .72. Actual
intermediate pole frequency/T60 follow coefficient interpolation. Soft hits
are lower and longer; medium hits approach the B center; hard hits are higher
and shorter. Opening never changes shell modes.

MIDI is clamped to 36–84. `size = pow(2, (note-60)*.25/12)`, spanning one octave
across four MIDI octaves; shell pitch scales by `pow(size,.82)`.

Cavity drive is `v*(.72+.28*v)`; shell drive is
`v*(.12+.88*v*v)`. The hand pulse, deterministic finger noise and contact pulse
retain their original formulas. Pressure damping multiplies state by
`1-.006*damping`. Every live restrike updates excitation and retriggers the
transient while retaining live cavity poles. A cleared voice or cavity below
the frozen threshold adopts the new opening. No smoothing or morphing exists.

Hashes are little-endian int32 stereo PCM FNV-1a64, before listening WAV gain.
The aggregate uses MIDI 24–96 × v30/70/110/127, 2048 frames per case, combining
each case hash little-endian into another FNV-1a64. Fixtures use six seconds
at 48 kHz, the canonical 128-frame render partition, and the M7.7.1 event lists.

| Model | Aggregate FNV64 |
|---|---|
| PAN | 9e9801244165101d |
| BELL | 7c2dc6f37fb99f4b |
| TONGUE | 83f75567fb47ebaf |
| BOWL | 14772092e35bc4a7 |
| KALIMBA | e9bcabdd0b6e3bd2 |
| GLASS | 14e0dd6ba566e1cf |
| MARIMBA | 236d684f05f5f1d2 |
| VIBRAPHONE V1 | 3cea893644aa0283 |
| MBIRA V1 | f5ee8755af2fa270 |
| UDU V1 | 552d8d59691b008e |

| UDU fixture | FNV64 |
|---|---|
| single v30 | 5d45252726ededed |
| single v70 | d8994f36036e4f99 |
| single v110 | 3b7acf2fd8f4234d |
| single v127 | 1ad5c9137ae41bf5 |
| dynamic groove | a3aac03f8ae05829 |
| chord4 | 468e7c01255461d5 |
| cluster8 | 12e6cab468bb78e9 |
| rapid restrike | 31b12820676fe041 |
| historical B / fixed PARTIAL groove | 1a768c0057636a2d |

The representative [dynamic groove](listening/m772/udu_v1_dynamic_groove.wav)
is a unity-gain 16-bit listening copy of the golden raw PCM fixture. The
authoritative assertions live in `test_prepared_note` and `test_udu_frozen`;
Vibraphone and Mbira special fixture assertions remain in their existing suites.

Fresh physical qualification and restored production memory are recorded below.
Both Process6 configurations passed before the M8 gate opened.

<!-- M772_PHYSICAL -->

Physical ESP32-S3 / COM10, BLE MIDI at 11.25 ms, 240 MHz, 48 kHz / 128 frames, live UI and I2S. 8192 blocks per fixture. Average is weighted across all callback classes; CPU is average / 2666.7 µs. p95 is the 5 µs histogram upper edge; p99 is the AudioStats histogram upper edge.

| Fixture | Avg µs | p95 µs | p99 µs | Max µs | CPU % | Event avg / max µs |
|---|---:|---:|---:|---:|---:|---|
| single v127 | 270.49 | 315 | 400 | 639 | 10.14 | 112.78 / 126 |
| chord4 | 628.97 | 750 | 950 | 1341 | 23.59 | 125.29 / 138 |
| cluster8 | 1090.91 | 1290 | 1525 | 2095 | 40.91 | 136.67 / 146 |
| dynamic groove | 443.68 | 575 | 750 | 1299 | 16.64 | 120.69 / 258 |
| model switching | 278.56 | 330 | 425 | 1040 | 10.45 | 560.62 / 593 |
| rapid restrike | 310.51 | 375 | 425 | 739 | 11.64 | 42.53 / 159 |

Every fixture: deadline misses, I2S timeout/error/short, nonfinite, resonator faults, hard clamps, BLE losses and invalid voice counts are **zero**.
Cluster8 margin: 571.7 µs.

Restored production: internal free **37707 bytes**, largest block **18432 bytes**; SynthEngine BSS 107848 bytes; PreparedNote tables 86760 bytes; firmware 686608 bytes. Forensics and fixed-opening control disabled; PAN boots and BLE reconnects.

Both Process6 configurations pass all 14 suites, including ten aggregate hashes, Vibraphone fixed fixtures, Mbira B + Buzz fixtures and the authoritative UDU fixtures. **M7.7.2 complete. M8 may begin.**

[Raw hardware evidence](qualification/m772/hardware_manifest.json), [completion and production memory](qualification/m772/completion.json).
