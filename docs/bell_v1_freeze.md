# Bell V1 freeze record (M6.4)

## Freeze state

```text
Bell V1 parameters : FROZEN (baseline A retained, no change)
Human listening    : COMPLETE
Bell V1            : FROZEN

Listening:
A/B/C perceptually equivalent to user

Tie-break:
performance

Selected:
A

Reason:
Candidate A fires the expensive modal safety-saturation branch least often (A: 960 vs C: 971 vs B: 984 in sustained v127 cluster8) with no timing difference. Retained frozen baseline A.
```

The M6.4 baseline was not changed. Human listening found no significant
perceived difference between A, B and C, so the decision fell to the tie-break
rule: retained baseline A. The parameter table below is the frozen reference.
No Bell golden was regenerated because no parameter changed.

Full verified configuration: [bell_v1_baseline.md](bell_v1_baseline.md).
Listening material: [bell_v1_listening_pack.md](bell_v1_listening_pack.md).

## Final Bell parameter table

Modal topology (`main/dsp/modal_preset.h`):

| # | ratio | gain | T60 s | role | reason |
|---:|---:|---:|---:|---|---|
| 0 | 0.5000 | 0.28 | 5.5 | hum | sub-octave body; longest ring sets the metallic weight |
| 1 | 1.0000 | 1.00 | 5.0 | prime | pitch identity and tonal centre |
| 2 | 1.0020 | 0.20 | 4.6 | prime doublet | slow beating shimmer (~0.29-1.17 Hz by register) |
| 3 | 1.2000 | 0.65 | 3.8 | tierce | minor-third colour that defines bell character |
| 4 | 1.5000 | 0.22 | 2.6 | quint | fifth reinforcement |
| 5 | 2.0000 | 0.85 | 4.2 | nominal | strong octave strike tone |
| 6 | 2.0015 | 0.16 | 3.9 | nominal doublet | upper beating texture |
| 7 | 3.0000 | 0.50 | 2.8 | superquint | compound-fifth brightness |
| 8 | 4.0000 | 0.32 | 2.2 | octave nominal | upper partial |
| 9 | 5.2000 | 0.15 | 1.4 | upper | metallic top; fastest decay |

Voicing / exciter / resonator / output: see
[bell_v1_baseline.md](bell_v1_baseline.md) sections 2-8. No field was changed
in M6.4.

## Listening observations

| Session | Result |
|---|---|
| M6.2.1 host A/B/C | A richest/most inharmonic; B a clearer A; C strongest tonal centre. C was a *recommendation to listen*, not a promotion. |
| M6.4 hardware objective | PASS — 210 s PAN+BELL soak, 82,686 callbacks, max callback 3,079 us, max streak 1, I2S 0/0/0, MIDI drops 0, BLE reconnects 0, heap delta 0 (`docs/hardware/m64_smoke_summary.md`). |
| M6.4 human listening | **DONE — no significant perceived difference between A, B and C** across D4 v70, D4 v110, chord4 and roll (matched comparisons), and the baseline register/velocity/polyphony/restrike fixtures. No audible click or dropout on the board. |

Per-dimension scores were assessed independently (pitch, metal character, hum,
prime, tierce/quint, high-mode fizz, doublets, attack, decay, velocity,
register, polyphony, restrike, tail, limiter) as listed in the pack; all read
as musically equivalent for this instrument. Since no candidate clearly
improved the baseline, the tie-break selected the retained baseline A.

## A / B / C performance tie-break

Because the three candidates were perceptually equivalent, the decision came
down to performance. The candidates differ only in modal gains, which can
change how often the per-mode `|y| > 2.0` `tanh` safety branch fires. A host
probe rendered a sustained 8-voice Bell cluster at velocity 127 (restruck every
100 ms, 20 s) for each candidate and counted deterministic modal
safety-saturation events, with render time as a secondary (noisy) measure:

| Candidate | modal saturation events | us/block (host, min of runs, indicative) |
|---|---:|---:|
| A (baseline) | 960 | ~16.6 - 17.1 |
| B | 984 | ~16.6 - 18.1 |
| C | 971 | ~16.9 - 17.3 |

- Saturation counts are deterministic: **A < C < B**. A fires the expensive
  branch least often.
- Render-time differences are within run-to-run noise (no consistent ordering).
- Conclusion: no meaningful performance difference; A is (marginally) cheapest
  and is therefore retained.

This host proxy is indicative only; the authoritative real-time behavior is the
frozen hardware evidence (M6.3.9.2 15-min soak plus the M6.4 hardware smoke).

## Golden / reference policy

- PAN has twelve committed FNV golden fixtures plus PAN -> BELL -> PAN bit
  identity; those remain exact (`pan_m6_regression.md`).
- Bell currently has **no committed golden assertions**. M6.4 did not add any,
  because no Bell parameter changed and the spec's golden policy says not to
  regenerate unnecessarily.
- The host reference hashes below are recorded for future comparison only. They
  are toolchain-dependent (produced here with MinGW g++ 14.2.0, x86_64) and must
  not be asserted across compilers; the committed PAN goldens have the same
  property and pass under the CI toolchain.

Reference FNV-1a64 of baseline Bell single notes (3 s, 144000 frames):

| Fixture | FNV-1a64 |
|---|---|
| MIDI 48 v30 | `0x9adb55fb052939d9` |
| MIDI 48 v60 | `0xce4ac99f743c1d8d` |
| MIDI 48 v90 | `0xb681e2179699144d` |
| MIDI 48 v110 | `0xdff4fd7a4e175461` |
| MIDI 48 v127 | `0xafad8ab573b261e5` |
| MIDI 60 v30 | `0x65364271709e7215` |
| MIDI 60 v60 | `0x7ce608098a6e0a91` |
| MIDI 60 v90 | `0x1cc7ce1d320cbaf9` |
| MIDI 60 v110 | `0xb22af1ec83f011f5` |
| MIDI 60 v127 | `0x5dad95917325c375` |
| MIDI 72 v30 | `0x7d8645d1bc5a2fd9` |
| MIDI 72 v60 | `0xdd5e6dba5a5c12a1` |
| MIDI 72 v90 | `0xebc73414e273cfe9` |
| MIDI 72 v110 | `0x154a4927b20cce7d` |
| MIDI 72 v127 | `0x9f90d22b6526d48d` |

If a listening decision changes a parameter, the old hashes above must be
replaced deliberately, with the musical reason recorded — never silently as a
"test update".

## Performance qualification reference

Bell runs under the frozen [performance contract](performance_contract_v1.md):
steady and attack-tail paths meet the nominal 2.667 ms period; extreme
high-energy 8-voice Bell saturation produced bounded accumulated debt (~7.5 ms
worst) with zero transport starvation, I2S errors, MIDI loss, BLE loss, crash
or heap drift over a 15.47 min hardware soak. Bell's hardware qualification in
`hardware_m63_metrics.md` is superseded by that contract: the earlier cluster8
"FAIL" was a timing-gate artifact, not a transport failure.

## Known limitations

- 10 modes/voice makes Bell the most expensive instrument; extreme high-energy
  polyphony can exceed one block period (absorbed by buffering, see contract).
- The internal `tanh` safety saturation is sound-defining and is intentionally
  not approximated; it is only reconsidered if listening demonstrates audible
  harm.
- Bell disables the shared PAN body and sympathetic paths.

## Freeze criteria

Bell V1 may be marked `FROZEN` only when:

```text
pitch identity stable
velocity progression musical
soft notes expressive, hard notes controlled
doublets audible but not distracting
upper modes metallic without harsh fizz
decay coherent
restrikes natural
polyphonic playing usable
no audible realtime failures
```

and the human listening decision (A retained, or B/C promoted with exact delta)
is recorded here.

### Decision (M6.4)

```text
Bell V1 = FROZEN (baseline A, no parameter change)
```

The listening session reported pitch identity stable, velocity progression
musical, soft notes expressive and hard notes controlled, doublets audible but
not distracting, upper modes metallic without harsh fizz, coherent decay,
natural restrikes, usable polyphony, and no audible realtime failures — with no
significant difference between A, B and C. With perceptually equivalent
candidates, the performance tie-break retained A. No Bell golden was
regenerated because no parameter changed.
