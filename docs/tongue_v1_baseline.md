# Tongue Drum V1 Baseline

Status: **FROZEN**  
Human listening: COMPLETE  
User verdict: accepted  

This document records the **M7.0 Tongue Drum V1** (`InstrumentModel::Tongue`) configuration, verified directly against source and host test qualification.

Source of truth: `main/dsp/modal_preset.h`, `main/dsp/instrument_model.cpp`, `main/dsp/instrument_model.h`, `main/dsp/dsp_config.h`, `main/dsp/modal_resonator.h`, `main/dsp/modal_resonator.cpp`, `main/dsp/modal_voice.cpp`, `main/dsp/exciter.cpp`, `main/dsp/synth_engine.h`, `main/dsp/synth_engine.cpp`, `main/dsp/peak_limiter.h`.

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

| # | Ratio | Gain | T60 (s) | Detune | Role |
|---:|------:|-----:|--------:|-------:|------|
| 0 | 1.0000 | 1.00 | 3.80 | 0.0 | Fundamental / Prime |
| 1 | 2.0000 | 0.48 | 2.40 | 0.0 | First Harmonic (Octave) |
| 2 | 2.9850 | 0.26 | 1.60 | 0.0 | Inharmonic Mid / Near-Triple |
| 3 | 4.0600 | 0.14 | 1.00 | 0.0 | High Overtone / Near Double Octave |
| 4 | 5.3800 | 0.07 | 0.65 | 0.0 | Upper Mode 1 |
| 5 | 6.7200 | 0.03 | 0.40 | 0.0 | Upper Mode 2 |

- **Mode Count:** 6 modes/voice (Total for 8 voices = 48 modes, vs PAN 64 modes [8 modes/voice] and BELL 80 modes [10 modes/voice]).
- **Resonator Comparison:**
  - Tongue has 25% fewer resonators than PAN ($48 \text{ vs } 64$).
  - Tongue has 40% fewer resonators than BELL ($48 \text{ vs } 80$).
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

| Parameter | Value | Source Verification |
|-----------|------:|---------------------|
| `strikeHardnessMin` | 0.25 | Rubber mallet floor |
| `strikeHardnessMax` | 0.95 | Solid strike ceiling |
| `lowRegisterGain` | 1.02 | Verified in `instrument_model.cpp:30` |
| `highRegisterGain` | 0.95 | Verified in `instrument_model.cpp:30` |
| `lowRegisterBrightness` | 1.00 | Warm low-end |
| `highRegisterBrightness` | 0.85 | Verified in `instrument_model.cpp:30` |
| `upperModeSoftVelocity` | 0.15 | Upper modes begin opening at v=0.15 |
| `upperModeHardVelocity` | 0.90 | Upper modes reach maximum blend at v=0.90 |
| `t60LowRegisterScale` | 1.15 | Bass tongues ring ~15% longer |
| `t60HighRegisterScale` | 0.85 | Treble tongues decay ~15% faster |
| `splitBeatTargetHz` | 0.0 | Inactive (no beating doublet) |
| `registerLowHz` | 146.83 Hz | D3 anchor |
| `registerHighHz` | 440.00 Hz | A4 anchor |
| `fixedHzSplit` | false | Inactive |
| `silenceThreshold` | 1.0e-8 | Verified in `instrument_model.cpp:33` |

---

## 4. Velocity Mapping & Dynamic Coupling

- **Hardness:** $h = 0.25 + 0.70 \cdot v^{1.15}$
- **Upper Blend:** $\text{blend} = \text{clamp}\left(\frac{v - 0.15}{0.90 - 0.15}, 0, 1\right)$
- **Mode Coupling Tables (`kTongueVoicing`):**

| Mode | Role | Soft Strike ($v \le 0.15$) | Hard Strike ($v \ge 0.90$) |
|-----:|------|---------------------------:|---------------------------:|
| 0 | Fundamental | 1.00 | 1.00 |
| 1 | Octave | 0.35 | 0.80 |
| 2 | Near-Triple | 0.15 | 0.65 |
| 3 | Quadruple | 0.05 | 0.45 |
| 4 | Upper 1 | 0.02 | 0.30 |
| 5 | Upper 2 | 0.00 | 0.18 |

Soft strikes ($v \le 0.15$) produce almost exclusively the fundamental with gentle octave support ($g_0 = 1.00, g_1 = 0.35$). Hard strikes ($v \ge 0.90$) progressively engage higher steel resonances ($g_2 = 0.65, g_3 = 0.45, g_4 = 0.30, g_5 = 0.18$) dynamically without pitch ambiguity.

---

## 5. Exciter Configuration & Impulse Mechanism

### 5.1 Exciter Parameters (`kTongueModelConfig.exciter`)

| Parameter | Tongue V1 | Bell V1 (ref) | Pan (ref) |
|-----------|----------:|--------------:|----------:|
| `gain` | 0.78 | 0.80 | 0.80 |
| `noiseAmount` | 0.42 | 0.70 | 1.00 |
| `brightnessMinHz` | 750 Hz | 900 Hz | 700 Hz |
| `brightnessMaxHz` | 8500 Hz | 14000 Hz | 12000 Hz |
| `velocityKnee` | 0.85 | 0.85 | 0.85 |
| `velocityKneeSlope` | 0.38 | 0.35 | 0.35 |

Note: `ExciterConfig` contains no model-specific `impulseWidthMin` or `impulseWidthMax` fields.

### 5.2 Actual Impulse Duration Mechanism

The impulse duration is computed generically in `main/dsp/exciter.cpp:92-93` from hardness:
```cpp
const float durationSamples = 14.0f - 11.0f * h;
impulseSamples_ = std::max<uint32_t>(3U, static_cast<uint32_t>(durationSamples));
```
- Across all models, duration ranges between 14 samples ($\approx 0.292\text{ ms}$ at 48 kHz for $h=0$) and 3 samples ($\approx 0.0625\text{ ms}$ at 48 kHz for $h=1$).
- What differentiates the Tongue attack is:
  1. Restricting the hardness bounds to $[0.25, 0.95]$ via `kTongueVoicing`.
  2. Low noise burst amount (`noiseAmount = 0.42`).
  3. Narrower low-pass cutoff range (750 Hz to 8500 Hz).
  4. Smooth velocity knee at $v = 0.85$ (slope 0.38).
  5. Dynamic overtone coupling scaling ($v=0.15 \to 0.90$).

---

## 6. Microkernel Optimization & DSP Efficiency

To prevent the 6-mode preset from falling back into generic scalar loops:
- Implemented unrolled 6-mode microkernel templates in `modal_resonator.h` & `modal_resonator.cpp`:
  - `MicroKernel::Process6Normal`
  - `MicroKernel::Process6Safety`
- With 6 modes/voice across 8 voices, Tongue executes 48 modes per block:
  - 25% fewer modes than PAN (64 modes/block).
  - 40% fewer modes than BELL (80 modes/block).

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
