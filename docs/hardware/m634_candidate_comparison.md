# M6.3.4 hardware candidate comparison

ESP32-S3 (240 MHz), ESP-IDF 5.3, **BLE connected** to a real BLE-MIDI central
(state=8, interval 11.25 ms) for every row. Forensics harness, 8192
blocks/fixture, DSP profiling **OFF**. Raw captures:

- `m634_c13_phaseA_connected.log` — candidate 13, critical fixtures
- `m634_c16_phaseA_connected.log` / `_repeat.log` — candidate 16 (Phase A)
- `m634_c17_tailT1_connected.log` — T1 `SynthEngine::renderBlock` IRAM
- `m634_c18_tailT2_connected.log` — T2 `PeakLimiter::processSample` IRAM
- `m634_c19_tailT3_connected.log` — T3 `BodyResonator::processSample` IRAM
- `m634_c20_tailT4_connected.log` / `_repeat.log` — T4 accepted combination
- `m634_c21_attack_connected.log` — T4 + exact attack/exciter fast path
- `m634_c20_production_connected.log` — production firmware, idle smoke

All fixtures report `hard=0`, `sat=0`, `bad_voices=0`, `ble_lost=0`,
I/O `timeout/short/tx = 0/0/0`.

## Phase A — connected validation (steady avg/p99/max us)

| Candidate | PAN cluster8 | BELL chord4 | BELL cluster8 |
|---|---|---|---|
| 13 | 1919.21 / 2175 / 2596 | 1123.43 / 1350 / 1683 | 1925.21 / 2200 / 2543 |
| 16 run 1 | 1767.52 / 2025 / 2575 | 974.43 / 1225 / 1385 | 1628.43 / 1900 / 2478 |
| 16 run 2 | 1771.32 / 2100 / 2284 | 977.17 / 1250 / 1379 | 1630.46 / 1900 / 2308 |

Connected c16 vs c13: PAN cluster **-7.90% / -6.90% / -0.81%**, Bell chord
**-13.26% / -9.26% / -17.71%**, Bell cluster **-15.42% / -13.64% / -2.56%**.
(avg / p99 / max). Candidate 16 promoted.

Connected vs disconnected (c16): Δavg/Δp99/Δmax = PAN -2.5/-25/+1 us,
Bell chord +0.4/0/-35 us, Bell cluster +0.4/+25/+34 us — at or below one 25 us
histogram bin.

## Phase B — tail-IRAM experiments (steady avg/p99/max us)

| Candidate | placement | PAN cluster8 | BELL chord4 | BELL cluster8 |
|---|---|---|---|---|
| 16 | — | 1767.52/2025/2575 | 974.43/1225/1385 | 1628.43/1900/2478 |
| 17 | renderBlock | 1765.23/2000/2272 | 971.26/1175/1627 | 1625.12/1850/2315 |
| 18 | limiter | 1763.02/2000/2478 | 968.70/1175/1464 | 1622.86/1825/2391 |
| 19 | body | 1770.89/2050/2608 | 977.57/1250/1732 | 1630.95/1900/2333 |
| **20** | all three | **1755.75/1900/2372** | **963.91/1125/1388** | **1617.53/1775/2426** |
| 20 repeat | all three | 1756.02/1925/2425 | 963.88/1125/1476 | 1617.47/1775/2290 |

T4 acceptance vs candidate 16: PAN cluster p99 -6.2/-8.3%, Bell chord p99
-8.2/-10.0%, Bell cluster p99 -6.6%; average regression <= 1.1%. T1/T2/T3 alone
are within bin noise. T4 retained; it is the production default.

IRAM delta (`.iram0.text`): candidate 16 = 97,115 B, candidate 20 = 98,351 B
(+1,236 B). `.dram0.bss` +32 B. Production free internal heap 157,115 B,
largest block 53,248 B.

## Phase C — attack/exciter fast path (event avg/p99/max us)

| Fixture | c20 event | c21 event | Δavg / Δp99 / Δmax |
|---|---|---|---|
| PAN cluster8 | 2869.36 / 3150 / 3132 | 2687.45 / 2950 / 2925 | -5.8% / -4.8% / -5.0% |
| BELL chord4 | 1731.53 / 2050 / 2034 | 1594.85 / 1900 / 1888 | -7.6% / -7.3% / -7.2% |
| BELL cluster8 | 2828.45 / 3150 / 3140 | 2694.56 / 3000 / 2991 | -4.9% / -4.8% / -4.6% |

Exact (differential harness + PAN golden + byte-identical WAVs) but below the
>= 15% retention gate; not promoted for performance. Fixture event-block
deadline counts (>2666 us) fall 44 -> 20 (PAN) and 44 -> 18 (BELL cluster).

## Whole-callback (`[AUDIO]`, production firmware, idle, BLE connected)

```text
avg 352 us  p99 425 us  max 588 us  cpu 13.2%  deadline 0  timeout/short/tx 0/0/0
```

Forensics whole-callback CPU windows were ~61% (PAN) and ~61% (Bell cluster),
below the 65% hard limit.

## Final gate summary (candidate 20)

| Fixture | avg gate 1733 | p99 gate 1733 | max gate 2133 |
|---|---|---|---|
| PAN cluster8 | 1755.75 FAIL | 1900 FAIL | 2372 FAIL |
| BELL chord4 | 963.91 PASS | 1125 PASS | 1388 PASS |
| BELL cluster8 | 1617.53 PASS | 1775 FAIL | 2426 FAIL |

M6.3.4 is **PARTIAL**: the 8-voice clusters remain the only red fixtures, and
PAN cluster is the sole blocker. Bell ablation NOT authorized.
