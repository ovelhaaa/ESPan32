# M6.3.3 hardware candidate comparison

ESP32-S3 (240 MHz), ESP-IDF 5.3.2, BLE **disconnected-scan** scenario
(`POCKETPAN_FORENSICS_DISCONNECTED=1`), 8192 blocks/fixture, DSP profiling OFF,
critical-only and full matrices. Raw captures:

- `m633_c13_disconnected.log` — candidate 13, critical fixtures
- `m633_c16_disconnected.log` — candidate 16, critical fixtures
- `m633_c13_full_disconnected.log` — candidate 13, full 16-fixture matrix
- `m633_c16_full_disconnected.log` — candidate 16, full 16-fixture matrix

Candidate 13 = M6.3.2 baseline (microkernel + PreparedNote). Candidate 16 = 13 +
stable-sustain voice fast path. All fixtures report `hard=0`, `sat=0`,
`bad_voices=0`, `ble_lost=0`, I/O `timeout/short/tx = 0/0/0`.

## Full matrix (avg / p99 / max us; steady vs event)

| Fixture | Model/voices | class | c13 avg/p99/max | c16 avg/p99/max | Δavg |
|---|---|---|---:|---:|---:|
| single | PAN 1 | steady | 518 / 675 / 1002 | 509 / 675 / 853 | -1.7% |
| single | PAN 1 | event | 976 / 1200 / 1185 | 1004 / 1325 / 1324 | +2.8% |
| chord4 | PAN 4 | steady | 1220 / 1475 / 1799 | 1149 / 1400 / 1704 | -5.8% |
| chord4 | PAN 4 | event | 1830 / 2275 / 2256 | 1882 / 2275 / 2250 | +2.9% |
| cluster8 | PAN 8 | steady | 1921 / 2200 / 2432 | 1770 / 2050 / 2574 | **-7.9%** |
| cluster8 | PAN 8 | event | 2871 / 3375 / 3370 | 2864 / 3250 / 3235 | -0.2% |
| roll | PAN 1 | steady | 630 / 875 / 1018 | 622 / 875 / 1054 | -1.3% |
| roll | PAN 1 | event | 1116 / 1400 / 1511 | 1125 / 1500 / 1611 | +0.8% |
| single | BELL 1 | steady | 337 / 425 / 521 | 299 / 375 / 501 | -11.1% |
| single | BELL 1 | event | 850 / 1100 / 1091 | 832 / 1125 / 1118 | -2.2% |
| chord4 | BELL 4 | steady | 1122 / 1350 / 1539 | 974 / 1225 / 1420 | **-13.2%** |
| chord4 | BELL 4 | event | 1741 / 2275 / 2253 | 1757 / 2175 / 2162 | +0.9% |
| cluster8 | BELL 8 | steady | 1923 / 2175 / 2386 | 1628 / 1875 / 2444 | **-15.3%** |
| cluster8 | BELL 8 | event | 2928 / 3600 / 3592 | 2866 / 3350 / 3325 | -2.1% |
| roll | BELL 1 | steady | 339 / 450 / 556 | 302 / 400 / 604 | -11.0% |
| roll | BELL 1 | event | 894 / 1125 / 1369 | 866 / 1200 / 1248 | -3.1% |

Full 6-voice rows also measured (PAN6 steady 1570 → 1459, -7.1%; BELL6 steady
1520 → 1299, -14.5%).

## Sustain fast-path acceptance

| Fixture | required | measured Δavg |
|---|---|---:|
| PAN cluster8 sustain | >= 7% (pref >= 10%) | **-7.9% — pass** |
| BELL chord sustain | >= 7% | **-13.2% — pass** |
| BELL cluster8 sustain | >= 7% | **-15.3% — pass** |

The fast path is **accepted** (all above the 7% minimum, none below the 4%
reject line). Candidate 13 is bit-identical and unchanged.

## Whole-callback telemetry (`[AUDIO]`, 5 s windows)

| Fixture window | c13 CPU % | c16 CPU % | c13 deadline | c16 deadline |
|---|---:|---:|---:|---:|
| PAN cluster8 | ~73.0 | ~67.5 | 40 | 40 |
| BELL chord | ~42.6 | ~36.8 | 44 | 44 |
| BELL cluster8 | ~72.3 | ~60.9 | 87 | 87 |

`timeout/short/tx = 0/0/0` throughout. CPU is the whole-audio-task callback load,
including the diagnostic reset blocks; it is not the fixture-only render cost.

## Gates (sustain avg/p99/max <= 1733/1733/2133 us; callback deadline = 0)

| Fixture | c16 avg | c16 p99 | c16 max | Gate |
|---|---:|---:|---:|---|
| PAN single | 509 | 675 | 853 | PASS |
| PAN chord4 | 1149 | 1400 | 1704 | PASS |
| PAN cluster8 | 1770 | 2050 | 2574 | **FAIL (avg/p99/max)** |
| PAN roll | 622 | 875 | 1054 | PASS |
| BELL single | 299 | 375 | 501 | PASS |
| BELL chord4 | 974 | 1225 | 1420 | PASS |
| BELL cluster8 | 1628 | 1875 | 2444 | **FAIL (p99/max)** |
| BELL roll | 302 | 400 | 604 | PASS |

Remaining failures are the 8-voice clusters only. The cluster event blocks also
carry 44 deadline misses (one per diagnostic reset block) and PAN cluster CPU
stays above the 65% hard gate. Per the M6.3.3 decision tree this is
**M6.3.3 FAIL**, so **Bell ablation remains NOT AUTHORIZED** and the next generic
hotspot is the modal bank / 8-voice cluster render (M6.3.2 section 4 residue).
