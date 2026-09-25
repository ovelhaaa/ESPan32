# Output headroom and transparent peak limiter (M5A)

## Problem and conclusion

The M4.2 qualification fixtures showed modal internal saturation at zero while
the former output `tanh` guard was frequently active for high velocity and
polyphonic material. That made the output stage, rather than the PAN modal
ratios, decay times, exciter, or damping, the primary distortion hypothesis.

## Production signal path

`voice mix -> smooth polyphonic headroom -> 32-sample lookahead peak limiter -> hard safety clamp -> stereo I2S`

The normal firmware no longer waveshapes with `tanh`. The final clamp is only a
NaN/Inf/overflow safeguard and is counted as `hardClampCount`; it should remain
zero for musical fixtures.

## Parameters and rationale

| Item | Value | Notes |
|---|---:|---|
| Master gain | 0.85 | Existing nominal output calibration |
| Poly headroom | 1: 0 dB, 2: -1.5 dB, 4: -3 dB, 8: -4.5 dB (limited to -5 dB) | Interpolated by `log2(voices)`; 3 ms attenuation attack and 50 ms recovery |
| Detector threshold | -3 dBFS | Explicit activity threshold for diagnostics |
| Ceiling | -0.5 dBFS | Gain-control output ceiling |
| Lookahead | 32 samples | 0.667 ms at 48 kHz |
| Attack | instantaneous | The delayed signal allows gain to be ready before its peak |
| Release | 80 ms | Candidate releases 50/80/120 ms are host-tested; 80 ms is the default compromise for metallic tails |

Voice-count headroom was chosen for this milestone because it is deterministic,
allocation-free, and does not alter the relative modal gains. It is deliberately
not `1 / activeVoices`; the smoothed curve prevents abrupt loudness steps.

## Diagnostics and realtime ownership

Core 0 owns `SynthEngine` and publishes `preLimiterPeak`, `postLimiterPeak`,
current and maximum gain reduction, limiter-active samples, and hard-clamp
count through the existing lock-free telemetry snapshot. Core 1 only consumes
that snapshot for the Audio Diagnostic screen.

The limiter has a fixed 64-float backing array (32 samples used by default), no
heap use, locking, or dynamic container on the target audio path. Its added
algorithmic latency is 0.667 ms. CPU cost must still be measured on hardware
using the existing average/max render-time counters and compared with the
2667 us block budget.

## Host A/B/C evidence

`test_dsp` renders the same chord, cluster8, and roll through three host-only
strategies: A is the retired 0.85-master `tanh` stage, B is the production
poly-headroom plus limiter path, and C is a 0.50-master `tanh` reference.  The
old waveshaper is test code only and cannot be selected by firmware.

| Fixture | A peak / RMS | B pre / post / RMS | B max / avg GR | B samples >0.1 / >1 dB | B clamps | C peak / RMS |
|---|---|---|---:|---:|---:|---|
| Chord | 1.000 / 0.118 | 1.716 / 0.944 / 0.072 | -5.19 / -0.09 dB | 14,171 / 5,525 | 0 | 0.985 / 0.073 |
| Cluster8 | 1.000 / 0.165 | 4.582 / 0.944 / 0.085 | -13.72 / -0.20 dB | 17,063 / 8,418 | 0 | 1.000 / 0.113 |
| Roll | 1.000 / 0.399 | 1.618 / 0.944 / 0.331 | -4.68 / -0.79 dB | 44,324 / 31,429 | 0 | 0.938 / 0.245 |

The complete generated table includes RMS-difference columns and is emitted as
the unversioned `output_stage_abc_metrics.md` CI artifact. It also emits
`pan_{chord,cluster8,roll}_{old,new}.wav` for listening comparisons. The host
result supports the output-stage hypothesis: merely lowering static gain loses
more roll RMS, while B stays below the ceiling without `tanh` saturation.

`limiterActiveSamples` remains available for compatibility, but it is not a
useful severity measure because it counts even negligible gain changes. The
realtime snapshot and Audio Diagnostic now additionally expose samples above
0.1 dB and 1 dB GR plus average GR; these are the preferred tuning metrics.

## Host evidence and remaining validation

`test_dsp` checks a 1 kHz sine below threshold for sample-exact output apart
from the known 32-sample delay. It also verifies an above-ceiling sine, the
ceiling contract, and gain recovery for 50, 80, and 120 ms releases. Existing
scheduled fixtures cover single notes, double strikes, chord, cluster8, and
roll; their generated WAVs remain unversioned CI artifacts.

The included host comparison is evidence of DSP behavior, not physical proof.
No ESP32 before/after CPU timing has been recorded yet, so CPU delta is
intentionally not claimed. Complete the added checklist in
`hardware_qualification.md`, record maximum observed GR, and compare the
existing average/max render-time counters with the 2667 us block budget before
declaring M5A physically validated.
