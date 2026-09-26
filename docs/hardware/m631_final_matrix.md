
### m631_candidate_final_connected.log

| Model/fixture | Class | N | Avg us | P99 us | Max us | Deadline | Bad voices/BLE lost |
|---|---|---:|---:|---:|---:|---:|---|
| PAN/0 (0 voices) | event | 44 | 339.81 | 450 | 445 | 0 | 0/0 |
| PAN/0 (0 voices) | steady | 8148 | 342.71 | 475 | 549 | 0 | 0/0 |
| PAN/1 (1 voices) | event | 44 | 1036.97 | 1225 | 1212 | 0 | 0/0 |
| PAN/1 (1 voices) | steady | 8148 | 534.78 | 700 | 917 | 0 | 0/0 |
| PAN/2 (2 voices) | event | 44 | 1421.19 | 1825 | 1811 | 0 | 0/0 |
| PAN/2 (2 voices) | steady | 8148 | 901.11 | 1200 | 1364 | 0 | 0/0 |
| PAN/3 (4 voices) | event | 44 | 2008.85 | 2700 | 2683 | 1 | 0/0 |
| PAN/3 (4 voices) | steady | 8148 | 1272.22 | 1575 | 1858 | 0 | 0/0 |
| PAN/4 (6 voices) | event | 44 | 2583.92 | 3150 | 3143 | 7 | 0/0 |
| PAN/4 (6 voices) | steady | 8148 | 1643.32 | 1925 | 2252 | 0 | 0/0 |
| PAN/5 (8 voices) | event | 44 | 3223.99 | 3825 | 3800 | 44 | 0/0 |
| PAN/5 (8 voices) | steady | 8148 | 2013.02 | 2325 | 2602 | 0 | 0/0 |
| PAN/6 (4 voices) | event | 44 | 2014.45 | 2650 | 2627 | 0 | 0/0 |
| PAN/6 (4 voices) | steady | 8148 | 1273.29 | 1575 | 1915 | 0 | 0/0 |
| PAN/7 (1 voices) | event | 216 | 1147.34 | 1550 | 1560 | 0 | 0/0 |
| PAN/7 (1 voices) | steady | 7976 | 650.32 | 900 | 1036 | 0 | 0/0 |
| BELL/8 (0 voices) | event | 44 | 150.24 | 225 | 218 | 0 | 0/0 |
| BELL/8 (0 voices) | steady | 8148 | 142.44 | 250 | 339 | 0 | 0/0 |
| BELL/9 (1 voices) | event | 44 | 927.40 | 1325 | 1316 | 0 | 0/0 |
| BELL/9 (1 voices) | steady | 8148 | 360.28 | 475 | 540 | 0 | 0/0 |
| BELL/10 (2 voices) | event | 44 | 1356.04 | 1725 | 1716 | 0 | 0/0 |
| BELL/10 (2 voices) | steady | 8148 | 764.72 | 1025 | 1281 | 0 | 0/0 |
| BELL/11 (4 voices) | event | 44 | 1959.63 | 2200 | 2185 | 0 | 0/0 |
| BELL/11 (4 voices) | steady | 8148 | 1202.95 | 1475 | 1630 | 0 | 0/0 |
| BELL/12 (6 voices) | event | 44 | 2599.54 | 2850 | 2842 | 4 | 0/0 |
| BELL/12 (6 voices) | steady | 8148 | 1640.79 | 1925 | 2118 | 0 | 0/0 |
| BELL/13 (8 voices) | event | 44 | 3243.82 | 3525 | 3515 | 44 | 0/0 |
| BELL/13 (8 voices) | steady | 8148 | 2076.65 | 2375 | 2648 | 0 | 0/0 |
| BELL/14 (4 voices) | event | 44 | 2004.31 | 2475 | 2473 | 0 | 0/0 |
| BELL/14 (4 voices) | steady | 8148 | 1206.22 | 1475 | 1784 | 0 | 0/0 |
| BELL/15 (1 voices) | event | 216 | 895.48 | 1225 | 1244 | 0 | 0/0 |
| BELL/15 (1 voices) | steady | 7976 | 362.64 | 475 | 541 | 0 | 0/0 |

PAN: measured zero-voice fixed cost 342.71 us; least-squares intercept 387.29 us, slope 208.73 us/voice.

Incremental cost per additional voice:
- 0->1: 192.07 us/voice
- 1->2: 366.33 us/voice
- 2->4: 185.56 us/voice
- 4->6: 185.55 us/voice
- 6->8: 184.85 us/voice

BELL: measured zero-voice fixed cost 142.44 us; least-squares intercept 183.79 us, slope 242.15 us/voice.

Incremental cost per additional voice:
- 0->1: 217.84 us/voice
- 1->2: 404.44 us/voice
- 2->4: 219.12 us/voice
- 4->6: 218.92 us/voice
- 6->8: 217.93 us/voice
