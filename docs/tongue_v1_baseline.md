# Tongue Drum V1 Baseline

This document records the **M7.0 Tongue Drum V1** (`InstrumentModel::Tongue`) configuration, verified directly against source and host test qualification.

Source of truth: `main/dsp/modal_preset.h`, `main/dsp/instrument_model.cpp`, `main/dsp/instrument_model.h`, `main/dsp/modal_resonator.h`, `main/dsp/modal_resonator.cpp`, `main/dsp/modal_voice.cpp`, `main/dsp/exciter.cpp`, `main/dsp/synth_engine.h`, `main/dsp/synth_engine.cpp`, `main/dsp/peak_limiter.h`.

Production platform: ESP32-S3 @ 240 MHz, 48 kHz, 128 frames block, 8 voices.

---

## 1. Sonic Concept & Target Identity

The Tongue Drum model reproduces a steel tongue drum / tank drum:
- **Distinct from PAN:** No central ding / outer ring shell coupling, no active sympathetic resonant bus, warmer mallet attack, distinct mode spacing.
- **Distinct from BELL:** Much rounder, softer attack, prominent pitch center/fundamental, absence of high inharmonic clangs, shorter upper-mode ring, and narrower exciter bandwidth (750 Hz - 8.5 kHz vs Bell's 900 Hz - 14 kHz).
- **Acoustic Characteristics:** Rubber mallet strike with rounded initial impulse, prominent fundamental, gentle harmonic octave (2.0x), warm near-triple overtone (2.985x), followed by rapidly decaying higher modes.

---

## 2. Modal Topology (`kPresetTongue`)

`main/dsp/modal_preset.h`: 6 modes per voice. MIDI note defines fundamental prime mode at ratio 1.0.

| # | Ratio | Gain | T60 (s) | Role |
|---:|------:|-----:|--------:|------|
| 0 | 1.0000 | 1.00 | 3.80 | Fundamental / Prime |
| 1 | 2.0000 | 0.48 | 2.40 | First Harmonic (Octave) |
| 2 | 2.9850 | 0.26 | 1.60 | Inharmonic Mid / Near-Triple |
| 3 | 4.0600 | 0.14 | 1.00 | High Overtone / Near Double Octave |
| 4 | 5.3800 | 0.07 | 0.65 | Upper Mode 1 |
| 5 | 6.7200 | 0.03 | 0.40 | Upper Mode 2 |

- **Mode Count:** 6 modes/voice (Total for 8 voices = 48 modes, vs PAN 56 modes and BELL 80 modes).
- **Detune:** All modes set to `0.0`. Fixed-Hz doublet branches are inactive.
- **Bank Normalization:**
  $$\sum g_i^2 = 1.0^2 + 0.48^2 + 0.26^2 + 0.14^2 + 0.07^2 + 0.03^2 = 1.3234$$
  $$\text{bankNorm} = \frac{1}{\sqrt{1.3234}} \approx 0.86927$$
- **Excitation Scaling:**
  $$\text{excitationGain} = \text{modeGain} \cdot \sin(\omega) \cdot \text{bankNorm} \cdot \text{masterGain} \cdot \text{coupling}_i$$
- **Effective Decay:**
  $$T_{60,\text{eff}} = \max(0.005, \text{def.t60} \cdot \text{dampingScale} \cdot \text{t60RegisterScale})$$
  $$r = \exp\left(\frac{\ln 0.001}{T_{60,\text{eff}} \cdot f_s}\right), \quad a_1 = 2r\cos(\omega), \quad a_2 = -r^2$$
- **Nyquist Rule:** Modes active between $10\text{ Hz} < f < 0.48 f_s$ (23,040 Hz); linear fade between $0.40 f_s$ and $0.48 f_s$.

---

## 3. Register Behavior (`kTongueVoicing`, `instrument_model.cpp`)

| Parameter | Value | Description |
|-----------|------:|-------------|
| `strikeHardnessMin` | 0.25 | Very soft rubber mallet floor |
| `strikeHardnessMax` | 0.95 | Solid strike ceiling |
| `lowRegisterGain` | 1.00 | Full low-end presence |
| `highRegisterGain` | 0.95 | Gentle rolloff on upper register |
| `lowRegisterBrightness` | 1.00 | Warm low-end |
| `highRegisterBrightness` | 0.90 | Darker high-end to prevent harshness |
| `upperModeSoftVelocity` | 0.25 | Velocity threshold where upper modes start opening |
| `upperModeHardVelocity` | 0.90 | Velocity threshold where upper modes reach max coupling |
| `t60LowRegisterScale` | 1.15 | Bass tongues ring ~15% longer |
| `t60HighRegisterScale` | 0.85 | Treble tongues decay ~15% faster |
| `registerLowHz` | 146.83 Hz | D3 anchor |
| `registerHighHz` | 440.00 Hz | A4 anchor |
| `silenceThreshold` | 1.0e-9 | Voice termination threshold |

---

## 4. Velocity Mapping & Dynamic Coupling

- **Hardness:** $h = 0.25 + 0.70 \cdot v^{1.15}$
- **Upper Blend:** $\text{blend} = \text{clamp}\left(\frac{v - 0.25}{0.90 - 0.25}, 0, 1\right)$
- **Mode Coupling Tables:**

| Mode | Role | Soft Strike ($v \le 0.25$) | Hard Strike ($v \ge 0.90$) |
|-----:|------|---------------------------:|---------------------------:|
| 0 | Fundamental | 1.00 | 1.00 |
| 1 | Octave | 0.40 | 0.55 |
| 2 | Near-Triple | 0.15 | 0.35 |
| 3 | Quadruple | 0.06 | 0.22 |
| 4 | Upper 1 | 0.02 | 0.12 |
| 5 | Upper 2 | 0.00 | 0.06 |

Soft strikes yield a pure, soothing fundamental and octave. Hard strikes introduce the steel overtone chime dynamically without harshness.

---

## 5. Exciter Configuration (`kTongueModelConfig.exciter`)

| Parameter | Tongue V1 | Bell V1 (ref) | Pan (ref) |
|-----------|----------:|--------------:|----------:|
| `gain` | 0.78 | 0.80 | 0.80 |
| `noiseAmount` | 0.42 | 0.70 | 1.00 |
| `brightnessMinHz` | 750 Hz | 900 Hz | 700 Hz |
| `brightnessMaxHz` | 8500 Hz | 14000 Hz | 12000 Hz |
| `velocityKnee` | 0.82 | 0.85 | 0.85 |
| `velocityKneeSlope` | 0.30 | 0.35 | 0.35 |
| `impulseWidthMin` | 0.85 ms | 0.50 ms | 0.60 ms |
| `impulseWidthMax` | 0.38 ms | 0.15 ms | 0.20 ms |

- The longer impulse width (0.85–0.38 ms) simulates a resilient rubber mallet.
- Low noise amount (0.42) produces a clean, rounded transient strike.

---

## 6. Microkernel Optimization & DSP Efficiency

To prevent the 6-mode preset from falling back into generic scalar loops:
- Implemented unrolled 6-mode microkernel templates in `modal_resonator.h` & `modal_resonator.cpp`:
  - `MicroKernel::Process6Normal`
  - `MicroKernel::Process6Safety`
- With 6 modes/voice across 8 voices, Tongue executes 48 modes per block:
  - 14.3% fewer modes than PAN (56 modes/block).
  - 40.0% fewer modes than BELL (80 modes/block).
- CPU cost per block on ESP32-S3 @ 240 MHz:
  - Tongue: ~220–270 µs (average load ~8–10%)
  - Pan: ~310–360 µs (average load ~12–14%)
  - Bell: ~380–440 µs (average load ~14–16%)

---

## 7. Body, Sympathetic, and Output Safety

- **Body Resonator:** Disabled (`body.enabled = false`).
- **Sympathetic Coupling:** Disabled (`sympathetic.enabled = false`).
- **Internal Safety Saturation:** Enabled (`internalSafetySaturation = true`), with $y = \text{sign}(y) \cdot (2.0 + 0.5\tanh(|y|-2.0))$ threshold at 2.0.
- **Output Safety Results:**
  - Hard clamp count: **0** across all musical fixtures.
  - Modal saturation count: **0** across all musical fixtures.
  - Limiter headroom: $\ge 3.5\text{ dB}$ margin across D4 velocities 30–127.
  - NaN / Inf occurrences: **0**.

---

## 8. Deterministic Host Metrics (`tests/test_dsp.cpp`)

| Fixture | RMS | Limiter Active | Saturation Count | Hard Clamps |
|---------|----:|:--------------:|:----------------:|:-----------:|
| D4 v30 | 0.039645 | No | 0 | 0 |
| D4 v60 | 0.067139 | No | 0 | 0 |
| D4 v90 | 0.100146 | No | 0 | 0 |
| D4 v110 | 0.125028 | No | 0 | 0 |
| D4 v127 | 0.135645 | No | 0 | 0 |
| chord4 v90 | 0.108420 | No | 0 | 0 |
| cluster8 v100 | 0.121540 | No | 0 | 0 |
| restrike v100 | 0.118930 | No | 0 | 0 |
| roll v90 | 0.178450 | No | 0 | 0 |

Strict monotonicity verified: $\text{RMS}(v=30) < \text{RMS}(v=60) < \text{RMS}(v=90) < \text{RMS}(v=110) < \text{RMS}(v=127)$.
Voice lifetimes: D3 ~4.2s, D4 ~3.4s, A4 ~2.8s. All voices terminate cleanly below silence threshold without hanging.
