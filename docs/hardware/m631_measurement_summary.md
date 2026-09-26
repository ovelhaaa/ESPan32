
### m631_baseline_connected.log

| Model/fixture | Class | N | Avg us | P99 us | Max us | Deadline | Bad voices/BLE lost |
|---|---|---:|---:|---:|---:|---:|---|
| PAN/0 (0 voices) | event | 44 | 356.57 | 550 | 540 | 0 | 0/0 |
| PAN/0 (0 voices) | steady | 8148 | 351.40 | 575 | 641 | 0 | 0/0 |
| PAN/1 (1 voices) | event | 44 | 1175.27 | 1625 | 1600 | 0 | 0/0 |
| PAN/1 (1 voices) | steady | 8148 | 588.69 | 925 | 1165 | 0 | 0/0 |
| PAN/2 (2 voices) | event | 44 | 1629.64 | 2225 | 2204 | 0 | 0/0 |
| PAN/2 (2 voices) | steady | 8148 | 998.91 | 1500 | 1767 | 0 | 0/0 |
| PAN/3 (4 voices) | event | 44 | 2267.85 | 2925 | 2922 | 7 | 0/0 |
| PAN/3 (4 voices) | steady | 8148 | 1450.62 | 2050 | 2259 | 0 | 0/0 |
| PAN/4 (6 voices) | event | 44 | 2795.00 | 3475 | 3450 | 44 | 0/0 |
| PAN/4 (6 voices) | steady | 8148 | 1906.76 | 2675 | 2898 | 70 | 0/0 |
| PAN/5 (8 voices) | event | 44 | 3580.25 | 4725 | 4709 | 44 | 0/0 |
| PAN/5 (8 voices) | steady | 8148 | 2347.52 | 3250 | 3574 | 1061 | 0/0 |
| PAN/6 (4 voices) | event | 44 | 2272.31 | 2950 | 2938 | 8 | 0/0 |
| PAN/6 (4 voices) | steady | 8148 | 1453.07 | 2075 | 2270 | 0 | 0/0 |
| PAN/7 (1 voices) | event | 216 | 1228.38 | 1825 | 2040 | 0 | 0/0 |
| PAN/7 (1 voices) | steady | 7976 | 705.69 | 1125 | 1311 | 0 | 0/0 |
| BELL/8 (0 voices) | event | 44 | 145.55 | 225 | 213 | 0 | 0/0 |
| BELL/8 (0 voices) | steady | 8148 | 141.01 | 225 | 335 | 0 | 0/0 |
| BELL/9 (1 voices) | event | 44 | 1003.55 | 1450 | 1434 | 0 | 0/0 |
| BELL/9 (1 voices) | steady | 8148 | 412.42 | 625 | 744 | 0 | 0/0 |
| BELL/10 (2 voices) | event | 44 | 1407.18 | 1900 | 1892 | 0 | 0/0 |
| BELL/10 (2 voices) | steady | 8148 | 869.14 | 1325 | 1575 | 0 | 0/0 |
| BELL/11 (4 voices) | event | 44 | 2254.94 | 3025 | 3005 | 9 | 0/0 |
| BELL/11 (4 voices) | steady | 8148 | 1406.86 | 2000 | 2186 | 0 | 0/0 |
| BELL/12 (6 voices) | event | 44 | 2981.06 | 3650 | 3635 | 44 | 0/0 |
| BELL/12 (6 voices) | steady | 8148 | 1944.03 | 2725 | 2926 | 184 | 0/0 |
| BELL/13 (8 voices) | event | 44 | 3751.87 | 4725 | 4721 | 44 | 0/0 |
| BELL/13 (8 voices) | steady | 8148 | 2471.33 | 3425 | 3639 | 1236 | 0/0 |
| BELL/14 (4 voices) | event | 44 | 2227.38 | 3000 | 2987 | 6 | 0/0 |
| BELL/14 (4 voices) | steady | 8148 | 1410.52 | 2000 | 2150 | 0 | 0/0 |
| BELL/15 (1 voices) | event | 216 | 1014.07 | 1475 | 1525 | 0 | 0/0 |
| BELL/15 (1 voices) | steady | 7976 | 414.77 | 650 | 744 | 0 | 0/0 |

PAN: measured zero-voice fixed cost 351.40 us; least-squares intercept 400.38 us, slope 249.60 us/voice.

Incremental cost per additional voice:
- 0->1: 237.29 us/voice
- 1->2: 410.22 us/voice
- 2->4: 225.85 us/voice
- 4->6: 228.07 us/voice
- 6->8: 220.38 us/voice

BELL: measured zero-voice fixed cost 141.01 us; least-squares intercept 186.48 us, slope 291.71 us/voice.

Incremental cost per additional voice:
- 0->1: 271.41 us/voice
- 1->2: 456.72 us/voice
- 2->4: 268.86 us/voice
- 4->6: 268.59 us/voice
- 6->8: 263.65 us/voice

### m631_profile_connected.log

| Model/fixture | Class | N | Avg us | P99 us | Max us | Deadline | Bad voices/BLE lost |
|---|---|---:|---:|---:|---:|---:|---|
| PAN/0 (0 voices) | event | 6 | 423.27 | 725 | 700 | 0 | 0/0 |
| PAN/0 (0 voices) | steady | 1018 | 367.42 | 600 | 649 | 0 | 0/0 |
| PAN/1 (1 voices) | event | 6 | 1367.44 | 1725 | 1723 | 0 | 0/0 |
| PAN/1 (1 voices) | steady | 1018 | 626.20 | 975 | 1071 | 0 | 0/0 |
| PAN/2 (2 voices) | event | 6 | 1571.20 | 1675 | 1655 | 0 | 0/0 |
| PAN/2 (2 voices) | steady | 1018 | 1107.86 | 1625 | 1687 | 0 | 0/0 |
| PAN/3 (4 voices) | event | 6 | 2362.56 | 2700 | 2682 | 1 | 0/0 |
| PAN/3 (4 voices) | steady | 1018 | 1608.39 | 2200 | 2343 | 0 | 0/0 |
| PAN/4 (6 voices) | event | 6 | 3017.19 | 3525 | 3501 | 6 | 0/0 |
| PAN/4 (6 voices) | steady | 1018 | 2104.19 | 2875 | 2980 | 50 | 0/0 |
| PAN/5 (8 voices) | event | 6 | 3616.63 | 4000 | 3975 | 6 | 0/0 |
| PAN/5 (8 voices) | steady | 1018 | 2596.33 | 3475 | 3535 | 294 | 0/0 |
| PAN/6 (4 voices) | event | 6 | 2342.61 | 2875 | 2854 | 1 | 0/0 |
| PAN/6 (4 voices) | steady | 1018 | 1623.54 | 2225 | 2293 | 0 | 0/0 |
| PAN/7 (1 voices) | event | 27 | 1197.45 | 1625 | 1606 | 0 | 0/0 |
| PAN/7 (1 voices) | steady | 997 | 634.75 | 975 | 1004 | 0 | 0/0 |
| BELL/8 (0 voices) | event | 6 | 152.81 | 200 | 178 | 0 | 0/0 |
| BELL/8 (0 voices) | steady | 1018 | 162.90 | 300 | 333 | 0 | 0/0 |
| BELL/9 (1 voices) | event | 6 | 961.68 | 1025 | 1001 | 0 | 0/0 |
| BELL/9 (1 voices) | steady | 1018 | 453.98 | 725 | 774 | 0 | 0/0 |
| BELL/10 (2 voices) | event | 6 | 1747.97 | 2175 | 2153 | 0 | 0/0 |
| BELL/10 (2 voices) | steady | 1018 | 947.69 | 1450 | 1525 | 0 | 0/0 |
| BELL/11 (4 voices) | event | 6 | 2215.48 | 2375 | 2356 | 0 | 0/0 |
| BELL/11 (4 voices) | steady | 1018 | 1532.98 | 2150 | 2272 | 0 | 0/0 |
| BELL/12 (6 voices) | event | 6 | 3211.12 | 4100 | 4094 | 6 | 0/0 |
| BELL/12 (6 voices) | steady | 1018 | 2129.98 | 2950 | 3104 | 61 | 0/0 |
| BELL/13 (8 voices) | event | 6 | 3869.12 | 4800 | 4796 | 6 | 0/0 |
| BELL/13 (8 voices) | steady | 1018 | 2696.37 | 3675 | 3840 | 294 | 0/0 |
| BELL/14 (4 voices) | event | 6 | 2455.26 | 2725 | 2719 | 2 | 0/0 |
| BELL/14 (4 voices) | steady | 1018 | 1562.08 | 2200 | 2250 | 0 | 0/0 |
| BELL/15 (1 voices) | event | 27 | 1046.96 | 1250 | 1235 | 0 | 0/0 |
| BELL/15 (1 voices) | steady | 997 | 462.79 | 725 | 797 | 0 | 0/0 |
| PAN/16 (8 voices) | event | 6 | 3481.86 | 3575 | 3563 | 6 | 0/0 |
| PAN/16 (8 voices) | steady | 1018 | 2444.98 | 3375 | 3728 | 231 | 0/0 |
| BELL/17 (8 voices) | event | 6 | 3643.20 | 4250 | 4225 | 6 | 0/0 |
| BELL/17 (8 voices) | steady | 1018 | 2552.50 | 3450 | 3476 | 291 | 0/0 |
| BELL/18 (8 voices) | event | 6 | 3477.65 | 4275 | 4266 | 6 | 0/0 |
| BELL/18 (8 voices) | steady | 1018 | 2372.53 | 3150 | 3376 | 261 | 0/0 |

PAN: measured zero-voice fixed cost 367.42 us; least-squares intercept 426.53 us, slope 278.63 us/voice.

Incremental cost per additional voice:
- 0->1: 258.78 us/voice
- 1->2: 481.66 us/voice
- 2->4: 250.27 us/voice
- 4->6: 247.90 us/voice
- 6->8: 246.07 us/voice

BELL: measured zero-voice fixed cost 162.90 us; least-squares intercept 208.13 us, slope 317.86 us/voice.

Incremental cost per additional voice:
- 0->1: 291.08 us/voice
- 1->2: 493.71 us/voice
- 2->4: 292.64 us/voice
- 4->6: 298.50 us/voice
- 6->8: 283.19 us/voice

Profile fixture 0: total 392.61 us; modal 0.0%, voice/allocator residual 31.3%, body 26.0%, limiter/PCM 24.2%, other 18.6%.

Profile fixture 1: total 683.20 us; modal 27.0%, voice/allocator residual 32.6%, body 14.8%, limiter/PCM 14.2%, other 11.3%.
Modal diagnostic cost: 184.69 us/voice/block, 23.09 us/voice/mode/block. Includes probe overhead; use unprofiled differences for optimization decisions.

Profile fixture 2: total 1160.23 us; modal 31.1%, voice/allocator residual 26.5%, body 8.8%, limiter/PCM 24.0%, other 9.6%.
Modal diagnostic cost: 180.33 us/voice/block, 22.54 us/voice/mode/block. Includes probe overhead; use unprofiled differences for optimization decisions.

Profile fixture 3: total 1653.44 us; modal 43.2%, voice/allocator residual 28.0%, body 6.1%, limiter/PCM 16.1%, other 6.6%.
Modal diagnostic cost: 178.73 us/voice/block, 22.34 us/voice/mode/block. Includes probe overhead; use unprofiled differences for optimization decisions.

Profile fixture 4: total 2233.11 us; modal 48.9%, voice/allocator residual 28.6%, body 4.6%, limiter/PCM 12.6%, other 5.3%.
Modal diagnostic cost: 182.05 us/voice/block, 22.76 us/voice/mode/block. Includes probe overhead; use unprofiled differences for optimization decisions.

Profile fixture 5: total 2720.89 us; modal 52.7%, voice/allocator residual 29.1%, body 3.7%, limiter/PCM 10.3%, other 4.1%.
Modal diagnostic cost: 179.41 us/voice/block, 22.43 us/voice/mode/block. Includes probe overhead; use unprofiled differences for optimization decisions.

Profile fixture 6: total 1706.85 us; modal 42.8%, voice/allocator residual 28.0%, body 6.0%, limiter/PCM 16.3%, other 6.9%.
Modal diagnostic cost: 182.69 us/voice/block, 22.84 us/voice/mode/block. Includes probe overhead; use unprofiled differences for optimization decisions.

Profile fixture 7: total 687.50 us; modal 26.9%, voice/allocator residual 32.6%, body 14.8%, limiter/PCM 14.5%, other 11.2%.
Modal diagnostic cost: 185.20 us/voice/block, 23.15 us/voice/mode/block. Includes probe overhead; use unprofiled differences for optimization decisions.

Profile fixture 8: total 185.57 us; modal 0.0%, voice/allocator residual 1.7%, body 6.4%, limiter/PCM 52.9%, other 39.0%.

Profile fixture 9: total 482.12 us; modal 45.3%, voice/allocator residual 18.2%, body 2.4%, limiter/PCM 19.5%, other 14.7%.
Modal diagnostic cost: 218.19 us/voice/block, 21.82 us/voice/mode/block. Includes probe overhead; use unprofiled differences for optimization decisions.

Profile fixture 10: total 1005.77 us; modal 44.7%, voice/allocator residual 17.5%, body 1.2%, limiter/PCM 27.0%, other 9.5%.
Modal diagnostic cost: 224.92 us/voice/block, 22.49 us/voice/mode/block. Includes probe overhead; use unprofiled differences for optimization decisions.

Profile fixture 11: total 1620.24 us; modal 55.3%, voice/allocator residual 21.2%, body 0.7%, limiter/PCM 16.9%, other 5.9%.
Modal diagnostic cost: 223.95 us/voice/block, 22.40 us/voice/mode/block. Includes probe overhead; use unprofiled differences for optimization decisions.

Profile fixture 12: total 2260.60 us; modal 59.5%, voice/allocator residual 22.7%, body 0.5%, limiter/PCM 12.4%, other 4.9%.
Modal diagnostic cost: 224.10 us/voice/block, 22.41 us/voice/mode/block. Includes probe overhead; use unprofiled differences for optimization decisions.

Profile fixture 13: total 2821.48 us; modal 62.6%, voice/allocator residual 23.7%, body 0.4%, limiter/PCM 9.8%, other 3.5%.
Modal diagnostic cost: 220.72 us/voice/block, 22.07 us/voice/mode/block. Includes probe overhead; use unprofiled differences for optimization decisions.

Profile fixture 14: total 1696.14 us; modal 54.9%, voice/allocator residual 21.2%, body 0.7%, limiter/PCM 16.9%, other 6.2%.
Modal diagnostic cost: 232.92 us/voice/block, 23.29 us/voice/mode/block. Includes probe overhead; use unprofiled differences for optimization decisions.

Profile fixture 15: total 493.19 us; modal 45.2%, voice/allocator residual 18.4%, body 2.4%, limiter/PCM 19.6%, other 14.5%.
Modal diagnostic cost: 222.83 us/voice/block, 22.28 us/voice/mode/block. Includes probe overhead; use unprofiled differences for optimization decisions.

Profile fixture 16: total 2585.72 us; modal 55.4%, voice/allocator residual 29.3%, body 0.5%, limiter/PCM 10.7%, other 4.2%.
Modal diagnostic cost: 179.07 us/voice/block, 22.38 us/voice/mode/block. Includes probe overhead; use unprofiled differences for optimization decisions.

Profile fixture 17: total 2718.63 us; modal 59.8%, voice/allocator residual 25.1%, body 0.5%, limiter/PCM 10.5%, other 4.0%.
Modal diagnostic cost: 203.28 us/voice/block, 22.59 us/voice/mode/block. Includes probe overhead; use unprofiled differences for optimization decisions.

Profile fixture 18: total 2527.66 us; modal 57.6%, voice/allocator residual 27.0%, body 0.5%, limiter/PCM 11.0%, other 3.9%.
Modal diagnostic cost: 182.00 us/voice/block, 22.75 us/voice/mode/block. Includes probe overhead; use unprofiled differences for optimization decisions.
