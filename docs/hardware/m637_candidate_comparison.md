# M6.3.7 — connected comparison (UI policy + trigger precompute)

Hardware: ESP32-S3 rev v0.2, COM10, 240 MHz, ESP-IDF 5.3.0. BLE-MIDI central
connected to **SMK25V2**, `state=8` Ready, interval 11.25 ms. Forensics, 8192
blocks/fixture, **5 us bins**, critical fixtures (5 PAN cluster8, 14 BELL chord4,
13 BELL cluster8), profiling OFF.

Raw captures:

```text
baseline c25        docs/hardware/m637_c25_connected.log
no-render U1b       docs/hardware/m637_u1b_connected.log
UI 5 Hz (hash)      docs/hardware/m637_ui5_connected.log
UI 2 Hz (pre-hash)  docs/hardware/m637_ui2_connected.log
UI 1 Hz (pre-hash)  docs/hardware/m637_ui1_connected.log
trigger c27         docs/hardware/m637_c27_connected.log
```

## Steady

| Variant | PAN cl. avg/p99/max | BELL cl. avg/p99/max | BELL chord avg/p99/max |
|---|---|---|---|
| c25 baseline | 1636 / 1775 / 2050 | 1617 / 1780 / 2089 | 964 / 1110 / 1240 |
| U1b no render | 1628 / 1700 / 1941 | 1608 / 1675 / 1971 | 955 / 1025 / 1178 |
| UI 1 Hz | 1636 / 1770 / 2041 | 1617 / 1760 / 2058 | 964 / 1105 / 1354 |
| UI 2 Hz | 1638 / 1790 / 2057 | 1618 / 1770 / 2182 | 965 / 1125 / 1277 |
| UI 5 Hz (hash) | 1632 / 1780 / 2067 | 1613 / 1760 / 1996 | 960 / 1105 / 1253 |
| c27 trigger | 1634 / 1780 / 2108 | 1619 / 1765 / 2162 | 966 / 1120 / 1305 |

## Events

| Variant | PAN cl. avg/p99/max /miss | BELL cl. avg/p99/max /miss | BELL chord avg/p99/max /miss |
|---|---|---|---|
| c25 baseline | 2475 / 2805 / 2800 / **2** | 2500 / 2755 / 2752 / **2** | 1513 / 1905 / 1904 / 0 |
| U1b no render | 2410 / 2525 / 2524 / **0** | 2445 / 2565 / 2562 / **0** | 1444 / 1590 / 1585 / 0 |
| UI 1 Hz | 2481 / 2690 / 2689 / 1 | 2498 / 2700 / 2700 / 2 | 1498 / 1565 / 1561 / 0 |
| UI 2 Hz | 2473 / 2640 / 2639 / 0 | 2511 / 2880 / 2879 / 3 | 1520 / 1905 / 1903 / 0 |
| UI 5 Hz (hash) | 2470 / 2615 / 2610 / **0** | 2502 / 2650 / 2648 / **0** | 1499 / 1575 / 1572 / 0 |
| c27 trigger | 2470 / 2800 / 2798 / 2 | 2499 / 2690 / 2689 / 2 | 1508 / 1685 / 1682 / 0 |

## Fast-path counters / diagnostics

`[UIREDRAW]` (UI 5 Hz, first 5 s windows): urgent=6, telemetry ~20-44, skip
dominant. The formatting-free hash policy renders only on urgent change or once
per 200 ms.

## Conclusions

```text
U1b (no render) passes every gate          -> DSP sustain path is below gate
scaling redraw 30->5->2->1 Hz              -> steady p99 unchanged (~1770-1790)
UI 5 Hz hash policy                        -> event misses 0, steady p99 unchanged
c27 trigger LUT + log cache                -> event avg -0.2%, misses unchanged
```

The steady p99 residual is caused by the render's presence (Core-1 formatting /
framebuffer / bus pressure during Core-0 blocks), not by its frequency. The UI
policy closes the event deadline gate; the steady p99 gate remains open.
