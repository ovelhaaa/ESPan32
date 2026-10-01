# M7.7 UDU listening candidates

UDU is technically qualified, not frozen. The user selected character B after
listening. B / PARTIAL remains the runtime baseline. Velocity-driven transitions
between CLOSED / PARTIAL / OPEN are deferred to the next implementation round.
MBIRA V1 remains B + Buzz ON.

All comparisons are six seconds, 48 kHz / 16-bit stereo (duplicated mono),
attenuated to the lowest RMS in their group. No candidate is boosted and no
limiter participates in the sound. [Verified PCM levels and file digests](manifest.json).

| Character | A: Deep / Hollow | B: Balanced / Acoustic | C: Dry / Percussive |
|---|---:|---:|---:|
| Main cavity at MIDI 60, partial opening | 80 Hz | 105 Hz | 140 Hz |
| Main air T60 | .65 s | .42 s | .22 s |
| Cavity drive | 1.20 | 1.00 | .85 |
| Shell excitation multiplier | .38 | 1.00 | 1.50 |
| Hand pulse slow time constant | 4.2 ms | 2.8 ms | 1.3 ms |

Compare the same row across A/B/C, then listen to the groove:

| Fixture | A | B | C |
|---|---|---|---|
| Low pot, MIDI 36 v70 | [A](udu_low_v70_A.wav) | [B](udu_low_v70_B.wav) | [C](udu_low_v70_C.wav) |
| Mid pot, MIDI 60 v70 | [A](udu_mid_v70_A.wav) | [B](udu_mid_v70_B.wav) | [C](udu_mid_v70_C.wav) |
| Mid pot, MIDI 60 v110 | [A](udu_mid_v110_A.wav) | [B](udu_mid_v110_B.wav) | [C](udu_mid_v110_C.wav) |
| High pot, MIDI 84 v70 | [A](udu_high_v70_A.wav) | [B](udu_high_v70_B.wav) | [C](udu_high_v70_C.wav) |
| Two-bar hand-percussion groove | [A](udu_groove_A.wav) | [B](udu_groove_B.wav) | [C](udu_groove_C.wav) |

The groove uses low palm hits (36), body accents (60), high finger taps (84),
and soft offbeat ghosts at 120 BPM. Velocity and register change air/body balance.

Provisional B opening-state groove comparisons are RMS matched separately:
[OPEN](udu_B_open.wav), [PARTIAL](udu_B_partial.wav), [CLOSED](udu_B_closed.wav).
OPEN increases cavity frequency ×1.18 and shortens T60 ×.72; CLOSED lowers
frequency ×.72 and lengthens T60 ×1.30. Shell coefficients stay unchanged.
These are reduced acoustic hypotheses, not measured covered-hole responses.
There is no user-facing opening control.

The runtime compresses four MIDI octaves into one cavity octave, clamps outside
36–84, and scales shell frequency by virtual size^.82. Three unnormalized
[fully pitched low](udu_mapping_pitched_36.wav),
[mid](udu_mapping_pitched_60.wav), [high](udu_mapping_pitched_84.wav)
probes investigate conventional keyboard transposition. Compare their identity
with the compressed B low/mid/high set; loudness differs for these mapping probes.
The provisional bounded family avoids 26–420 Hz extreme B air modes.

Character B is selected. The remaining decisions are whether the hollow
ceramic identity is convincing in the groove, the opening response and the final
size mapping. Human listening must decide; numeric qualification does not freeze UDU.

Regenerate: `build-m76-host/test_udu.exe docs/listening/m77`, then
`python tests/summarize_m77.py`. Raw production metrics are in [CSV](host_metrics.csv).
