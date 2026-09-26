# M6.3.2 candidate comparison

Connected, BLE Ready, 8192 blocks/fixture, DSP profiling OFF. Critical-only
fixtures: PAN cluster8 (`fixture=5`), BELL chord (`fixture=14`), BELL cluster8
(`fixture=13`). Raw captures:

- `m632_baseline_connected.log` — candidate 9 (D+E)
- `m632_microkernel_connected.log` — candidate 11 (A, inlined)
- `m632_microkernel_repeat_connected.log` — candidate 11 repeat
- `m632_microkernel_outofline_connected.log` — candidate 11 before the placement
  fix (flash-resident, rejected)
- `m632_preparednote_connected.log` — candidate 12 (B)
- `m632_combined_connected.log` — candidate 13 (A+B)
- `m632_combined_outofline_connected.log` — candidate 13 before the placement fix
- `m632_combined_disconnected_scan.log` — candidate 13, disconnected diagnostic
- `m632_profile_baseline_connected.log` — candidate 9, trigger profile
- `m632_profile_preparednote_connected.log` — candidate 12, trigger profile
- `m632_production_smoke.log` — candidate 13 normal firmware

## Sustain and event (avg / p99 / max, us)

| Candidate | PAN cluster sustain | PAN cluster event | BELL chord sustain | BELL chord event | BELL cluster sustain | BELL cluster event |
|---|---|---|---|---|---|---|
| 9 D+E | 2021.68/2325/2597 | 3186.29/3850/3834 | 1209.03/1450/1724 | 2020.12/2475/2470 | 2082.37/2375/2635 | 3320.57/3975/3950 |
| 11 A | 1939.30/2200/2509 | 3105.30/3900/3883 | 1132.67/1375/1483 | 1891.17/2075/2066 | 1923.41/2200/2485 | 3098.90/3675/3667 |
| 12 B | 1997.94/2275/2523 | 2956.22/3350/3346 | 1189.47/1425/1705 | 1803.44/2250/2244 | 2059.04/2300/2547 | 3046.86/3550/3533 |
| 13 A+B | 1908.28/2150/2410 | 2873.27/3275/3260 | 1106.00/1325/1449 | 1736.21/2150/2130 | 1894.87/2125/2604 | 2824.07/3200/3188 |

Sustain deadline misses 0 for all candidates. Event deadline misses 44 for
PAN/Bell cluster, 0 for Bell chord. `hard=0`, `sat=0`, `bad_voices=0`,
`ble_lost=0`, I/O 0/0/0 throughout.

## Rejected variant

| Candidate | PAN cluster sustain | BELL cluster sustain |
|---|---|---|
| 11 out-of-line microkernel (flash) | 2111.29/2775/3089 (dl 346) | 2127.82/2875/3164 (dl 536) |

Repeat reproduced 2111.09 and 2127.35, confirming a real +2–4% regression caused
by the weak template copy landing in flash instead of IRAM.

## Trigger profile (diagnostic, 1024 blocks, profiling ON)

| Model | cand 9 trigger probes us (modal coeff) | cand 12 trigger probes us (modal coeff + lookup) |
|---|---|---|
| PAN cluster8 | 571.66 (270.25) | 273.80 (29.25 + 13.02) |
| BELL chord | 528.25 (259.57) | 259.11 (25.94 + 12.34) |
| BELL cluster8 | 575.68 (326.98) | 292.39 (31.25 + 13.08) |

## Linked sizes

| Candidate | `.iram0.vectors` | `.iram0.text` | `.text_end` | IRAM total | `.dram0.bss` |
|---|---:|---:|---:|---:|---:|
| 9 | 1027 | 96015 | 237 | 97279 | 99816 |
| 11 | 1027 | 96855 | 165 | 98047 | 99816 |
| 12 | 1027 | 96015 | 237 | 97279 | 118552 |
| 13 | 1027 | 96855 | 165 | 98047 | 118552 |

DIRAM (instruction+data) usage: candidate 9 119075 B (34.84%), candidate 13
138651 B (40.57%). PreparedNote tables add 18696 B (measured bss delta 18736 B).
The dedicated 16 KiB IRAM segment is 16383/16384 for both candidate 9 and 13 —
unchanged and pre-existing.
