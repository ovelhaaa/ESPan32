# M7.5.3 hardware evidence

ESP32-S3 on COM10, 240 MHz, 48 kHz/128 frames, candidate 25, Process6 ON, profiling OFF, UI/I2S active, BLE MIDI connected at 11.25 ms. Six fixtures of 8192 blocks each. Full callback includes event and transition blocks.

| Fixture | Avg us | p95 us | p99 us | Max us | CPU % | Misses | I2S timeouts/errors/short writes |
|---|---:|---:|---:|---:|---:|---:|---|
| single OFF | 263 | 295 | 350 | 690 | 9.86 | 0 | 0/0/0 |
| single ON | 312 | 340 | 425 | 823 | 11.70 | 0 | 0/0/0 |
| chord4 ON | 680 | 735 | 925 | 1626 | 25.50 | 0 | 0/0/0 |
| cluster8 ON | 1121 | 1160 | 1550 | 2212 | 42.03 | 0 | 0/0/0 |
| phrase ON | 560 | 720 | 875 | 1059 | 21.00 | 0 | 0/0/0 |
| model switching | 312 | 335 | 425 | 1117 | 11.70 | 0 | 0/0/0 |

Fault-free qualification: **True**. Cluster8 worst-case margin: **454.7 us**. Preferred >250 us margin achieved: **True**.
M7.5.2 cluster8: avg 1248 us, p95 1325 us, p99 1650 us, max 2382 us, CPU 46.79%, zero misses, margin 284.7 us. Final max change: **170 us**; required >=M7.5.2 margin achieved: **True**. Historical M7.5.1 max was 2658 us. Separate captures include scheduling variability.

p95 uses combined callback histograms with 5 us upper edges; p99 uses AudioStats 75 us bins. Histograms contain 8191 callbacks; full averages/maxima/misses cover 8192, including the final callback.

| Fixture | Class | Timing | N | Avg us | p95 us | p99 us | Max us | Misses |
|---|---|---|---:|---:|---:|---:|---:|---:|
| single OFF | true_steady | inner | 8103 | 245.93 | 255 | 275 | 372 | 0 |
| single OFF | true_steady | callback | 8102 | 259.29 | 285 | 345 | 676 | 0 |
| single OFF | attack_tail | inner | 44 | 313.41 | 335 | 340 | 335 | 0 |
| single OFF | attack_tail | callback | 44 | 343.82 | 390 | 425 | 424 | 0 |
| single OFF | event | inner | 44 | 560.81 | 575 | 580 | 577 | 0 |
| single OFF | event | callback | 44 | 650.16 | 675 | 695 | 690 | 0 |
| single OFF | fixture_transition | inner | 1 | 520.07 | 525 | 525 | 521 | 0 |
| single OFF | fixture_transition | callback | 1 | 635.00 | 640 | 640 | 635 | 0 |
| chord4 ON | true_steady | inner | 8103 | 657.15 | 695 | 785 | 903 | 0 |
| chord4 ON | true_steady | callback | 8103 | 673.62 | 730 | 875 | 1057 | 0 |
| chord4 ON | attack_tail | inner | 44 | 916.06 | 960 | 1070 | 1069 | 0 |
| chord4 ON | attack_tail | callback | 44 | 952.45 | 1020 | 1245 | 1244 | 0 |
| chord4 ON | event | inner | 44 | 1244.63 | 1375 | 1510 | 1505 | 0 |
| chord4 ON | event | callback | 44 | 1344.57 | 1535 | 1630 | 1626 | 0 |
| chord4 ON | fixture_transition | inner | 1 | 448.07 | 450 | 450 | 449 | 0 |
| chord4 ON | fixture_transition | callback | 1 | 484.00 | 485 | 485 | 484 | 0 |
| cluster8 ON | true_steady | inner | 8103 | 1089.83 | 1125 | 1215 | 1338 | 0 |
| cluster8 ON | true_steady | callback | 8103 | 1106.40 | 1155 | 1300 | 1510 | 0 |
| cluster8 ON | attack_tail | inner | 44 | 1545.53 | 1595 | 1695 | 1690 | 0 |
| cluster8 ON | attack_tail | callback | 44 | 1575.98 | 1640 | 1780 | 1778 | 0 |
| cluster8 ON | event | inner | 44 | 2019.49 | 2065 | 2090 | 2087 | 0 |
| cluster8 ON | event | callback | 44 | 2117.11 | 2195 | 2215 | 2212 | 0 |
| cluster8 ON | fixture_transition | inner | 1 | 437.06 | 440 | 440 | 438 | 0 |
| cluster8 ON | fixture_transition | callback | 1 | 466.00 | 470 | 470 | 466 | 0 |
| phrase ON | true_steady | inner | 8131 | 582.15 | 680 | 755 | 905 | 0 |
| phrase ON | true_steady | callback | 8130 | 598.62 | 715 | 840 | 1030 | 0 |
| phrase ON | attack_tail | inner | 30 | 704.01 | 785 | 790 | 787 | 0 |
| phrase ON | attack_tail | callback | 30 | 741.83 | 860 | 905 | 903 | 0 |
| phrase ON | event | inner | 30 | 975.34 | 1025 | 1030 | 1025 | 0 |
| phrase ON | event | callback | 30 | 1006.40 | 1060 | 1060 | 1059 | 0 |
| phrase ON | fixture_transition | inner | 1 | 838.55 | 840 | 840 | 839 | 0 |
| phrase ON | fixture_transition | callback | 1 | 1037.00 | 1040 | 1040 | 1037 | 0 |
| model switching | true_steady | inner | 8103 | 292.47 | 300 | 325 | 384 | 0 |
| model switching | true_steady | callback | 8103 | 305.96 | 330 | 405 | 667 | 0 |
| model switching | attack_tail | inner | 44 | 354.78 | 375 | 380 | 375 | 0 |
| model switching | attack_tail | callback | 44 | 413.39 | 480 | 530 | 527 | 0 |
| model switching | event | inner | 44 | 864.48 | 895 | 1030 | 1026 | 0 |
| model switching | event | callback | 44 | 909.91 | 990 | 1120 | 1117 | 0 |
| model switching | fixture_transition | inner | 1 | 465.13 | 470 | 470 | 466 | 0 |
| model switching | fixture_transition | callback | 1 | 551.00 | 555 | 555 | 551 | 0 |
| single ON | true_steady | inner | 8103 | 292.45 | 300 | 330 | 371 | 0 |
| single ON | true_steady | callback | 8103 | 306.12 | 330 | 410 | 533 | 0 |
| single ON | attack_tail | inner | 44 | 355.88 | 370 | 380 | 376 | 0 |
| single ON | attack_tail | callback | 44 | 398.84 | 435 | 440 | 436 | 0 |
| single ON | event | inner | 44 | 612.59 | 640 | 660 | 656 | 0 |
| single ON | event | callback | 44 | 705.16 | 750 | 825 | 823 | 0 |
| single ON | fixture_transition | inner | 1 | 455.73 | 460 | 460 | 456 | 0 |
| single ON | fixture_transition | callback | 1 | 494.00 | 495 | 495 | 494 | 0 |

All development captures are retained below; unsuccessful versions are not qualification evidence for the final source.

| Capture | Cluster avg us | Max us | Misses |
|---|---:|---:|---:|
| batch0_raw.log | 1213 | 2436 | 0 |
| batch0_segment_raw.log | 1163 | 2490 | 0 |
