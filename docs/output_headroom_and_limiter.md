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
| Poly headroom | 1: 0 dB, 2: -1.5 dB, 4: -3 dB, 8: -4.5 dB (limited to -5 dB) | Interpolated by `log2(voices)` and smoothed over 35 ms |
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

## Host evidence and remaining validation

`test_dsp` checks a 1 kHz sine below threshold for sample-exact output apart
from the known 32-sample delay. It also verifies an above-ceiling sine, the
ceiling contract, and gain recovery for 50, 80, and 120 ms releases. Existing
scheduled fixtures cover single notes, double strikes, chord, cluster8, and
roll; their generated WAVs remain unversioned CI artifacts.

The included host comparison is evidence of DSP behavior, not physical proof.
Complete the added checklist in `hardware_qualification.md`, record maximum
observed GR, and compare audio CPU measurements before declaring M5A physically
validated.
