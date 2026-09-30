# Kalimba / Thumb Piano V1 Baseline

Status: **CANDIDATE**  
Human listening: PENDING  
User verdict: unreviewed  

This document records the **M7.2 Kalimba / Thumb Piano V1** (`InstrumentModel::Kalimba`) specification and baseline configuration, verified directly against source, host test suite, and hardware execution.

Source of truth: `main/dsp/modal_preset.h`, `main/dsp/instrument_model.cpp`, `main/dsp/instrument_model.h`, `main/dsp/dsp_config.h`, `main/dsp/exciter.h`, `main/dsp/exciter.cpp`, `main/dsp/synth_engine.h`, `main/dsp/synth_engine.cpp`.

Production platform: ESP32-S3 @ 240 MHz, 48 kHz, 128 frames/block, 8 voices.

---

## 1. Sonic Concept & Target Identity

The Kalimba model reproduces an acoustic African thumb piano / mbira:
- **Metal Tine Physics:** Fixed-free vibrating metallic tine characterized by a dominant pitch and non-harmonic bending overtones.
- **Fast Pluck Onset:** Distinct pluck excitation (`ExciterShape::Pluck`) with high initial displacement release and short, crisp click transient.
- **Resonant Wood Box:** Subtle 3-mode wooden cavity/soundboard body excited via `StrikeBus`.
- **Decay Profile:** Percussive and warm, with moderate sustain (1.6–2.8 s on fundamental) decaying cleanly to silence without long metallic wash or cloud tails.
- **Distinct from Other Instruments:**
  - Distinct from PAN: Plucked tine onset rather than struck metallic dome, no ding/gu coupling, much lighter modal topology.
  - Distinct from Bell: No dense high clang, fundamental-dominated, wooden warmth rather than brass/bronze bell resonance.
  - Distinct from Tongue: Shorter T60, asymmetric pluck transient, distinct tine ratios (1.00, 2.70, 5.40, 8.90, 13.00), lower CPU budget.
  - Distinct from Bowl: Warm percussive strike rather than long meditative sustain (~2.2 s vs ~6.0 s), no acoustic doublet beating.

---

## 2. Modal Topology (`kPresetKalimba`)

`main/dsp/modal_preset.h`: 5 modes per voice (Total for 8 voices = 40 resonators, making Kalimba the lightest instrument model in ESPan32).

| # | Ratio | Gain | T60 (s) | Detune | Role |
|---:|------:|-----:|--------:|-------:|------|
| 0 | 1.0000 | 1.00 | 2.20 | 0.0000 | Fundamental / Tine pitch |
| 1 | 2.7000 | 0.32 | 1.10 | 0.0000 | First tine bending overtone |
| 2 | 5.4000 | 0.15 | 0.55 | 0.0000 | Second bending partial |
| 3 | 8.9000 | 0.06 | 0.25 | 0.0000 | High metallic tine click / colour |
| 4 | 13.0000 | 0.02 | 0.12 | 0.0000 | Upper tine partial (short shimmer) |

- **Resonator Count Comparison (8 voices):**
  - **Kalimba: 40 resonators (5 modes/voice)**
  - Tongue: 48 resonators (6 modes/voice)
  - Bowl: 56 resonators (7 modes/voice)
  - PAN: 64 resonators (8 modes/voice)
  - Bell: 80 resonators (10 modes/voice)
- **Bank Normalization:**
  $$\sum g_i^2 = 1.0^2 + 0.32^2 + 0.15^2 + 0.06^2 + 0.02^2 = 1.0^2 + 0.1024 + 0.0225 + 0.0036 + 0.0004 = 1.1289$$
  $$\text{bankNorm} = \frac{1}{\sqrt{1.1289}} \approx 0.94119$$
- **Effective Decay:**
  $$T_{60,\text{eff}} = \max(0.005, \text{def.t60} \cdot \text{dampingScale} \cdot \text{t60RegisterScale})$$
  $$r = \exp\left(\frac{\ln 0.001}{T_{60,\text{eff}} \cdot f_s}\right), \quad a_1 = 2r\cos(\omega), \quad a_2 = -r^2$$
- **Nyquist Rule:** Natural mode pruning occurs at higher MIDI pitches; modes above $0.48 f_s$ fade smoothly, which matches physical tine acoustics where high overtones naturally extinguish.

---

## 3. Register Behavior (`kKalimbaVoicing`, `instrument_model.cpp`)

| Parameter | Value | Role |
|-----------|------:|------|
| `strikeHardnessMin` | 0.35 | Soft thumb flesh pad floor |
| `strikeHardnessMax` | 0.95 | Crisp fingernail pluck ceiling |
| `lowRegisterGain` | 1.08 | Full resonant body in bass register |
| `highRegisterGain` | 0.88 | Controlled upper register output |
| `lowRegisterBrightness` | 1.00 | Warm acoustic bass |
| `highRegisterBrightness` | 0.75 | Smooth treble without harshness |
| `upperModeSoftVelocity` | 0.18 | Upper modes open progressively above v=0.18 |
| `upperModeHardVelocity` | 0.88 | Upper partials reach maximum blend at v=0.88 |
| `t60LowRegisterScale` | 1.20 | Bass tines sustain longer (~2.64 s on prime) |
| `t60HighRegisterScale` | 0.70 | Treble tines decay rapidly (~1.54 s on prime) |
| `silenceThreshold` | 1.0e-7 | Clean percussive cutoff |

---

## 4. Velocity Mapping & Dynamic Coupling

- **Hardness:** $h = 0.35 + 0.60 \cdot v^{1.15}$
- **Upper Mode Blend:** $\text{blend} = \text{clamp}\left(\frac{v - 0.18}{0.88 - 0.18}, 0, 1\right)$
- **Mode Coupling Tables (`kKalimbaVoicing`):**

| Mode | Role | Soft Pluck ($v \le 0.18$) | Hard Pluck ($v \ge 0.88$) |
|-----:|------|---------------------------:|---------------------------:|
| 0 | Fundamental | 1.00 | 1.00 |
| 1 | 1st Bending | 0.18 | 0.65 |
| 2 | 2nd Bending | 0.05 | 0.42 |
| 3 | Tine Click | 0.01 | 0.22 |
| 4 | Upper Shimmer | 0.00 | 0.10 |

Soft plucks ($v \le 0.18$) produce a round, wooden, fundamental-heavy tone with virtually zero high partials. Hard plucks ($v \ge 0.88$) activate bright metallic tine partials without developing bell clang.

---

## 5. Pluck Exciter Architecture (`ExciterShape::Pluck`)

To capture the physical displacement release of a plucked tine, `ExciterShape::Pluck` was added while strictly preserving bit-exact arithmetic for historical models (`ExciterShape::Strike`).

```cpp
enum class ExciterShape : uint8_t {
    Strike = 0,
    Pluck = 1
};
```

### 5.1 Pluck Dynamics
- **Displacement Release Pulse:** Asymmetric waveform with fast attack (0.15 ms / ~7 samples) and exponential release toward zero (1.0 ms / ~48 samples).
- **Filtered Noise Transient:** Short click component bandpass-filtered with velocity-dependent brightness ($f_c \in [1200, 11000]\text{ Hz}$), decaying within 1.0–2.5 ms.
- **Exciter Config (`kKalimbaModelConfig`):**
  - `shape`: `ExciterShape::Pluck`
  - `gain`: 0.80
  - `noiseAmount`: 0.25
  - `brightnessMinHz`: 1200.0 Hz
  - `brightnessMaxHz`: 11000.0 Hz
  - `velocityKnee`: 0.85
  - `velocityKneeSlope`: 0.42
- **Bit-Exact Rule:** Verified bit-identical across PAN, Bell, Tongue, and Bowl in host oracles.

---

## 6. Wood Box Body Resonator (`BodyResonator`)

A lightweight 3-mode wooden cavity/soundboard resonator was evaluated in an A/B test:
- **K0:** Tine modal bank only (`body.active = false`).
- **K1:** Tine + 3-mode wood box resonator (`body.active = true`, excited via `StrikeBus`).

### 6.1 Body Modes
| Mode | Frequency | Gain | T60 (s) | Role |
|-----:|----------:|-----:|--------:|------|
| 0 | 210.0 Hz | 0.14 | 0.28 | Main wooden cavity air mode |
| 1 | 420.0 Hz | 0.08 | 0.18 | Soundboard low bending mode |
| 2 | 820.0 Hz | 0.04 | 0.10 | Upper soundboard body color |

### 6.2 A/B Verdict
K1 was selected: it provides the subtle warmth of a wooden soundboard box without masking tine pitch or creating reverb-like wash. Modal saturation remains 0 and hard clamp count remains 0.

---

## 7. Polyphony Forensics (ESP32-S3 @ 240 MHz, Candidate 25)

Measured on connected physical hardware (`COM10`) with BLE-MIDI active and 5 µs histogram bins:

| Fixture | Voices | Class | Avg (µs) | p95 (µs) | p99 (µs) | Max (µs) | Misses | Hard Clamps | Saturation |
|:---|:---:|:---|---:|---:|---:|---:|:---:|:---:|:---:|
| **Kalimba chord4** | 4 | `true_steady` | 1091.15 | 1235 | 1270 | 1535 | 0 | 0 | 0 |
| **Kalimba chord4** | 4 | callback steady | 1107.68 | 1260 | 1335 | 1750 | 0 | 0 | 0 |
| **Kalimba chord4** | 4 | callback event | 1697.84 | 1845 | 1985 | 1980 | 0 | 0 | 0 |
| **Kalimba cluster8** | 8 | `true_steady` | 1720.22 | 1970 | 2005 | 2279 | 0 | 0 | 0 |
| **Kalimba cluster8** | 8 | callback steady | 1736.73 | 1990 | 2040 | 2410 | 0 | 0 | 0 |
| **Kalimba cluster8** | 8 | callback event | 2418.80 | 2720 | 2860 | 2855 | 4 | 0 | 0 |

### 7.1 Key Observations
1. **Steady Deadline Margin:** 0 steady deadline misses across both 4-voice chord and 8-voice cluster fixtures (maximum callback steady is 2410 µs, safely within the 2666.7 µs deadline).
2. **CPU Budget:** At 4 voices (chord4), steady load is only ~41.5% (1107 µs), making Kalimba extraordinarily efficient. At 8 voices, steady callback is 1736.73 µs (~65% load).
3. **Safety:** 0 hard clamps, 0 modal saturation instances, 0 bad voices, 0 BLE drops.

---

## 8. Hardware Model Cycling Qualification

The 5-model cycling sequence was qualified on hardware:
```text
PAN -> BELL -> TONGUE -> BOWL -> KALIMBA -> PAN
```
- Short-press: switches UI screen modes without disturbing instrument model.
- Long-press (>= 800 ms): advances instrument model cyclically, updates UI label, and resets audio engine at block boundary.
- Hardware automated qualification verified 3 short presses + 9 complete 5-model cycles with 0 audio or visual regressions.
