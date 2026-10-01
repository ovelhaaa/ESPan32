# M7.6 historical Mbira listening experiments

MBIRA V1 FROZEN = Candidate B + Buzz ON, accepted explicitly by the user. This A/C and buzz-off pack is retained as historical evidence. Aggregate FNV64 `f5ee8755af2fa270` and seven exact PCM fixtures are mandatory freeze assertions.

Compare these five RMS-matched A/B/C groups:

| Fixture | Warm A | Balanced B | Raw C |
|---|---|---|---|
| D3 v70 | [A](mbira_D3_v70_A.wav) | [B](mbira_D3_v70_B.wav) | [C](mbira_D3_v70_C.wav) |
| D4 v70 | [A](mbira_D4_v70_A.wav) | [B](mbira_D4_v70_B.wav) | [C](mbira_D4_v70_C.wav) |
| D4 v110 | [A](mbira_D4_v110_A.wav) | [B](mbira_D4_v110_B.wav) | [C](mbira_D4_v110_C.wav) |
| D minor chord | [A](mbira_chord_A.wav) | [B](mbira_chord_B.wav) | [C](mbira_chord_C.wav) |
| Interlocking groove | [A](mbira_groove_A.wav) | [B](mbira_groove_B.wav) | [C](mbira_groove_C.wav) |

Compare B with its shared mechanical contact disabled/enabled:

| Fixture | Buzz off | Buzz on |
|---|---|---|
| D4 v70 | [Off](mbira_D4_v70_buzz_off.wav) | [On](mbira_D4_v70_buzz_on.wav) |
| D4 v110 | [Off](mbira_D4_v110_buzz_off.wav) | [On](mbira_D4_v110_buzz_on.wav) |
| Groove | [Off](mbira_groove_buzz_off.wav) | [On](mbira_groove_buzz_on.wav) |

Every file is six seconds, 48 kHz/16 bit stereo with duplicated mono. Each group is attenuated to its lowest raw RMS; no boost, normalization limiter or effect is used. [Manifest](manifest.json) verifies PCM levels and SHA256 for all 21 listening files. Two unnormalized Kalimba references are retained for structural analysis; their louder raw gain makes direct listening comparisons unreliable without level matching.

The groove alternates two six-pulse hands at 150 ms offsets, using D3/A3/C4/D4 and F4/A4/D5. The chord is D3/A3/D4/F4. Tests additionally render D3/A3/D4/A4/D5 at v30/70/110/127, cluster8 at v127, 100/250 ms restrikes, rolls at 250/125/50 ms and intentional stealing without adding these to the listening pack.

Final decision: Candidate B + Buzz ON. No further Mbira voicing exploration. [Freeze report](../../m761_mbira_freeze.md).

Regenerate with `build-m76-host/test_mbira.exe docs/listening/m76`, then `python tests/summarize_m76.py`. This focused listening pack is versioned explicitly despite the repository's general generated-WAV ignore rule.
