# Bell V1 baseline (frozen reference before M6.4 listening)

This document records the **current** Bell configuration exactly as compiled,
verified directly against source. It is the baseline against which any M6.4
listening candidate is compared. No Bell parameter is changed by M6.4 unless
listening demonstrates a specific problem.

Source of truth: `main/dsp/modal_preset.h`, `main/dsp/instrument_model.cpp`,
`main/dsp/instrument_model.h`, `main/dsp/modal_resonator.cpp`,
`main/dsp/modal_voice.cpp`, `main/dsp/exciter.cpp`, `main/dsp/synth_engine.cpp`,
`main/dsp/peak_limiter.h`.

Production DSP candidate 25, UI F0, 48 kHz, block 128, 8 voices.

---

## 1. Modal topology (`kPresetBell`)

`main/dsp/modal_preset.h:39-45`. MIDI pitch is the Bell prime reference: ratio
1.0 is the audible prime.

| # | ratio | gain | T60 s | role |
|---:|---:|---:|---:|---|
| 0 | 0.5000 | 0.28 | 5.5 | hum |
| 1 | 1.0000 | 1.00 | 5.0 | prime |
| 2 | 1.0020 | 0.20 | 4.6 | prime doublet |
| 3 | 1.2000 | 0.65 | 3.8 | tierce |
| 4 | 1.5000 | 0.22 | 2.6 | quint |
| 5 | 2.0000 | 0.85 | 4.2 | nominal (octave) |
| 6 | 2.0015 | 0.16 | 3.9 | nominal doublet |
| 7 | 3.0000 | 0.50 | 2.8 | superquint |
| 8 | 4.0000 | 0.32 | 2.2 | octave nominal |
| 9 | 5.2000 | 0.15 | 1.4 | upper |

- 10 modes/voice, all `detune = 0.0`.
- The doublets are literal ratio offsets (1.0020 and 2.0015), **not** the
  `detune` field. The PAN-style fixed-Hz doublet branch
  (`updatePitchAndDamping`, index 1 with `ratio == 1.0 && detune != 0`) is
  inactive for Bell because every Bell detune is 0.
- Bank normalization: `bankNorm = 1 / sqrt(sum(gain_i^2))`
  (`modal_resonator.cpp:103-109`). Bell `sum(gain^2) = 2.7123`,
  `bankNorm ~ 0.60708`.
- Per-mode excitation gain:
  `excitationGain = modeGain * sin(w) * bankNorm * resonatorMasterGain * coupling_i`.
  The `sin(w)` factor normalizes attack peak independently of frequency and T60.
- Effective decay:
  `T60_eff = max(0.005, def.t60 * dampingScale * t60RegisterScale)`,
  `r = exp(ln(0.001) / (T60_eff * fs))`, `a1 = 2 r cos(w)`, `a2 = -r^2`.
  `dampingScale = 1 - 0.95 * damping`.
- Nyquist rule (`modal_resonator.h:14-18`): a mode is active only while
  `10 Hz < f < 0.48*fs` (23,040 Hz at 48 kHz); between `0.40*fs` (19,200 Hz) and
  `0.48*fs` the gain is linearly faded. For every supported Bell note the
  5.2x upper mode stays below the fade region up to MIDI 84.

## 2. Register behavior (`kBellVoicing`, `instrument_model.cpp:8-12`)

| Control | Value |
|---|---:|
| `strikeHardnessMin` | 0.30 |
| `strikeHardnessMax` | 1.00 |
| `lowRegisterGain` / `highRegisterGain` | 1.0 / 1.0 |
| `lowRegisterBrightness` / `highRegisterBrightness` | 1.0 / 0.95 |
| `upperModeSoftVelocity` / `upperModeHardVelocity` | 0.18 / 0.94 |
| `t60LowRegisterScale` / `t60HighRegisterScale` | 1.10 / 0.85 |
| `splitBeatTargetHz` | 0.0 |
| `registerLowHz` / `registerHighHz` | 146.83 / 440.0 |
| `fixedHzSplit` | false |
| `silenceThreshold` | 1.0e-9 |

- Register position `reg = clamp((ln(f) - ln(146.83)) / (ln(440) - ln(146.83)), 0, 1)`.
  D3 (146.83 Hz) maps to 0, A4 (440 Hz) to 1; notes outside the range clamp.
- `t60RegisterScale = 1.10 - 0.25 * reg` (clamped to [0.5, 1.5]), so low notes
  ring ~29% longer and high notes ~15% shorter than nominal.
- `brightnessScale = 1.0 - 0.05 * reg`.
- Total modal gain is flat across the register (both register gains are 1.0).

## 3. Velocity mapping

- `velocity = data2 / 127` (`midi_mapping.h:16-18`, `synth_engine.cpp:126`);
  NoteOn with `data2 == 0` is treated as NoteOff.
- Hardness: `h = 0.30 + 0.70 * pow(velocity, 1.15)`
  (`modal_voice.cpp:189-192`).
- Upper-mode blend:
  `blend = clamp((velocity - 0.18) / (0.94 - 0.18), 0, 1)`
  (`modal_voice.cpp:194-195`).
- Mode coupling (before register gain):
  `coupling_i = soft_i + (hard_i - soft_i) * blend`
  (`modal_voice.cpp:198-204`).

## 4. Mode coupling tables

| # | soft | hard |
|---:|---:|---:|
| 0 | 0.55 | 0.45 |
| 1 | 1.00 | 1.00 |
| 2 | 0.12 | 0.25 |
| 3 | 0.28 | 0.70 |
| 4 | 0.06 | 0.28 |
| 5 | 0.50 | 0.90 |
| 6 | 0.06 | 0.20 |
| 7 | 0.12 | 0.68 |
| 8 | 0.05 | 0.45 |
| 9 | 0.02 | 0.26 |

Soft strikes favor the prime; hard strikes progressively reveal tierce,
nominal, superquint and octave modes.

## 5. Exciter configuration (`instrument_model.cpp:22`)

| Field | Bell | PAN (reference) |
|---|---:|---:|
| `gain` | 0.80 | 0.80 |
| `noiseAmount` | 0.70 | 1.0 |
| `brightnessMinHz` | 900 | 700 |
| `brightnessMaxHz` | 14000 | 12000 |
| `velocityKnee` | 0.85 | 0.85 |
| `velocityKneeSlope` | 0.35 | 0.35 |

Derived (`exciter.cpp:63-117`):

```text
energyVelocity = v <= 0.85 ? v : 0.85 + (v - 0.85) * 0.35
strikeGain     = 0.15 + 0.85 * energyVelocity^1.25
strikeAmplitude= 0.80 * strikeGain
impulseSamples = clamp(14 - 11*h, min 3)            // soft ~14, hard ~3
noiseSamples   = (0.003 + 0.005*(1-h)) * 48000      // 144..384 samples
cutoffHz       = clamp((900 + 13100*h^2) * brightnessScale, 585, 14000)
filterCoeff    = clamp(1 - exp(-2*pi*cutoffHz/fs), 0.01, 0.99)
noiseGain      = strikeAmplitude * (0.035 + 0.045*h) * 0.70
```

The noise burst is deterministic (xorshift PRNG seeded 123456789 at every
model init/selection).

## 6. Resonator / saturation configuration (`instrument_model.cpp:22`)

```text
ResonatorConfig { masterGain = 1.0, dampingDepth = 0.95, internalSafetySaturation = true }
```

Internal safety saturation (`modal_resonator.cpp:226-229`, `:328-331`):

```text
if (internalSafetySaturation && |y| > 2.0) {
    y = sign(y) * (2.0 + 0.5 * tanh(|y| - 2.0));
}
```

This is sound-defining modal compression that prevents runaway on rapid
strikes. M6.4 must not approximate, LUT-ify or remove it during ordinary Bell
tuning (see the performance contract, §5).

## 7. Body and sympathetic state (`instrument_model.cpp:24-26`)

```text
body        = { modes {}, modeCount 0, excitationGain 0, outputGain 0, lowpassHz 1000, enabled false }
sympathetic = { enabled false, inputGain 0, feedbackGain 0, lowpassHz 1500, maxBusLevel 0 }
bodyStrategy = StrikeBus
strikeBusGain = 0.0
```

Bell is a pure per-voice modal instrument: the shared PAN shell and sympathetic
bus are disabled. Body/sympathetic do not contribute to Bell output.

## 8. Output chain and limiter interaction (`synth_engine.cpp`)

```text
masterGain = 0.85
poly headroom (exact table, voice count 0..8):
    v<=1: 0 dB, v=2: -1.5 dB, v=4: -3 dB, v=8: -5 dB   (floor -5 dB)
    target = 10^(max(-1.5*log2(v), -5)/20)
    one-pole: attack ~3 ms, release ~50 ms
peak limiter:
    threshold = -3.0 dB
    ceiling   = -0.5 dB
    release   = 80 ms
    lookahead = 32 samples (bypasses below threshold with fixed delay)
final safety: NaN/Inf guard -> limiter -> hard clamp count (must stay 0 for
    ordinary musical fixtures) -> 32-bit PCM, stereo duplicate.
```

`silenceThreshold = 1.0e-9`: a voice deactivates when its exciter is finished
and estimated modal energy falls below this.

## 9. Reference host metrics (baseline, from the M6.4 host run)

These are the deterministic host values for the current baseline; they are
produced by `tests/test_dsp.cpp` and are **not** goldens (see the golden policy
in `bell_v1_freeze.md`).

| Fixture | RMS | Notes |
|---|---:|---|
| D4 v30 | 0.034242 | monotonic velocity family |
| D4 v50 | 0.050971 | |
| D4 v70 | 0.071692 | |
| D4 v90 | 0.096420 | |
| D4 v110 | 0.123737 | |
| D4 v127 | 0.135906 | still no limiter, no modal saturation |
| chord4 v90 | 0.114920 | max 4 voices |
| cluster8 v100 | 0.128233 | max 8 voices, clamp 0, modalSat 0 |
| roll v90 | 0.194392 | clamp 0, modalSat 0 |

Lifetime (time to voice inactive): D3 ~6.07 s, D4 ~4.89 s, A4 ~4.34 s.

Spectral family balance at D4 v110: hum/prime 0.0148, tierce/prime 0.0999,
nominal/prime 0.3768, upper/primary 6.1e-5.

## 10. Baseline verification

- PAN FNV regression 12/12 exact and PAN -> BELL -> PAN bit-identical
  (`build-host-m64/pan_m6_regression.md`).
- Bell safety guards: hard clamp = 0, modal saturation = 0, NaN/Inf = 0 for all
  ordinary fixtures.
- The config values above were read from source at the M6.4 base commit; they
  match the previously committed `bell_m6_metrics.md` tables.
