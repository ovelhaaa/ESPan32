# Bell V1 M6.4 listening pack

Generated deterministically by `tests/test_dsp.cpp` (`testBellM64ListeningPack`).
A = current production baseline (`kPresetBell`); B = tierce 0.58; C = hum 0.24 + tierce 0.58 + nominal 0.90. All renders are 48 kHz stereo 16-bit PCM.

Listening is the primary gate. The numeric columns are safety guards and must not be used as a musical score. See `docs/bell_v1_listening_pack.md`.

## 1. Register sweep (baseline A, velocity 90, 4 s)

| File | MIDI | Hz | peak | RMS | clamp | modalSat | finite |
|---|---:|---:|---:|---:|---:|---:|---|
| bell_m64_note_36_v90.wav | 36 | 65.406387 | 0.503015 | 0.072143 | 0 | 0 | YES |
| bell_m64_note_48_v90.wav | 48 | 130.812775 | 0.509384 | 0.073808 | 0 | 0 | YES |
| bell_m64_note_55_v90.wav | 55 | 195.997726 | 0.522219 | 0.072778 | 0 | 0 | YES |
| bell_m64_note_60_v90.wav | 60 | 261.625549 | 0.530072 | 0.069712 | 0 | 0 | YES |
| bell_m64_note_67_v90.wav | 67 | 391.995422 | 0.556344 | 0.069724 | 0 | 0 | YES |
| bell_m64_note_72_v90.wav | 72 | 523.251099 | 0.545545 | 0.066559 | 0 | 0 | YES |
| bell_m64_note_84_v90.wav | 84 | 1046.502197 | 0.546968 | 0.070223 | 0 | 0 | YES |

## 2. Velocity matrix (baseline A, 3 s)

| File | MIDI | Velocity | peak | RMS | attack RMS | tail RMS | clamp | modalSat | FNV-1a64 |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---|
| bell_m64_single_48_v30.wav | 48 | 30 | 0.168567 | 0.032533 | 0.084966 | 0.028101 | 0 | 0 | 0x9adb55fb052939d9 |
| bell_m64_single_48_v60.wav | 48 | 60 | 0.312035 | 0.055917 | 0.147829 | 0.047536 | 0 | 0 | 0xce4ac99f743c1d8d |
| bell_m64_single_48_v90.wav | 48 | 90 | 0.509384 | 0.085211 | 0.228365 | 0.071175 | 0 | 0 | 0xb681e2179699144d |
| bell_m64_single_48_v110.wav | 48 | 110 | 0.668383 | 0.107688 | 0.291022 | 0.088959 | 0 | 0 | 0xdff4fd7a4e175461 |
| bell_m64_single_48_v127.wav | 48 | 127 | 0.745269 | 0.118542 | 0.321314 | 0.097522 | 0 | 0 | 0xafad8ab573b261e5 |
| bell_m64_single_60_v30.wav | 60 | 30 | 0.170236 | 0.028931 | 0.081211 | 0.022793 | 0 | 0 | 0x65364271709e7215 |
| bell_m64_single_60_v60.wav | 60 | 60 | 0.320800 | 0.051245 | 0.145660 | 0.039564 | 0 | 0 | 0x7ce608098a6e0a91 |
| bell_m64_single_60_v90.wav | 60 | 90 | 0.530072 | 0.080492 | 0.231492 | 0.060906 | 0 | 0 | 0x1cc7ce1d320cbaf9 |
| bell_m64_single_60_v110.wav | 60 | 110 | 0.699792 | 0.102612 | 0.297315 | 0.076641 | 0 | 0 | 0xb22af1ec83f011f5 |
| bell_m64_single_60_v127.wav | 60 | 127 | 0.780717 | 0.112538 | 0.327265 | 0.083539 | 0 | 0 | 0x5dad95917325c375 |
| bell_m64_single_72_v30.wav | 72 | 30 | 0.176375 | 0.028311 | 0.084210 | 0.020587 | 0 | 0 | 0x7d8645d1bc5a2fd9 |
| bell_m64_single_72_v60.wav | 72 | 60 | 0.332851 | 0.049628 | 0.149764 | 0.035307 | 0 | 0 | 0xdd5e6dba5a5c12a1 |
| bell_m64_single_72_v90.wav | 72 | 90 | 0.545545 | 0.076854 | 0.235114 | 0.053419 | 0 | 0 | 0xebc73414e273cfe9 |
| bell_m64_single_72_v110.wav | 72 | 110 | 0.713329 | 0.097571 | 0.300863 | 0.066827 | 0 | 0 | 0x154a4927b20cce7d |
| bell_m64_single_72_v127.wav | 72 | 127 | 0.791242 | 0.107163 | 0.331423 | 0.072988 | 0 | 0 | 0x9f90d22b6526d48d |

## 3. Intervals (baseline A, velocity 90, 4 s)

| File | Notes | peak | RMS | clamp | modalSat | finite |
|---|---|---:|---:|---:|---:|---|
| bell_m64_interval_D3_A3.wav | 50+57 | 0.944061 | 0.089588 | 0 | 0 | YES |
| bell_m64_interval_D4_A4.wav | 62+69 | 0.944061 | 0.082331 | 0 | 0 | YES |
| bell_m64_interval_C4_G4.wav | 60+67 | 0.944061 | 0.083799 | 0 | 0 | YES |

## 4. Four-note chord and eight-note polyphony (baseline A, 4 s)

| File | Voices | peak | RMS | pre-limiter peak | max GR dB | max voices | clamp | modalSat |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| bell_m64_chord4_v60.wav | 4 | 0.944061 | 0.080470 | 1.092141 | -1.265578 | 4 | 0 | 0 |
| bell_m64_chord4_v90.wav | 4 | 0.944061 | 0.114920 | 1.819160 | -5.697418 | 4 | 0 | 0 |
| bell_m64_chord4_v127.wav | 4 | 0.944061 | 0.138711 | 2.680860 | -9.065484 | 4 | 0 | 0 |
| bell_m64_poly8_v90.wav | 8 | 0.944061 | 0.127560 | 3.549972 | -11.504499 | 8 | 0 | 0 |
| bell_m64_poly8_v110.wav | 8 | 0.944061 | 0.145141 | 4.689055 | -13.921708 | 8 | 0 | 0 |

## 5. Restrikes and roll (baseline A, 4 s)

| File | Gesture | peak | RMS | max sample delta | clamp | modalSat | finite |
|---|---|---:|---:|---:|---:|---:|---|
| bell_m64_restrike_soft_hard.wav | soft_hard | 0.753491 | 0.091406 | 0.066173 | 0 | 0 | YES |
| bell_m64_restrike_hard_soft.wav | hard_soft | 0.703057 | 0.091225 | 0.061798 | 0 | 0 | YES |
| bell_m64_restrike_double_100ms.wav | double_100ms | 0.684704 | 0.096064 | 0.043859 | 0 | 0 | YES |
| bell_m64_restrike_double_250ms.wav | double_250ms | 0.611821 | 0.084747 | 0.043859 | 0 | 0 | YES |
| bell_m64_roll.wav | roll 100 ms | 0.944061 | 0.194392 | 0.072341 | 0 | 0 | YES |

## 6. A/B/C level-matched timbre comparison (4 s)

A = baseline; B = tierce 0.58; C = hum 0.24 / tierce 0.58 / nominal 0.90. Each candidate is written raw and RMS-matched to A so loudness cannot bias the choice.

| Fixture | Candidate | raw RMS | matched RMS | peak | max GR dB | clamp | modalSat |
|---|---|---:|---:|---:|---:|---:|---|
| D4_v70 | A | 0.050750 | 0.050750 | 0.385447 | 0.000000 | 0 | 0 |
| D4_v70 | B | 0.051266 | 0.050750 | 0.385616 | 0.000000 | 0 | 0 |
| D4_v70 | C | 0.051040 | 0.050750 | 0.386469 | 0.000000 | 0 | 0 |
| D4_v110 | A | 0.087581 | 0.087581 | 0.703057 | 0.000000 | 0 | 0 |
| D4_v110 | B | 0.088221 | 0.087581 | 0.702541 | 0.000000 | 0 | 0 |
| D4_v110 | C | 0.088235 | 0.087581 | 0.705661 | 0.000000 | 0 | 0 |
| chord4 | A | 0.114920 | 0.114920 | 0.944061 | -5.697418 | 0 | 0 |
| chord4 | B | 0.116324 | 0.114920 | 0.944061 | -5.691909 | 0 | 0 |
| chord4 | C | 0.116221 | 0.114920 | 0.944061 | -5.708801 | 0 | 0 |
| roll | A | 0.143819 | 0.143819 | 0.944061 | -1.534719 | 0 | 0 |
| roll | B | 0.145306 | 0.143819 | 0.944061 | -1.593571 | 0 | 0 |
| roll | C | 0.144262 | 0.143819 | 0.944061 | -1.542673 | 0 | 0 |

## 7. Listening checklist (independent dimensions)

- pitch clarity / stable tonal centre
- metallic identity
- low-frequency hum balance
- prime definition
- tierce / quint audibility
- high-mode harshness (fizz)
- doublet beating (audible but not distracting)
- attack realism
- decay realism
- velocity progression (soft expressive, hard controlled)
- register consistency
- polyphonic density
- restrike behaviour
- tail smoothness
- limiter interaction
- audible realtime defects (click / dropout / instability): NONE expected

Decision rule: if no candidate clearly improves the baseline, keep baseline A. Bell parameters are frozen only after a human listening decision.
