# PAN M5C.2 body coupling qualification

Selected production strategy: **strike/exciter bus**. It taps local exciter energy before each voice modal bank; sympathetic feedback is excluded. Full mix and transient remain host-only comparison strategies. The fixed 1980 Hz body mode is retained and is currently weakly excited by the 1800 Hz input LPF. Physical listening required.

## Strategy comparison — D3 v70

| Strategy | RMS | dry→wet dB | difference RMS / relative dB | contribution correlation (attack/mid/tail) | body RMS | clamp / sympathetic safety |
|---|---:|---:|---:|---|---:|---:|
| A: body off | 0.0695 | 0.0000 | 0.0000 / -216.8441 | 0.0000 / 0.0000 / 0.0000 | 0.0000 | 0 / 0 |
| B: full mix | 0.0708 | 0.1510 | 0.0213 / -10.2802 | -0.2529 / -0.0960 / -0.5226 | 0.0250 | 0 / 0 |
| C: transient | 0.0701 | 0.0743 | 0.0122 / -15.1278 | 0.0624 / -0.1014 / -0.2811 | 0.0143 | 0 / 0 |
| D: strike bus | 0.0703 | 0.0915 | 0.0103 / -16.6174 | -0.1291 / 0.0443 / -0.0180 | 0.0121 | 0 / 0 |

## Candidate gain sweeps — D3 v70, strike bus

| Sweep | Value | RMS delta dB | difference relative dB |
|---|---:|---:|---:|
| outputGain | 0.0700 | 0.0368 | -20.5433 |
| outputGain | 0.0900 | 0.0612 | -18.3604 |
| outputGain | 0.1100 | 0.0915 | -16.6174 |
| outputGain | 0.1300 | 0.1277 | -15.1664 |
| excitationGain | 0.1000 | 0.0279 | -21.7230 |
| excitationGain | 0.1400 | 0.0552 | -18.8004 |
| excitationGain | 0.1800 | 0.0915 | -16.6174 |

## Register balance — candidate D, velocity 70

| Note | dry RMS | candidate RMS | dry→body dB | tail dry→body dB | 0-5 / 0-20 / 0-100 ms dB | correlation attack/mid/tail | difference RMS / relative dB |
|---|---:|---:|---:|---:|---:|---|---:|
| D3 | 0.0695 | 0.0703 | 0.0915 | -0.0014 | 1.9221 / -0.0414 / 0.1094 | -0.1291 / 0.0443 / -0.0180 | 0.0103 / -16.6174 |
| A3 | 0.0649 | 0.0657 | 0.1050 | 0.0017 | 1.2092 / 0.6072 / 0.1491 | 0.2432 / 0.0095 / -0.0010 | 0.0103 / -16.0183 |
| D4 | 0.0589 | 0.0598 | 0.1294 | 0.0026 | 0.3397 / 0.0918 / 0.1842 | -0.0723 / 0.0021 / -0.0006 | 0.0103 / -15.1672 |
| A4 | 0.0651 | 0.0658 | 0.0995 | 0.0034 | 0.7407 / 0.1229 / 0.1348 | -0.0363 / 0.0024 / 0.0017 | 0.0103 / -16.0337 |

## Full-mix polarity and delay diagnostics — D3 v70

These are offline analysis only; no polarity or delay switch is shipped.

| Full-mix return | RMS delta dB vs dry |
|---|---:|
| +1, 0 samples | 0.1510 |
| +1, 1 samples | 0.1484 |
| +1, 2 samples | 0.1460 |
| +1, 4 samples | 0.1419 |
| +1, 8 samples | 0.1360 |
| -1, 0 samples | 0.6150 |
| -1, 1 samples | 0.6173 |
| -1, 2 samples | 0.6194 |
| -1, 4 samples | 0.6232 |
| -1, 8 samples | 0.6284 |

## Chord, interval, roll, limiter, and stability

| Fixture | dry/candidate RMS | dry/candidate max GR | dry/candidate avg GR | candidate clamp / safety |
|---|---:|---:|---:|---:|
| D3+A3 interval | 0.0883 / 0.0893 | 0.0000 / 0.0000 | 0.0000 / 0.0000 | 0 / 0 |
| D3 A3 D4 A4 chord | 0.0996 / 0.0971 | -3.0856 / -4.8646 | -0.1149 / -0.1728 | 0 / 0 |
| D3 roll | 0.3977 / 0.3883 | -6.8058 / -7.3473 | -1.1917 / -1.3268 | 0 / 0 |
| D3 v110 | 0.1051 / 0.1062 | 0.0000 / 0.0000 | 0.0000 / 0.0000 | 0 / 0 |

## Conclusion

M5C.2 status: **PASS (host)**. Strike bus is selected because it stops continuously feeding the shell with coherent modal tails while preserving a single six-mode global resonator. Sympathetic defaults are unchanged (0.005 / 0.002 / 1500 Hz / 0.03), safety count is zero, and body-off/sympathetic-off M5B bit identity remains covered by Test 13. CPU: host only; hardware telemetry and physical listening remain required before voicing freeze.
