# M7.5.1 hardware evidence

ESP32-S3 on COM10, 240 MHz, 48 kHz, 128-frame blocks, candidate 25, Process6 ON, phase profiling OFF. UI/I2S active; BLE connected at 11.25 ms. Each fixture spans 8192 blocks. All six final fixtures have zero deadline misses, I2S timeouts/errors/short writes, PCM clamps, modal saturation, bad voice counts, and BLE event loss.

| Fixture | Callback avg us | p95 us | I2S p99 us | Callback max us | CPU % | Misses | I2S faults | Clamps / sat |
|---|---:|---:|---:|---:|---:|---:|---|---|
| single motor OFF | 272 | 305 | 375 | 929 | 10.20 | 0 | 0 / 0 / 0 | 0 / 0 |
| single motor ON | 338 | 370 | 450 | 1366 | 12.67 | 0 | 0 / 0 / 0 | 0 / 0 |
| chord4 motor ON | 717 | 765 | 1050 | 1606 | 26.88 | 0 | 0 / 0 / 0 | 0 / 0 |
| phrase motor ON | 703 | 755 | 925 | 1440 | 26.36 | 0 | 0 / 0 / 0 | 0 / 0 |
| cluster8 motor ON | 1210 | 1250 | 1875 | 2658 | 45.37 | 0 | 0 / 0 / 0 | 0 / 0 |
| model switching | 336 | 365 | 475 | 1205 | 12.60 | 0 | 0 / 0 / 0 | 0 / 0 |

p95 is reconstructed from the combined callback histograms (5 us bin upper edges); p99 is the existing AudioStats value (75 us bins). The final callback is outside the class snapshot, which covers 8191 blocks. Maxima come from full AudioStats.

| Fixture | Class | Timing | N | Avg us | p95 us | p99 us | Max us |
|---|---|---|---:|---:|---:|---:|---:|
| single motor OFF | true_steady | inner | 8059 | 252.16 | 265 | 285 | 321 |
| single motor OFF | true_steady | callback | 8058 | 266.66 | 300 | 360 | 488 |
| single motor OFF | attack_tail | inner | 88 | 300.52 | 335 | 380 | 376 |
| single motor OFF | attack_tail | callback | 88 | 325.78 | 400 | 525 | 521 |
| single motor OFF | event | inner | 44 | 679.06 | 705 | 710 | 707 |
| single motor OFF | event | callback | 44 | 816.77 | 890 | 930 | 929 |
| single motor OFF | fixture_transition | inner | 1 | 598.96 | 600 | 600 | 599 |
| single motor OFF | fixture_transition | callback | 1 | 761.00 | 765 | 765 | 761 |
| chord4 motor ON | true_steady | inner | 8059 | 687.55 | 720 | 810 | 911 |
| chord4 motor ON | true_steady | callback | 8059 | 704.69 | 760 | 915 | 1131 |
| chord4 motor ON | attack_tail | inner | 88 | 899.03 | 1110 | 1160 | 1156 |
| chord4 motor ON | attack_tail | callback | 88 | 926.42 | 1160 | 1210 | 1208 |
| chord4 motor ON | event | inner | 44 | 1358.43 | 1395 | 1445 | 1442 |
| chord4 motor ON | event | callback | 44 | 1482.93 | 1550 | 1610 | 1606 |
| chord4 motor ON | fixture_transition | inner | 1 | 471.05 | 475 | 475 | 472 |
| chord4 motor ON | fixture_transition | callback | 1 | 521.00 | 525 | 525 | 521 |
| cluster8 motor ON | true_steady | inner | 8059 | 1174.36 | 1205 | 1280 | 1396 |
| cluster8 motor ON | true_steady | callback | 8059 | 1191.31 | 1245 | 1365 | 1572 |
| cluster8 motor ON | attack_tail | inner | 88 | 1555.06 | 1950 | 2100 | 2097 |
| cluster8 motor ON | attack_tail | callback | 88 | 1581.83 | 2000 | 2180 | 2177 |
| cluster8 motor ON | event | inner | 44 | 2252.55 | 2375 | 2520 | 2519 |
| cluster8 motor ON | event | callback | 44 | 2386.80 | 2560 | 2660 | 2658 |
| cluster8 motor ON | fixture_transition | inner | 1 | 442.83 | 445 | 445 | 443 |
| cluster8 motor ON | fixture_transition | callback | 1 | 474.00 | 475 | 475 | 474 |
| phrase motor ON | true_steady | inner | 8101 | 641.93 | 715 | 810 | 957 |
| phrase motor ON | true_steady | callback | 8100 | 659.04 | 750 | 905 | 1060 |
| phrase motor ON | attack_tail | inner | 60 | 723.13 | 805 | 970 | 968 |
| phrase motor ON | attack_tail | callback | 60 | 753.22 | 850 | 1090 | 1087 |
| phrase motor ON | event | inner | 30 | 1093.75 | 1215 | 1320 | 1318 |
| phrase motor ON | event | callback | 30 | 1158.63 | 1280 | 1445 | 1440 |
| phrase motor ON | fixture_transition | inner | 1 | 815.79 | 820 | 820 | 816 |
| phrase motor ON | fixture_transition | callback | 1 | 1017.00 | 1020 | 1020 | 1017 |
| model switching | true_steady | inner | 8059 | 313.33 | 325 | 355 | 404 |
| model switching | true_steady | callback | 8059 | 328.17 | 360 | 450 | 982 |
| model switching | attack_tail | inner | 88 | 367.24 | 420 | 445 | 442 |
| model switching | attack_tail | callback | 88 | 393.73 | 480 | 535 | 534 |
| model switching | event | inner | 44 | 987.93 | 1025 | 1045 | 1041 |
| model switching | event | callback | 44 | 1072.95 | 1120 | 1210 | 1205 |
| model switching | fixture_transition | inner | 1 | 436.24 | 440 | 440 | 437 |
| model switching | fixture_transition | callback | 1 | 459.00 | 460 | 460 | 459 |
| single motor ON | true_steady | inner | 8059 | 313.38 | 325 | 355 | 420 |
| single motor ON | true_steady | callback | 8059 | 328.31 | 360 | 445 | 618 |
| single motor ON | attack_tail | inner | 88 | 365.51 | 415 | 450 | 445 |
| single motor ON | attack_tail | callback | 88 | 389.64 | 460 | 525 | 524 |
| single motor ON | event | inner | 44 | 794.18 | 815 | 1195 | 1194 |
| single motor ON | event | callback | 44 | 927.16 | 995 | 1370 | 1366 |
| single motor ON | fixture_transition | inner | 1 | 483.21 | 485 | 485 | 484 |
| single motor ON | fixture_transition | callback | 1 | 516.00 | 520 | 520 | 516 |

Single steady inner render: OFF 252.16 us, ON 313.38 us, added tube/motor path **61.22 us/block** (2.30% of a block). These are separate fixtures and include scheduling variation. M7.5 connected old-motor delta was 11.69 us/block; the historical incremental comparison is approximately 49.53 us/block. This is not an isolated paired measurement against the old firmware.

Polyphonic cost remains higher: chord4 steady 687.55 us versus historical M7.5 dry 604.55 us (+83.00 us); cluster8 1174.36 versus historical dry 1067.11 us (+107.25 us). The preference for less than 50 us additional cost is not established for polyphony. All final measured fixtures meet the deadline, but cluster8 maximum 2658 us leaves only about 9 us beneath 2666.7 us.

Repeated MARIMBA -> VIBRAPHONE paired switches remain inside the measured event callback; no tables or coefficients are regenerated on selection. Use the event rows above for the exact switch maxima.

The initial sample-call tube renderer in batch0_raw.log produced one cluster event deadline miss (3094 us callback). It was superseded by the fused sustain/attack block renderer. That failed capture is preserved and is excluded from the final qualification totals.
