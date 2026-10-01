# M7.5.2 hardware evidence

ESP32-S3 on COM10, 240 MHz, 48 kHz/128 frames, candidate 25, Process6 ON, profiling OFF, UI/I2S active, BLE MIDI connected at 11.25 ms. Six fixtures of 8192 blocks each. Full callback includes event and transition blocks.

| Fixture | Avg us | p95 us | p99 us | Max us | CPU % | Misses | I2S timeouts/errors/short writes |
|---|---:|---:|---:|---:|---:|---:|---|
| single OFF | 270 | 320 | 375 | 952 | 10.12 | 0 | 0/0/0 |
| single ON | 349 | 395 | 450 | 791 | 13.09 | 0 | 0/0/0 |
| chord4 ON | 746 | 830 | 1025 | 1702 | 27.97 | 0 | 0/0/0 |
| cluster8 ON | 1248 | 1325 | 1650 | 2382 | 46.79 | 0 | 0/0/0 |
| phrase ON | 619 | 805 | 950 | 1410 | 23.21 | 0 | 0/0/0 |
| model switching | 349 | 395 | 450 | 1066 | 13.09 | 0 | 0/0/0 |

Fault-free qualification: **True**. Cluster8 worst-case margin: **284.7 us**. Preferred >250 us margin achieved: **True**.
Historical M7.5.1 cluster8: avg 1210 us, p95 1250 us, p99 1875 us, max 2658 us, CPU 45.37%, no misses, margin 8.7 us. New worst-case margin change: **276 us**. These are separate captures, not a paired isolated benchmark.

p95 uses combined callback histograms with 5 us upper edges; p99 uses AudioStats 75 us bins. Histograms contain 8191 callbacks; full averages/maxima/misses cover 8192, including the final callback.

| Fixture | Class | Timing | N | Avg us | p95 us | p99 us | Max us | Misses |
|---|---|---|---:|---:|---:|---:|---:|---:|
| single OFF | true_steady | inner | 8103 | 250.77 | 280 | 295 | 374 | 0 |
| single OFF | true_steady | callback | 8102 | 265.67 | 320 | 375 | 585 | 0 |
| single OFF | attack_tail | inner | 44 | 312.02 | 325 | 360 | 357 | 0 |
| single OFF | attack_tail | callback | 44 | 358.05 | 435 | 445 | 443 | 0 |
| single OFF | event | inner | 44 | 565.97 | 585 | 825 | 820 | 0 |
| single OFF | event | callback | 44 | 671.16 | 720 | 955 | 952 | 0 |
| single OFF | fixture_transition | inner | 1 | 513.38 | 515 | 515 | 514 | 0 |
| single OFF | fixture_transition | callback | 1 | 648.00 | 650 | 650 | 648 | 0 |
| chord4 ON | true_steady | inner | 8103 | 715.47 | 775 | 850 | 953 | 0 |
| chord4 ON | true_steady | callback | 8103 | 733.37 | 825 | 955 | 1128 | 0 |
| chord4 ON | attack_tail | inner | 44 | 976.70 | 1055 | 1100 | 1098 | 0 |
| chord4 ON | attack_tail | callback | 44 | 1031.16 | 1135 | 1215 | 1211 | 0 |
| chord4 ON | event | inner | 44 | 1322.08 | 1380 | 1495 | 1490 | 0 |
| chord4 ON | event | callback | 44 | 1443.61 | 1535 | 1705 | 1702 | 0 |
| chord4 ON | fixture_transition | inner | 1 | 471.88 | 475 | 475 | 472 | 0 |
| chord4 ON | fixture_transition | callback | 1 | 512.00 | 515 | 515 | 512 | 0 |
| cluster8 ON | true_steady | inner | 8103 | 1216.93 | 1275 | 1355 | 1491 | 0 |
| cluster8 ON | true_steady | callback | 8103 | 1234.95 | 1315 | 1455 | 1654 | 0 |
| cluster8 ON | attack_tail | inner | 44 | 1650.68 | 1665 | 1795 | 1792 | 0 |
| cluster8 ON | attack_tail | callback | 44 | 1703.25 | 1745 | 2030 | 2026 | 0 |
| cluster8 ON | event | inner | 44 | 2144.77 | 2190 | 2235 | 2231 | 0 |
| cluster8 ON | event | callback | 44 | 2263.70 | 2325 | 2385 | 2382 | 0 |
| cluster8 ON | fixture_transition | inner | 1 | 464.31 | 470 | 470 | 465 | 0 |
| cluster8 ON | fixture_transition | callback | 1 | 512.00 | 515 | 515 | 512 | 0 |
| phrase ON | true_steady | inner | 8131 | 635.36 | 745 | 810 | 952 | 0 |
| phrase ON | true_steady | callback | 8130 | 653.27 | 785 | 910 | 1105 | 0 |
| phrase ON | attack_tail | inner | 30 | 756.52 | 800 | 875 | 874 | 0 |
| phrase ON | attack_tail | callback | 30 | 792.00 | 885 | 940 | 938 | 0 |
| phrase ON | event | inner | 30 | 1072.08 | 1210 | 1305 | 1301 | 0 |
| phrase ON | event | callback | 30 | 1136.73 | 1335 | 1415 | 1410 | 0 |
| phrase ON | fixture_transition | inner | 1 | 578.08 | 580 | 580 | 579 | 0 |
| phrase ON | fixture_transition | callback | 1 | 705.00 | 710 | 710 | 705 | 0 |
| model switching | true_steady | inner | 8103 | 328.53 | 355 | 370 | 404 | 0 |
| model switching | true_steady | callback | 8103 | 343.51 | 390 | 445 | 1008 | 0 |
| model switching | attack_tail | inner | 44 | 391.44 | 415 | 425 | 423 | 0 |
| model switching | attack_tail | callback | 44 | 441.91 | 505 | 535 | 530 | 0 |
| model switching | event | inner | 44 | 889.59 | 935 | 950 | 945 | 0 |
| model switching | event | callback | 44 | 943.61 | 1015 | 1070 | 1066 | 0 |
| model switching | fixture_transition | inner | 1 | 445.46 | 450 | 450 | 446 | 0 |
| model switching | fixture_transition | callback | 1 | 471.00 | 475 | 475 | 471 | 0 |
| single ON | true_steady | inner | 8103 | 328.51 | 355 | 370 | 429 | 0 |
| single ON | true_steady | callback | 8103 | 343.40 | 395 | 445 | 604 | 0 |
| single ON | attack_tail | inner | 44 | 394.50 | 425 | 440 | 437 | 0 |
| single ON | attack_tail | callback | 44 | 442.18 | 525 | 535 | 533 | 0 |
| single ON | event | inner | 44 | 629.45 | 655 | 675 | 670 | 0 |
| single ON | event | callback | 44 | 725.45 | 790 | 795 | 791 | 0 |
| single ON | fixture_transition | inner | 1 | 471.13 | 475 | 475 | 472 | 0 |
| single ON | fixture_transition | callback | 1 | 525.00 | 530 | 530 | 525 | 0 |

All development captures are retained below; unsuccessful versions are not qualification evidence for the final source.

| Capture | Cluster avg us | Max us | Misses |
|---|---:|---:|---:|
| batch0_final_raw.log | 1278 | 2940 | 1 |
| batch0_inline_raw.log | 1249 | 2509 | 0 |
| batch0_raw.log | 1329 | 2630 | 0 |
| batch0_stable8_raw.log | 1277 | 2811 | 2 |
