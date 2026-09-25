# PAN M5C.2 body coupling qualification

Selected production strategy: **strike/exciter bus**. It taps local exciter energy before each voice modal bank; sympathetic feedback is excluded. Full mix and transient remain host-only comparison strategies. The fixed 1980 Hz body mode is retained and is currently weakly excited by the 1800 Hz input LPF. Physical listening required.

## Strategy comparison — D3 v70

| Strategy | RMS | dry→wet dB | difference RMS / relative dB | contribution correlation (attack/mid/tail) | body RMS | clamp / sympathetic safety |
|---|---:|---:|---:|---|---:|---:|
| A: body off | 0.0694 | 0.0000 | 0.0000 / -216.8284 | 0.0000 / 0.0000 / 0.0000 | 0.0000 | 0 / 0 |
| B: full mix | 0.0706 | 0.1521 | 0.0213 / -10.2807 | -0.2527 / -0.0956 / -0.5230 | 0.0250 | 0 / 0 |
| C: transient | 0.0700 | 0.0757 | 0.0122 / -15.1312 | 0.0633 / -0.1006 / -0.2809 | 0.0143 | 0 / 0 |
| D: strike bus | 0.0701 | 0.0919 | 0.0103 / -16.6018 | -0.1289 / 0.0443 / -0.0181 | 0.0121 | 0 / 0 |

## Candidate gain sweeps — D3 v70, strike bus

| Sweep | Value | RMS delta dB | difference relative dB |
|---|---:|---:|---:|
| outputGain | 0.0700 | 0.0370 | -20.5276 |
| outputGain | 0.0900 | 0.0614 | -18.3448 |
| outputGain | 0.1100 | 0.0919 | -16.6018 |
| outputGain | 0.1300 | 0.1282 | -15.1507 |
| excitationGain | 0.1000 | 0.0280 | -21.7073 |
| excitationGain | 0.1400 | 0.0554 | -18.7848 |
| excitationGain | 0.1800 | 0.0919 | -16.6018 |

## Register balance — candidate D, velocity 70

| Note | dry RMS | candidate RMS | dry→body dB | tail dry→body dB | 0-5 / 0-20 / 0-100 ms dB | correlation attack/mid/tail | difference RMS / relative dB |
|---|---:|---:|---:|---:|---:|---|---:|
| D3 | 0.0694 | 0.0701 | 0.0919 | -0.0014 | 1.9260 / -0.0407 / 0.1100 | -0.1289 / 0.0443 / -0.0181 | 0.0103 / -16.6018 |
| A3 | 0.0648 | 0.0656 | 0.1054 | 0.0017 | 1.2113 / 0.6094 / 0.1498 | 0.2435 / 0.0094 / -0.0010 | 0.0103 / -16.0035 |
| D4 | 0.0588 | 0.0597 | 0.1299 | 0.0026 | 0.3421 / 0.0926 / 0.1850 | -0.0723 / 0.0021 / -0.0006 | 0.0103 / -15.1518 |
| A4 | 0.0650 | 0.0657 | 0.0998 | 0.0034 | 0.7428 / 0.1237 / 0.1354 | -0.0361 / 0.0024 / 0.0017 | 0.0103 / -16.0201 |

## Full-mix polarity and delay diagnostics — D3 v70

These are offline analysis only; no polarity or delay switch is shipped.

| Full-mix return | RMS delta dB vs dry |
|---|---:|
| +1, 0 samples | 0.1521 |
| +1, 1 samples | 0.1495 |
| +1, 2 samples | 0.1471 |
| +1, 4 samples | 0.1429 |
| +1, 8 samples | 0.1368 |
| -1, 0 samples | 0.6139 |
| -1, 1 samples | 0.6162 |
| -1, 2 samples | 0.6184 |
| -1, 4 samples | 0.6222 |
| -1, 8 samples | 0.6276 |

## Chord, interval, roll, limiter, and stability

| Fixture | dry/candidate RMS | dry/candidate max GR | dry/candidate avg GR | candidate clamp / safety |
|---|---:|---:|---:|---:|
| D3+A3 interval | 0.0881 / 0.0892 | 0.0000 / 0.0000 | 0.0000 / 0.0000 | 0 / 0 |
| D3 A3 D4 A4 chord | 0.0995 / 0.0969 | -3.0422 / -4.8293 | -0.1134 / -0.1714 | 0 / 0 |
| D3 roll | 0.3982 / 0.3887 | -6.7914 / -7.3363 | -1.1793 / -1.3155 | 0 / 0 |
| D3 v110 | 0.1047 / 0.1058 | 0.0000 / 0.0000 | 0.0000 / 0.0000 | 0 / 0 |

## Conclusion

M5C.2 status: **PASS (host)**. Strike bus is selected because it stops continuously feeding the shell with coherent modal tails while preserving a single six-mode global resonator. Sympathetic defaults are unchanged (0.005 / 0.002 / 1500 Hz / 0.03), safety count is zero, and body-off/sympathetic-off M5B bit identity remains covered by Test 13. CPU: host only; hardware telemetry and physical listening remain required before voicing freeze.
