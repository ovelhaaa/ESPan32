# PAN M5C.1 host metrics

Host timing is not ESP32-S3 realtime timing; hardware telemetry is authoritative for CPU, deadline misses, write timeouts, and TX errors.

## A — D3 component isolation

| Candidate | Dry baseline RMS | Candidate RMS | Difference RMS | Relative dB | Body peak/RMS | Bus peak/RMS | Max/avg GR dB | GR >0.1 / >1 dB | Clamp / safety |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| dry | 0.107099 | 0.107099 | 0.000000 | -220.595688 | 0.000000 / 0.000000 | 0.000000 / 0.000000 | 0.000000 / 0.000000 | 0 / 0 | 0 / 0 |
| body only contribution | 0.107099 | 0.108797 | 0.032672 | -10.312040 | 0.304040 / 0.038434 | 0.000000 / 0.000000 | 0.000000 / 0.000000 | 0 / 0 | 0 / 0 |
| sympathetic only contribution | 0.107099 | 0.107172 | 0.004826 | -26.923330 | 0.000000 / 0.000000 | 0.001708 / 0.000250 | 0.000000 / 0.000000 | 0 / 0 | 0 / 0 |
| full | 0.107099 | 0.108837 | 0.033117 | -10.194639 | 0.304448 / 0.038495 | 0.001708 / 0.000250 | 0.000000 / 0.000000 | 0 / 0 | 0 / 0 |

## B — musical fixtures

| Fixture | Dry baseline RMS | Full RMS | Difference RMS | Relative dB | Dry/full max GR | Dry/full avg GR | Clamp / safety |
|---|---:|---:|---:|---:|---:|---:|---:|
| interval | 0.112186 | 0.100906 | 0.059054 | -5.573802 | 0.000000 / 0.000000 | 0.000000 / 0.000000 | 0 / 0 |
| chord | 0.113726 | 0.103529 | 0.045804 | -7.899158 | -5.002847 / -5.342328 | -0.178992 / -0.188134 | 0 / 0 |
| roll | 0.391216 | 0.374066 | 0.069085 | -15.060633 | -7.010152 / -7.766256 | -1.215925 / -1.390732 | 0 / 0 |
| cluster8 | 0.126274 | 0.117091 | 0.054876 | -7.238575 | -12.923484 / -13.218987 | -0.377171 / -0.453632 | 0 / 0 |

### A.1 — D3 attack and tail contribution

| Velocity | Path | 0-5 ms RMS | 0-20 ms RMS | 0-100 ms RMS | 500-1500 ms RMS | Attack delta RMS / % | Tail delta RMS / % |
|---:|---|---:|---:|---:|---:|---:|---:|
| 70 | dry | 0.205252 | 0.241556 | 0.222401 | 0.019929 | 0.000000 / 0.000000 | 0.000000 / 0.000000 |
| 70 | body | 0.263281 | 0.237550 | 0.229701 | 0.019374 | -0.004006 / -1.658366 | -0.000556 / -2.788371 |
| 70 | full | 0.263434 | 0.237552 | 0.229541 | 0.019823 | -0.004004 / -1.657496 | -0.000107 / -0.534570 |
| 110 | dry | 0.329529 | 0.374951 | 0.344054 | 0.029629 | 0.000000 / 0.000000 | 0.000000 / 0.000000 |
| 110 | body | 0.423728 | 0.370051 | 0.354361 | 0.028815 | -0.004900 / -1.306903 | -0.000814 / -2.748667 |
| 110 | full | 0.423992 | 0.370091 | 0.354221 | 0.029422 | -0.004861 / -1.296355 | -0.000208 / -0.700636 |

### A.2 — body mode spectral validation (D3 body ON vs OFF)

| Mode Hz | Body-off magnitude | Body-on magnitude | bodyModeDeltaDb |
|---:|---:|---:|---:|
| 110.000000 | 0.000700 | 0.008652 | 21.839115 |
| 205.000000 | 0.000123 | 0.000970 | 17.959911 |
| 390.000000 | 0.000010 | 0.000047 | 13.581556 |
| 730.000000 | 0.000016 | 0.000039 | 7.639599 |
| 1280.000000 | 0.000021 | 0.000022 | 0.559284 |
| 1980.000000 | 0.000005 | 0.000004 | -0.981264 |

## C — register audit (body ON vs OFF, velocity 70)

| Note | RMS off/on | Attack RMS off/on | Tail RMS off/on | Body RMS | Pre-limiter peak off/on | Max/avg GR off/on | Nearest body mode | Distance % |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| D3 | 0.069727 / 0.070962 | 0.222401 / 0.229701 | 0.019929 / 0.019374 | 0.025108 | 0.441594 / 0.546942 | 0.000000 / 0.000000; 0.000000 / 0.000000 | 390.000000 | 12.947983 |
| A3 | 0.065199 / 0.043952 | 0.212232 / 0.160320 | 0.016857 / 0.007398 | 0.057423 | 0.415350 / 0.471940 | 0.000000 / 0.000000; 0.000000 / 0.000000 | 205.000000 | 7.317073 |
| D4 | 0.059212 / 0.059184 | 0.196374 / 0.197006 | 0.014058 / 0.013907 | 0.009966 | 0.390023 / 0.446502 | 0.000000 / 0.000000; 0.000000 / 0.000000 | 730.000000 | 19.543900 |
| A4 | 0.065534 / 0.056853 | 0.220280 / 0.191963 | 0.013460 / 0.011472 | 0.014817 | 0.435913 / 0.434822 | 0.000000 / 0.000000; 0.000000 / 0.000000 | 1280.000000 | 3.125000 |

## D — stability and sympathetic gain sweep

| Case | Bus RMS | Bus peak | Final body energy | Safety | Hard clamp |
|---|---:|---:|---:|---:|---:|
| 10s fixture | 0.000112 | 0.001708 | 0.000000 | 0 | 0 |
| 10s fixture | 0.000298 | 0.009766 | 0.000000 | 0 | 0 |
| 10s fixture | 0.000577 | 0.004843 | 0.000000 | 0 | 0 |
| 15s decay | 0.000159 | 0.003940 | 0.000000 | 0 | 0 |
| sympathetic 0.500000x | 0.000149 | 0.004881 | 0.000000 | 0 | 0 |
| sympathetic 1.000000x | 0.000298 | 0.009766 | 0.000000 | 0 | 0 |
| sympathetic 2.000000x | 0.000593 | 0.019564 | 0.000000 | 0 | 0 |
| sympathetic 4.000000x | 0.001148 | 0.030000 | 0.000000 | 25 | 0 |

## E — gain staging

| Fixture | Dry max GR | Full max GR | Dry avg GR | Full avg GR | Dry >0.1/>1 dB | Full >0.1/>1 dB | Hard clamp | Safety |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| D3 | 0.000000 | 0.000000 | 0.000000 | 0.000000 | 0 / 0 | 0 / 0 | 0 | 0 |
| interval | 0.000000 | 0.000000 | 0.000000 | 0.000000 | 0 / 0 | 0 / 0 | 0 | 0 |
| chord | -5.002847 | -5.342328 | -0.178992 | -0.188134 | 14120 / 5473 | 14266 / 5619 | 0 | 0 |
| roll | -7.010152 | -7.766256 | -1.215925 | -1.390732 | 47373 / 35128 | 48791 / 36559 | 0 | 0 |
| cluster8 | -12.923484 | -13.218987 | -0.377171 | -0.453632 | 16564 / 7919 | 18120 / 9474 | 0 | 0 |

## Hardware CPU capture (required before freeze)

| Scenario | M5B-like avg/max block us | M5C avg/max block us | CPU delta | Deadline misses | Write timeouts | TX errors |
|---|---:|---:|---:|---:|---:|---:|
| single | hardware required | hardware required | hardware required | hardware required | hardware required | hardware required |
| 4-note chord | hardware required | hardware required | hardware required | hardware required | hardware required | hardware required |
| 8-note cluster | hardware required | hardware required | hardware required | hardware required | hardware required | hardware required |
| roll | hardware required | hardware required | hardware required | hardware required | hardware required | hardware required |

### B.1 — M5B velocity expression with M5C enabled

| Velocity | modalBrightnessRatio | highModalRatio |
|---:|---:|---:|
| 30 | 0.003709 | 0.000009 |
| 70 | 0.014041 | 0.000123 |
| 110 | 0.027588 | 0.000374 |
