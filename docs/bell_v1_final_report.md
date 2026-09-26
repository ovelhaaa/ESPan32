# Bell V1 final report

## Status

**NOT FROZEN — BELL chord and cluster8 failed the ESP32-S3 real-time gates;
the final listening choice is also pending.**

## Current production candidate

Candidate A remains the compiled `kPresetBell` baseline. Its mode ratios,
gains and T60 values remain unchanged by M6.3. Prime doublet is 1.0020;
nominal doublet is frozen at 2.0015. PAN remains the M6 fingerprinted model.

## Host gates

- PAN FNV regression: 12/12 exact, including PAN -> BELL -> PAN bit identity.
- Bell A/B/C, lifetime, stealing and limiter qualifications: host PASS at the
  M6.2.1 baseline.
- M6.3 timing-histogram sanity test: PASS. It verifies nearest-rank p99 with
  99 samples at 500 us plus one at 2000 us, and verifies overflow retention.

## Required final evidence

1. Complete [hardware_m63_metrics.md](hardware_m63_metrics.md) for both BLE
   scenarios and pass all real-time gates.
2. Listen on the same DAC/headphone chain to A and C for D4 v70, D4 v110,
   chord and roll. Record only the preferred wording: A richer/more complex,
   or C clearer/stronger tonal center. Consider B only if both extremes are
   unsatisfactory.
3. Compare prime-doublet half/current/1.5x on a D4 long tail. Keep current
   1.002 unless a preference is clear. Do not reopen nominal-doublet tuning.
4. If C wins, make the explicit production promotion (hum .24, tierce .58,
   nominal .90); otherwise retain A. Then generate and commit Bell golden
   FNV fingerprints for D4 v30/v70/v110/v127, D3 v70, A3 v70, A4 v70, chord,
   and roll, rerun host tests and ESP-IDF build, and replace this status with
   `BELL V1 FROZEN`.

## Captured hardware result (BLE MIDI scenario)

| BELL fixture | Avg us | P99 us | Max us | CPU % | Deadline misses | Result |
|---|---:|---:|---:|---:|---:|---|
| single | 420 | 650 | 1562 | 15.7 | 0 | PASS |
| chord | 1431 | 2000 | 2800 | 53.7 | 1 | FAIL |
| cluster8 | 2495 | >=3175 | 4534 | 93.6 | 1015 | FAIL |
| roll | 597 | 1175 | 2055 | 22.4 | 0 | PASS |

All captured transport counters were zero. The fixed histogram's final bin is
an overflow bin, so cluster8 p99 is at least 3175 us. The failure prevents
Bell V1 promotion and freeze; do not change voicing as part of this report.
