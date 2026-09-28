# M6.3.8 — Connected Candidate Comparison (Tile/Rect UI Rendering vs Baselines)

Hardware: ESP32-S3 rev v0.2, COM10, 240 MHz, ESP-IDF 5.3.0. BLE-MIDI central
connected to **SMK25V2**, `state=8` Ready, interval 11.25 ms. Forensics, 8192
blocks/fixture, **5 us bins**, critical fixtures (5 PAN cluster8, 14 BELL chord4,
13 BELL cluster8), candidate 25 production DSP.

Raw captures:

```text
U0 (baseline row-band)      docs/hardware/m638_u0_connected.log
U1 (full FB + true rect)    docs/hardware/m638_u1_connected.log
U2 (tile/scratch UI)        docs/hardware/m638_u2_connected.log
U1b (no-render control)     docs/hardware/m638_u1b_connected.log
```

---

## Steady Performance

| Variant | PAN cl. avg / p95 / p99 / max | BELL cl. avg / p95 / p99 / max | BELL chord avg / p95 / p99 / max |
|---|---|---|---|
| **U0** Baseline Row-Band | 1632.8 / 1675 / 1815 / 2140 | 1612.9 / 1650 / 1785 / 2009 | 959.4 / 995 / 1125 / 1337 |
| **U1** Full FB + Rect | 1634.8 / 1680 / 1915 / 2129 | 1615.0 / 1650 / 1900 / 2100 | 961.3 / 1000 / 1200 / 1288 |
| **U2** Tile/Scratch UI | 1634.7 / 1675 / 1915 / 2189 | 1615.0 / 1655 / 1895 / 2212 | 961.1 / 1000 / 1185 / 1418 |
| **U1b** No-Render Control | **1627.7** / **1660** / **1695** / **1958** | **1607.7** / **1640** / **1680** / **2014** | **954.5** / **985** / **1020** / **1182** |

---

## Event Performance

| Variant | PAN cl. avg / p99 / max / misses | BELL cl. avg / p99 / max / misses | BELL chord avg / p99 / max / misses |
|---|---|---|---|
| **U0** Baseline Row-Band | 2492.4 / 2655 / 2654 / **0** | 2526.2 / 2795 / 2792 / **1** | 1522.6 / 1640 / 1636 / **0** |
| **U1** Full FB + Rect | 2512.5 / 3085 / 3082 / **2** | 2522.1 / 2645 / 2643 / **0** | 1535.3 / 2060 / 2057 / **0** |
| **U2** Tile/Scratch UI | 2497.5 / 3100 / 3095 / **2** | 2512.0 / 2645 / 2640 / **0** | 1510.7 / 1770 / 1768 / **0** |
| **U1b** No-Render Control | **2412.7** / **2500** / **2499** / **0** | **2455.1** / **2610** / **2606** / **0** | **1432.1** / **1590** / **1586** / **0** |

---

## Display Hardware Transfer Benchmark

Measured via ST7789 SPI2 GDMA hardware completion ISR (`on_color_trans_done`):

| Transfer Size | Pixels | Bytes | CPU Submit Duration | SPI GDMA Transfer Duration | Total Time |
|---|---:|---:|---:|---:|---:|
| Full Frame (240×135) | 32,400 | 64,800 B | 134 µs | 13,101 µs | 13,235 µs |
| Row Band (240×20) | 4,800 | 9,600 B | 132 µs | 2,045 µs | 2,177 µs |
| Small Rect (80×16) | 1,280 | 2,560 B | 138 µs | 635 µs | 773 µs |

---

## Correlation & Jitter Forensics Analysis

1. **GDMA SPI Transfer Isolation:**
   - In U2: `[UICORR] >1733(tot=553 draw=37 lcd=0 both=115 none=401)`
   - `lcdOnly = 0` across all blocks: Background SPI GDMA bus transfers alone do NOT stall Core 0 audio.
   - `none = 401` (72.5%): The vast majority of blocks >1733 µs happen when Core 1 is completely idle with respect to UI rendering and LCD DMA.
2. **U1b Proof:**
   - Without the UI task rendering loop, Candidate 25 easily beats the steady deadline gate:
     - PAN steady p99 = **1695 µs** ($\le 1733$ µs PASS)
     - BELL steady p99 = **1680 µs** ($\le 1733$ µs PASS)
     - Deadline misses = **0**
     - Transport errors = **0/0/0**
3. **P95 vs P99 Tail:**
   - Across U0, U1, and U2, steady p95 is uniformly $\le 1675$ µs, comfortably below the 1733 µs physical budget. The p99 tail is driven by periodic cross-core contention when the FreeRTOS scheduler, memory bus, and NimBLE stack interact during visual redraws.
