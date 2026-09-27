# M6.3.5 — connected candidate comparison (candidate 20 vs 25) + tail isolation

Hardware: ESP32-S3 rev v0.2, COM10, 240 MHz, ESP-IDF 5.3.0. BLE-MIDI central
connected to **SMK25V2**, `state=8 (Ready)`, interval **11.25 ms**. Forensics
builds, profiling **OFF**, 8192 blocks/fixture, `CRITICAL_ONLY=1`,
`DISCONNECTED=0`. `bad_voices=0`, `ble_lost=0`, `hard=0`, `sat=0` in all rows.

Raw captures:

```text
c20                docs/hardware/m635_c20_connected.log
c25 run 1          docs/hardware/m635_c25_connected.log
c25 run 2 (repeat) docs/hardware/m635_c25_connected_repeat.log
c25 full matrix    docs/hardware/m635_c25_full_connected.log
U0 normal          docs/hardware/m635_tail_u0_normal.log
U1 no LCD transfer docs/hardware/m635_tail_u1_no_lcd_transfer.log
U2 no UI           docs/hardware/m635_tail_u2_no_ui.log
```

Percentiles below are the device `[CURVE]` values (`p99` is the 25 µs bin edge);
the histogram-implied quantiles come from `tests/analyze_m635_tail.py`.

## Steady (c20 vs c25 run 1)

| Fixture | c20 avg/p99/max | c25 avg/p99/max | Δavg | Δp99 | Δmax |
|---|---|---|---:|---:|---:|
| PAN cluster8 | 1754.67 / 1900 / 2389 | 1632.58 / 1775 / 2131 | **-6.96%** | **-6.58%** | **-10.87%** |
| BELL chord4  | 963.58 / 1100 / 1482  | 963.43 / 1100 / 1265  | -0.02% | 0.00% | -14.64% |
| BELL cluster8| 1617.11 / 1775 / 2293 | 1617.59 / 1775 / 2082 | +0.03% | 0.00% | -9.20% |

Repeatability (c25 run 2): PAN cluster8 `1632.18 / 1775 / 2038`; BELL cluster8
`1616.83 / 1750 / 2074`; BELL chord4 `963.62 / 1125 / 1233`. Within one 25 µs
histogram bin of run 1, i.e. the gains are real, not bin noise.

## Events (c20 vs c25 run 1)

| Fixture | c20 event avg/p99/max | c25 event avg/p99/max | Δavg | Δp99 | Δmax |
|---|---|---|---:|---:|---:|
| PAN cluster8 | 2821.09 / 3225 / 3202 | 2499.21 / 2750 / 2728 | **-11.41%** | -14.73% | -14.81% |
| BELL chord4  | 1691.72 / 1900 / 1890 | 1512.01 / 1775 / 1772 | **-10.63%** | -6.58% | -6.24% |
| BELL cluster8| 2820.92 / 3400 / 3398 | 2502.50 / 2850 / 2838 | **-11.30%** | -9.52% | -16.46% |

Event `deadline` (blocks over 640000 cycles): c20 PAN 44 / BELL cl. 44 →
c25 PAN 1 / BELL cl. 2.

## Fast-path counters (hardware)

Instrumented in `polyphony_forensics.h` (read at fixture completion; counters are
cleared by `synth.reset()` on each event, so they reflect the final ~188-block
cycle). c20 compiles no fast paths → all zero by construction.

| Fixture | c20 stable8/attack | c25 stable8/attack |
|---|---|---|
| PAN cluster8 (5)  | 0 / 0 | **106 / 16** |
| BELL chord4 (14)  | 0 / 0 | 0 / 8 |
| BELL cluster8 (13)| 0 / 0 | 0 / 16 |

This proves on hardware that candidate 25 actually selects the sympathetic
coefficient cache (no counter; exercised by every PAN render), the PAN stable-8
path, and the attack-voice fast path. The remaining cycle blocks are the general
path (attack-transition blocks and, for BELL, the voice-major fallback).

## Gates — candidate 25

| Gate | Required | c25 | Result |
|---|---|---|---|
| PAN cluster8 steady avg | ≤1733 | 1632.4 | PASS |
| PAN cluster8 steady p99 | ≤1733 | 1775 | **FAIL** |
| PAN cluster8 steady max | ≤2133 | 2038–2131 | PASS |
| BELL chord4 steady | ≤1733/≤1733/≤2133 | 963/1100/1265 | PASS |
| BELL cluster8 steady avg | ≤1733 | 1617 | PASS |
| BELL cluster8 steady p99 | ≤1733 | 1750–1775 | **FAIL** |
| BELL cluster8 steady max | ≤2133 | 2074–2082 | PASS |
| CPU (forensics whole-callback) | ≤65% | 62.2% peak | PASS |
| production deadline | 0 | 0 | PASS |
| I/O timeout/short/tx | 0/0/0 | 0/0/0 | PASS |

Only the two 8-voice cluster **p99** gates remain red; both averages/maxes pass.

## Phase C — tail isolation (candidate 25)

| Variant | PAN cl. steady avg/p99/max | BELL cl. steady avg/p99/max |
|---|---|---|
| U0 normal | 1632.23 / 1775 / 2134 | 1616.68 / 1775 / 2116 |
| U1 no LCD transfer | 1631.75 / 1775 / 2260 | 1617.06 / 1775 / 2015 |
| U2 UI suspended | 1621.65 / 1675 / 1927 | 1605.79 / 1650 / 1917 |

Residual-tail population (steady, bins in µs):

| Variant | 1900–2100 | 2100–2200 | 2200–2300 | 2300–2400 | >2400 |
|---|---:|---:|---:|---:|---:|
| U0 PAN / BELL | 41 / 43 | 1 / 1 | 0 / 0 | 0 / 0 | 0 / 0 |
| U1 PAN / BELL | 46 / 44 | 1 / 0 | 1 / 0 | 0 / 0 | 0 / 0 |
| U2 PAN / BELL | 43 / 41 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 |

Interpretation (§17):

```text
U0 tail present
U1 ~ U0   (skipping only the LCD bitmap transfer changes nothing)
U2 strongly improved (p99 -100/-125 us, max -207/-199 us)
=> UI task / framebuffer rendering / scheduler implicated.
   LCD transfer / GDMA NOT implicated.
```

BLE-off (§18) was **not** required: U2 already removes the tail, so the residual
is not BLE-attributable. No UI/DMA/refresh-rate change was made.

## Full connected matrix (candidate 25)

32/32 curves captured; all `bad_voices=0`, `ble_lost=0`, `hard=0`, `sat=0`.
Histogram-implied quantiles:

| Model/fixture | voices | steady avg | steady p99 | steady max | event avg | event p99 | event max |
|---|---:|---:|---:|---:|---:|---:|---:|
| PAN single | 1 | 500.78 | 650–675 | 789 | 902.67 | 1150–1175 | 1166 |
| PAN double | 2 | 823.20 | 925–950 | 1214 | 1215.21 | 1475–1500 | 1498 |
| PAN chord4 | 4 | 1133.80 | 1275–1300 | 1553 | 1715.36 | 2025–2050 | 2041 |
| PAN 6-note | 6 | 1443.76 | 1575–1600 | 1855 | 2198.67 | 2425–2450 | 2438 |
| PAN cluster8 | 8 | 1633.71 | 1775–1800 | 2073 | 2504.57 | 2750–2775 | 2761 |
| PAN restrike(chord4) | 4 | 1134.27 | 1275–1300 | 1480 | 1706.77 | 1925–1950 | 1941 |
| PAN roll | 1 | 611.22 | 725–750 | 895 | 1076.63 | 1325–1350 | 1337 |
| BELL single | 1 | 294.63 | 325–350 | 381 | 697.44 | 900–925 | 903 |
| BELL double | 2 | 630.62 | 725–750 | 931 | 1027.14 | 1325–1350 | 1329 |
| BELL chord4 | 4 | 960.46 | 1100–1125 | 1236 | 1522.57 | 1650–1675 | 1655 |
| BELL 6-note | 6 | 1288.99 | 1425–1450 | 1608 | 2043.44 | 2325–2350 | 2333 |
| BELL cluster8 | 8 | 1618.50 | 1750–1775 | 2175 | 2518.29 | 2750–2775 | 2768 |
| BELL restrike(chord4) | 4 | 964.81 | 1100–1125 | 1294 | 1506.47 | 1625–1650 | 1647 |
| BELL roll | 1 | 296.02 | 325–350 | 384 | 792.04 | 1000–1025 | 1088 |

(0-voice baseline rows omitted.) All fixtures pass avg/max; only the two 8-voice
clusters miss p99.

## Production smoke (candidate 25, forensics OFF)

```text
avg 346 us   p99 400 us   max 988 us   cpu 13.0%
deadline 0   timeout 0    short 0      tx 0
BLE state 8 (Ready, 11.25 ms)   heap free 153443   largest block 51200
```
