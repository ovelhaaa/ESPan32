# M7.5.2 — selected C bar, tube modulation

Historical listening archive. The final selection is **C + M1**, frozen in [M7.5.3](../../m753_vibraphone_v1_freeze.md). M2/M3 were not selected and are absent from production.

Exactly 16 stereo PCM16 WAVs, 48 kHz, eight seconds each. Identical events, deterministic seeds and C bar in every comparison. Each four-file group is RMS matched after PCM conversion; normalization only attenuates. Unmatched safety levels and limiter diagnostics are in [host metrics](../../qualification/m752/host_metrics.md); checksums, RMS and peaks are in [manifest](../../qualification/m752/listening_manifest.json).

| State | Tube behavior |
|---|---|
| M0 | Motor OFF, exact selected C dry bar |
| M1 | Exact M7.5.1 C mode-0 coupling; no tube memory/phase |
| M2 | Note-tracking allpass, 60° lag at mode 0; quadratic opening and phase blend |
| M3 | Same topology, 110° lag at mode 0; same coupling and opening curve |

All ON states use 4.5 Hz, depth .32 and tube coupling .75. The production bar is C; M2 is the provisional tube configuration, not an accepted perceptual winner. A/B/C bar exploration is closed.

| Fixture | Dry | Previous tube | Moderate phase | Stronger phase |
|---|---|---|---|---|
| D4 v70 | [M0](vibra_C_D4_v70_M0_matched.wav) | [M1](vibra_C_D4_v70_M1_matched.wav) | [M2](vibra_C_D4_v70_M2_matched.wav) | [M3](vibra_C_D4_v70_M3_matched.wav) |
| D4 v110 | [M0](vibra_C_D4_v110_M0_matched.wav) | [M1](vibra_C_D4_v110_M1_matched.wav) | [M2](vibra_C_D4_v110_M2_matched.wav) | [M3](vibra_C_D4_v110_M3_matched.wav) |
| Chord | [M0](vibra_C_chord_M0_matched.wav) | [M1](vibra_C_chord_M1_matched.wav) | [M2](vibra_C_chord_M2_matched.wav) | [M3](vibra_C_chord_M3_matched.wav) |
| Sparse phrase | [M0](vibra_C_phrase_M0_matched.wav) | [M1](vibra_C_phrase_M1_matched.wav) | [M2](vibra_C_phrase_M2_matched.wav) | [M3](vibra_C_phrase_M3_matched.wav) |

Compare M1/M2/M3 first: **does the resonance underneath the bar change shape and bloom with the fan, rather than the fundamental simply getting louder and quieter?** On chords, check for low-frequency buildup, excessive beating or diffuse phasing. No automatic scoring or winner is supplied.

Historical reproduction: check out commit `43f9196` and build/run its `test_dsp`, then `python tests/collect_m752.py BUILD docs/listening/m752 docs/qualification/m752/listening_manifest.json`. The suite also tests cluster8, roll, three phase relationships (30/60/110°) and linear/quadratic aperture curves without writing additional listening WAVs. Linear versus quadratic coupling remains a listening decision; quadratic is provisional.
