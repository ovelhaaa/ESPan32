# Bell V1 freeze record

## Freeze state

**Pending M6.3 hardware qualification and human selection.** Candidate A is
the current production preset and baseline tie-breaker. This record must not
be relabelled as frozen until the required ESP32-S3 data and listening result
are recorded in the M6.3 reports.

## Fixed DSP configuration

| Family | Ratio | Gain | T60 s |
|---|---:|---:|---:|
| hum | 0.5000 | 0.28 | 5.5 |
| prime | 1.0000 | 1.00 | 5.0 |
| prime doublet | 1.0020 | 0.20 | 4.6 |
| tierce | 1.2000 | 0.65 | 3.8 |
| quint | 1.5000 | 0.22 | 2.6 |
| nominal | 2.0000 | 0.85 | 4.2 |
| nominal doublet | 2.0015 | 0.16 | 3.9 |
| superquint | 3.0000 | 0.50 | 2.8 |
| octave nominal | 4.0000 | 0.32 | 2.2 |
| upper | 5.2000 | 0.15 | 1.4 |

Sample rate remains 48 kHz and block size remains 128 frames. Bell follows
the existing velocity response, silence threshold, voice allocation and
limiter behavior unchanged from M6.2.1. Bell disables the shared PAN body and
sympathetic paths; modes above the safe Nyquist region are disabled by the
existing resonator-bank rule.

## Required promotion evidence

- PAN's twelve FNV fixtures and the PAN -> BELL -> PAN switch remain exact.
- Bell's selected preset receives committed FNV golden fixtures: D4 v30/v70/
  v110/v127, D3 v70, A3 v70, A4 v70, chord and roll.
- Hardware timing and transport counters pass every gate in
  [hardware_m63_metrics.md](hardware_m63_metrics.md).
- The listening choice is A or C. A wins an unresolved tie; B is not a default
  third comparison. Prime doublet remains 1.002 unless listening identifies a
  clear alternate preference; nominal doublet remains 2.0015.

Once promoted, future instrument-model changes must pass both the PAN and
Bell fingerprint suites before merge.
