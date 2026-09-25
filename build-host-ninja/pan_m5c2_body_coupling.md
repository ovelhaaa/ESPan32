# PAN M5C.2 body coupling qualification

Selected production strategy: **strike/exciter bus**. It taps local exciter energy before each voice modal bank; sympathetic feedback is excluded. Full mix and transient remain host-only comparison strategies. The fixed 1980 Hz body mode is retained and is currently weakly excited by the 1800 Hz input LPF. Physical listening required.

## Strategy comparison — D3 v70

| Strategy | RMS | dry→wet dB | difference RMS / relative dB | contribution correlation (attack/mid/tail) | body RMS | clamp / sympathetic safety |
|---|---:|---:|---:|---|---:|---:|
| A: body off | 0.0698 | 0.0000 | 0.0000 / -216.8717 | 0.0000 / 0.0000 / 0.0000 | 0.0000 | 0 / 0 |
| B: full mix | 0.0710 | 0.1494 | 0.0214 / -10.2737 | -0.2532 / -0.0967 / -0.5204 | 0.0251 | 0 / 0 |
| C: transient | 0.0703 | 0.0718 | 0.0122 / -15.1248 | 0.0609 / -0.1030 / -0.2813 | 0.0144 | 0 / 0 |
| D: strike bus | 0.0705 | 0.0909 | 0.0103 / -16.6450 | -0.1294 / 0.0442 / -0.0180 | 0.0121 | 0 / 0 |

## Candidate gain sweeps — D3 v70, strike bus

| Sweep | Value | RMS delta dB | difference relative dB |
|---|---:|---:|---:|
| outputGain | 0.0700 | 0.0365 | -20.5709 |
| outputGain | 0.0900 | 0.0607 | -18.3880 |
| outputGain | 0.1100 | 0.0909 | -16.6450 |
| outputGain | 0.1300 | 0.1269 | -15.1940 |
| excitationGain | 0.1000 | 0.0277 | -21.7506 |
| excitationGain | 0.1400 | 0.0548 | -18.8281 |
| excitationGain | 0.1800 | 0.0909 | -16.6450 |

## Register balance — candidate D, velocity 70

| Note | dry RMS | candidate RMS | dry→body dB | tail dry→body dB | 0-5 / 0-20 / 0-100 ms dB | correlation attack/mid/tail | difference RMS / relative dB |
|---|---:|---:|---:|---:|---:|---|---:|
| D3 | 0.0698 | 0.0705 | 0.0909 | -0.0014 | 1.9154 / -0.0426 / 0.1084 | -0.1294 / 0.0442 / -0.0180 | 0.0103 / -16.6450 |
| A3 | 0.0651 | 0.0659 | 0.1044 | 0.0017 | 1.2054 / 0.6033 / 0.1480 | 0.2426 / 0.0095 / -0.0010 | 0.0103 / -16.0453 |
| D4 | 0.0591 | 0.0599 | 0.1286 | 0.0026 | 0.3356 / 0.0905 / 0.1828 | -0.0723 / 0.0021 / -0.0006 | 0.0103 / -15.1942 |
| A4 | 0.0653 | 0.0660 | 0.0989 | 0.0034 | 0.7371 / 0.1215 / 0.1339 | -0.0365 / 0.0024 / 0.0017 | 0.0103 / -16.0575 |

## Full-mix polarity and delay diagnostics — D3 v70

These are offline analysis only; no polarity or delay switch is shipped.

| Full-mix return | RMS delta dB vs dry |
|---|---:|
| +1, 0 samples | 0.1494 |
| +1, 1 samples | 0.1469 |
| +1, 2 samples | 0.1446 |
| +1, 4 samples | 0.1406 |
| +1, 8 samples | 0.1350 |
| -1, 0 samples | 0.6175 |
| -1, 1 samples | 0.6197 |
| -1, 2 samples | 0.6218 |
| -1, 4 samples | 0.6254 |
| -1, 8 samples | 0.6304 |

## Chord, interval, roll, limiter, and stability

| Fixture | dry/candidate RMS | dry/candidate max GR | dry/candidate avg GR | candidate clamp / safety |
|---|---:|---:|---:|---:|
| D3+A3 interval | 0.0886 / 0.0896 | 0.0000 / 0.0000 | 0.0000 / 0.0000 | 0 / 0 |
| D3 A3 D4 A4 chord | 0.0999 / 0.0973 | -3.1595 / -4.9249 | -0.1174 / -0.1747 | 0 / 0 |
| D3 roll | 0.3969 / 0.3877 | -6.8312 / -7.3656 | -1.2139 / -1.3470 | 0 / 0 |
| D3 v110 | 0.1072 / 0.1082 | 0.0000 / 0.0000 | 0.0000 / 0.0000 | 0 / 0 |

## Conclusion

M5C.2 status: **PASS (host)**. Strike bus is selected because it stops continuously feeding the shell with coherent modal tails while preserving a single six-mode global resonator. Sympathetic defaults are unchanged (0.005 / 0.002 / 1500 Hz / 0.03), safety count is zero, and body-off/sympathetic-off M5B bit identity remains covered by Test 13. CPU: host only; hardware telemetry and physical listening remain required before voicing freeze.
