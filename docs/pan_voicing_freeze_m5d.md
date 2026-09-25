# PAN M5D voicing freeze

M5D freezes the PAN as the reference sound for M6. It keeps the eight modal
voices, 48 kHz / 128-frame processing, six shared body modes, StrikeBus body
coupling, shared sympathetic bus, and M5A headroom/limiter. No new resonator,
effect, allocation, or expensive DSP is introduced.

## Selected calibration

| Area | Frozen value |
|---|---|
| Exciter gain / noise | 0.80 / 1.00 |
| Brightness range | 700–12000 Hz |
| Velocity energy curve | `0.15 + 0.85 * pow(v', 1.25)`, `v' = v` through 0.85, then slope 0.35 |
| Hardness | `0.18 + 0.82 * pow(v, 1.15)` |
| Upper-mode blend | 0.18 → 0.94 velocity (M5D.1 provisional final) |
| Doublet | fixed 1 Hz target |
| StrikeBus gain | 48 |
| Body excitation / output / LPF | 0.18 / 0.11 / 1800 Hz |
| Body modes (Hz, gain, T60 s) | 110/.18/1.00; 205/.14/.82; 390/.11/.65; 730/.08/.48; 1280/.05/.32; 1980/.035/.22 |
| Sympathetic | enabled; input .005, feedback .002, LPF 1500 Hz, max bus .03 |
| Limiter | -3 dB threshold, -0.5 dB ceiling, 32-sample lookahead, 80 ms release |

The M5D knee remains 0.85 / 0.35. M5D.1 selects a provisional upper-mode
hard-velocity endpoint of 0.94: it restores moderate metallic life relative
to the 0.98 M5D current curve while retaining the v127 limiter headroom gain
over M5C.2. This is pending hardware listening; no other body, sympathetic,
limiter, doublet, polyphony, sample-rate, or block-size parameter changed.

## Host regression fingerprint

`test_dsp` writes `pan_m5d_voicing.md`, `pan_m5d1_ab.md`, and named true
M5C.2/M5D/bright WAV fixtures in its working directory. M5D.1 uses C=0.94 as
the provisional listening candidate; final hardware approval remains required.

| D3 velocity | RMS | brightness | body RMS | max limiter GR |
|---:|---:|---:|---:|---:|
| 30 | .037892 | .002623 | .006652 | 0 dB |
| 70 | .070271 | .009437 | .012075 | 0 dB |
| 110 | .106164 | .018184 | .017748 | 0 dB |
| 127 | .114743 | .019630 | .019001 | 0 dB |

The full tested C curve is monotonic in RMS and configured upper-mode blend at
velocities 1, 10, 20, 30, 40, 50, 64, 80, 96, 110, 120, and 127. The M5C.2
register-loss guard remains in force: no principal note may lose more than 1.5
dB from body interaction without explanation. Host fixtures report zero hard
clamps and zero modal internal-saturation events.

## Hardware listening and realtime gate

- [ ] v30 sounds soft but alive; v70 is natural; v110 has metal without harshness; v127 remains controlled.
- [ ] D3 has weight; A3 is not weak; D4 stays coherent; A4 is not clicky.
- [ ] Soft→hard preserves the tail; hard→soft does not erase it.
- [ ] Steady, crescendo, and decrescendo rolls feel continuous.
- [ ] The body belongs to the instrument; sympathetic sound is subtle; no phase or limiter pumping.
- [ ] Compare fixed 1 Hz doublet with a relative doublet by ear.
- [ ] Record ESP32-S3 single/4-voice/8-voice/roll: average, p99 (if available), and max block time; CPU load; deadline misses; write timeouts; short writes; TX errors.

Host CI does not substitute for this gate. Realtime acceptance requires zero
deadline misses, timeouts, short writes, and TX errors during normal
qualification. M6 must leave the selected PAN bit-for-bit and sonically
unchanged when PAN is selected.
