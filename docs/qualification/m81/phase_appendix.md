# M8.1 phase profile appendix

Actual ESP32-S3 cycle measurements / 240, in microseconds per sampled block. These builds have phase probes enabled and are diagnostic, not timing qualification. Allocator includes modal/exciter/energy/sympathetic; mix includes body. Do not add parents to their children. UDU has its own renderer and no modal/exciter probes; zero is not a free custom renderer. Its steady sample population includes natural voice expiry.

| Model / fixture | Class | allocator | modal | exciter | energy | sympathetic | body | mix | limiter | pcm |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| PAN single | true_steady | 294.80 | 132.81 | 0.00 | 0.69 | 24.53 | 97.97 | 140.10 | 77.03 | 17.07 |
| PAN single | attack_tail | 372.57 | 132.81 | 48.61 | 0.69 | 24.47 | 97.60 | 140.01 | 69.33 | 17.07 |
| PAN single | event | 418.52 | 132.80 | 94.28 | 0.67 | 24.31 | 97.64 | 139.63 | 70.36 | 17.07 |
| PAN chord4 | true_steady | 803.34 | 531.42 | 0.00 | 2.75 | 24.54 | 97.63 | 140.93 | 242.90 | 17.07 |
| PAN chord4 | attack_tail | 1115.50 | 531.22 | 197.35 | 2.75 | 24.53 | 97.63 | 140.83 | 249.99 | 17.07 |
| PAN chord4 | event | 1230.11 | 531.77 | 312.07 | 2.67 | 24.29 | 97.62 | 140.67 | 263.31 | 17.07 |
| PAN cluster8 | true_steady | 1365.14 | 1063.36 | 0.00 | 5.50 | 24.54 | 97.63 | 140.83 | 249.90 | 17.07 |
| PAN cluster8 | attack_tail | 1791.38 | 1062.73 | 344.34 | 5.50 | 37.87 | 97.67 | 140.87 | 281.89 | 17.07 |
| PAN cluster8 | event | 2062.79 | 1063.32 | 612.63 | 5.33 | 37.59 | 98.36 | 141.86 | 280.51 | 17.30 |
| PAN rapid | attack_tail | 364.45 | 132.81 | 41.16 | 0.68 | 24.53 | 97.70 | 139.86 | 240.27 | 17.12 |
| PAN rapid | event | 400.14 | 132.81 | 76.98 | 0.67 | 24.53 | 97.74 | 139.92 | 242.88 | 17.08 |
| VIBRAPHONE cluster8 | true_steady | 1085.17 | 849.47 | 0.00 | 0.00 | 0.00 | 10.67 | 55.52 | 69.69 | 17.07 |
| VIBRAPHONE cluster8 | event | 2006.75 | 849.70 | 745.54 | 0.00 | 0.00 | 10.68 | 55.48 | 69.19 | 17.07 |
| MBIRA cluster8 | true_steady | 1241.86 | 858.25 | 0.00 | 4.50 | 0.00 | 54.95 | 138.88 | 70.61 | 17.07 |
| MBIRA cluster8 | event | 1545.49 | 857.91 | 342.58 | 4.50 | 0.00 | 54.95 | 140.14 | 69.47 | 17.07 |
| UDU cluster8 | true_steady | 918.25 | 0.00 | 0.00 | 0.00 | 0.00 | 10.67 | 61.35 | 69.34 | 17.07 |
| UDU cluster8 | attack_tail | 1213.13 | 0.00 | 0.00 | 0.00 | 0.00 | 10.67 | 61.56 | 69.35 | 17.07 |
| UDU cluster8 | event | 1234.04 | 0.00 | 0.00 | 0.00 | 0.00 | 10.67 | 61.34 | 68.88 | 17.07 |
| PAN cluster8 | release_tail | 331.57 | 176.08 | 0.00 | 0.91 | 24.53 | 97.62 | 139.93 | 238.95 | 17.07 |

## Phases from the largest sampled PAN cluster callback by class

These are all phases of one largest sampled block per class, not separately chosen phase maxima. Callback-wide normal maxima are reported separately in the main report.

| Class | allocator | modal | exciter | energy | sympathetic | body | mix | limiter | pcm |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| true_steady | 1365.12 | 1062.58 | 0.00 | 5.50 | 24.54 | 97.60 | 140.80 | 330.70 | 17.07 |
| attack_tail | 1836.66 | 1062.62 | 390.99 | 5.50 | 37.87 | 97.78 | 141.00 | 333.24 | 17.07 |
| event | 2133.56 | 1062.40 | 684.22 | 5.33 | 37.59 | 97.60 | 140.65 | 314.63 | 17.07 |

Full trigger subphases, kernel counts and class distributions: [PAN](phases_pan_complete/summary.json), [comparisons](phases/summary.json), [release](phases_release/summary.json).

The older `profile` directory was an uninstrumented, incomplete initial control, despite its label. It is not phase evidence. Initial `phases` PAN sampling omitted attack_tail; `phases_pan_complete` fixes that by sampling every active-exciter block. Legacy profile-only IDs 16–18 are guarded out of M8 fixtures to retain frozen UDU definitions.
