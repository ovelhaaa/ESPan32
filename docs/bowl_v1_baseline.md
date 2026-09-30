# Singing Bowl V1 Baseline

This document records the **M7.1 Singing Bowl V1** (`InstrumentModel::Bowl`) configuration, verified directly against source and host test qualification.

Source of truth: `main/dsp/modal_preset.h`, `main/dsp/instrument_model.cpp`, `main/dsp/instrument_model.h`, `main/dsp/dsp_config.h`, `main/dsp/modal_resonator.h`, `main/dsp/modal_resonator.cpp`, `main/dsp/modal_voice.cpp`, `main/dsp/exciter.cpp`, `main/dsp/synth_engine.h`, `main/dsp/synth_engine.cpp`, `main/dsp/peak_limiter.h`.

Production platform: ESP32-S3 @ 240 MHz, 48 kHz, 128 frames block, 8 voices.

---

## 1. Sonic Concept & Target Identity

The Singing Bowl model reproduces a traditional Tibetan / Himalayan singing bowl struck with a padded mallet:
- **Distinct from PAN:** No handpan ding/gu cavity resonances, no active sympathetic coupling bus, longer decay, prominent slow acoustic beating doublet on the prime.
- **Distinct from BELL:** Soft rounded mallet strike instead of hard clapper strike, dark-to-bright evolution without harsh high clangs, significantly lower transient noise, and lower modal saturation count (0 across single v127, chord4 v110, cluster8 v100).
- **Distinct from TONGUE:** Slower, more evolving sustain (up to 6.90 s in low register), acoustic beating doublet (~0.70 Hz fixed-Hz split), inharmonic metallic upper partials, and 7 modes/voice (56 resonators across 8 voices).
- **Acoustic Characteristics:** Soft rounded onset, strong stable fundamental, organic beating doublet, evolving metallic sustain where upper partials fade gracefully into a long, pure ring-down.

---

## 2. Modal Topology (`kPresetBowl`)

`main/dsp/modal_preset.h`: 7 modes per voice. MIDI note defines fundamental prime mode at ratio 1.0.

| # | Ratio | Gain | T60 (s) | Detune | Role |
|---:|------:|-----:|--------:|-------:|------|
| 0 | 1.0000 | 1.00 | 6.00 | 0.0000 | Fundamental / Prime |
| 1 | 1.0000 | 0.48 | 5.20 | 0.0032 | Prime Doublet (Beating ~0.70 Hz) |
| 2 | 2.3200 | 0.38 | 4.20 | 0.0000 | Low Inharmonic Partial |
| 3 | 2.9600 | 0.26 | 3.20 | 0.0000 | Mid Partial |
| 4 | 4.1500 | 0.16 | 2.20 | 0.0000 | Metallic Partial |
| 5 | 5.4500 | 0.08 | 1.40 | 0.0000 | Upper Partial |
| 6 | 6.8000 | 0.04 | 0.80 | 0.0000 | Upper Colour Shimmer |

- **Mode Count:** 7 modes/voice (Total for 8 voices = 56 modes, vs Tongue 48 modes, PAN 64 modes, and Bell 80 modes).
- **Resonator Comparison:**
  - Tongue: 48 resonators (6 modes/voice)
  - **Bowl: 56 resonators (7 modes/voice)**
  - PAN: 64 resonators (8 modes/voice)
  - Bell: 80 resonators (10 modes/voice)
- **Detune / Doublet:**
  - Mode 1 detune is `0.0032`, configured with `fixedHzSplit = true` and `splitBeatTargetHz = 0.70 Hz`.
  - In `ModalResonatorBank::setModeParameters`:
    $$\Delta f = 0.70\text{ Hz}$$
    $$f_1 = f_0 + \Delta f$$
    $$\omega_1 = \frac{2\pi f_1}{f_s}$$
  - Host test verified envelope beat cancellation ratio $0.0001 < 0.20$ across D3, D4, A4.
- **Bank Normalization:**
  $$\sum g_i^2 = 1.0^2 + 0.48^2 + 0.38^2 + 0.26^2 + 0.16^2 + 0.08^2 + 0.04^2 = 1.4744$$
  $$\text{bankNorm} = \frac{1}{\sqrt{1.4744}} \approx 0.82357$$
- **Effective Decay:**
  $$T_{60,\text{eff}} = \max(0.005, \text{def.t60} \cdot \text{dampingScale} \cdot \text{t60RegisterScale})$$
  $$r = \exp\left(\frac{\ln 0.001}{T_{60,\text{eff}} \cdot f_s}\right), \quad a_1 = 2r\cos(\omega), \quad a_2 = -r^2$$
- **Nyquist Rule:** Modes active between $10\text{ Hz} < f < 0.48 f_s$ (23,040 Hz); linear fade between $0.40 f_s$ and $0.48 f_s$.

---

## 3. Register Behavior (`kBowlVoicing`, `instrument_model.cpp`)

| Parameter | Value | Source Verification |
|-----------|------:|---------------------|
| `strikeHardnessMin` | 0.20 | Soft padded mallet floor |
| `strikeHardnessMax` | 0.90 | Firm mallet strike ceiling |
| `lowRegisterGain` | 1.05 | Verified in `instrument_model.cpp` |
| `highRegisterGain` | 0.92 | Verified in `instrument_model.cpp` |
| `lowRegisterBrightness` | 1.00 | Full warm fundamental in bass |
| `highRegisterBrightness` | 0.80 | Tamed top end to avoid harsh aliasing |
| `upperModeSoftVelocity` | 0.15 | Upper modes start opening at v=0.15 |
| `upperModeHardVelocity` | 0.92 | Upper modes reach maximum blend at v=0.92 |
| `t60LowRegisterScale` | 1.15 | Bass bowl rings up to 6.90 s on prime |
| `t60HighRegisterScale` | 0.80 | High bowl decays faster (~4.80 s) |
| `splitBeatTargetHz` | 0.70 | Target 0.70 Hz beating doublet |
| `registerLowHz` | 146.83 Hz | D3 anchor |
| `registerHighHz` | 440.00 Hz | A4 anchor |
| `fixedHzSplit` | true | Constant Hz split across octave registers |
| `silenceThreshold` | 1.0e-8 | Verified in `instrument_model.cpp` |

---

## 4. Velocity Mapping & Dynamic Coupling

- **Hardness:** $h = 0.20 + 0.70 \cdot v^{1.20}$
- **Upper Blend:** $\text{blend} = \text{clamp}\left(\frac{v - 0.15}{0.92 - 0.15}, 0, 1\right)$
- **Mode Coupling Tables (`kBowlVoicing`):**

| Mode | Role | Soft Strike ($v \le 0.15$) | Hard Strike ($v \ge 0.92$) |
|-----:|------|---------------------------:|---------------------------:|
| 0 | Fundamental | 1.00 | 1.00 |
| 1 | Beating Doublet | 0.40 | 0.55 |
| 2 | Low Inharmonic | 0.25 | 0.50 |
| 3 | Mid Partial | 0.15 | 0.40 |
| 4 | Metallic Partial | 0.05 | 0.30 |
| 5 | Upper Partial | 0.02 | 0.18 |
| 6 | Upper Colour | 0.00 | 0.10 |

Soft strikes ($v \le 0.15$) produce a dark, calming, fundamental-heavy tone with gentle beating doublet ($g_0 = 1.00, g_1 = 0.40, g_2 = 0.25$). Firm strikes ($v \ge 0.92$) progressively energize metallic upper partials ($g_3 = 0.40, g_4 = 0.30, g_5 = 0.18, g_6 = 0.10$) dynamically, retaining a warm singing bowl identity without bell-like clang.

---

## 5. Exciter Configuration & Impulse Mechanism

### 5.1 Exciter Parameters (`kBowlModelConfig.exciter`)

| Parameter | Bowl V1 | Tongue V1 (ref) | Bell V1 (ref) | Pan (ref) |
|-----------|--------:|----------------:|--------------:|----------:|
| `gain` | 0.76 | 0.78 | 0.80 | 0.80 |
| `noiseAmount` | 0.30 | 0.42 | 0.70 | 1.00 |
| `brightnessMinHz` | 600 Hz | 750 Hz | 900 Hz | 700 Hz |
| `brightnessMaxHz` | 9000 Hz | 8500 Hz | 14000 Hz | 12000 Hz |
| `velocityKnee` | 0.85 | 0.82 | 0.85 | 0.85 |
| `velocityKneeSlope` | 0.36 | 0.38 | 0.35 | 0.35 |

### 5.2 Contact Duration & Impulse Shape
- **Contact Duration:**
  $$\text{duration} = \max(3, \text{static\_cast<uint32\_t>}(15.0f - 11.0f \cdot h))$$
  - At minimum hardness ($h = 0.20$): duration = 12 samples (~0.25 ms at 48 kHz).
  - At maximum hardness ($h = 0.90$): duration = 5 samples (~0.10 ms at 48 kHz).
- **Excitation Output:** Hann-windowed half-sine impulse filtered through variable cutoff one-pole low-pass filter, mixed with 30% shaped noise.

---

## 6. Body & Sympathetic Configurations

- **Body Resonator:** `body.enabled = false` (Body = OFF).
- **Sympathetic Coupling:** `sympathetic.enabled = false` (Sympathetic = OFF).
- **Microkernel / Dispatcher Path:**
  - Generic scalar 7-mode recurrence (`processSampleReference`).
  - No specialized Process7 microkernel needed: hardware measurements show 0 deadline misses in steady state and ample headroom (~60% on chord4, ~32% on cluster8).

---

## 7. Safety Saturation & Headroom

- **Resonator Internal Saturation:** `safetySaturation = true`
- **Safety Saturator Implementation:**
  ```cpp
  if (fabsf(x) > 1.2f) {
      x = 1.2f * tanhf(x / 1.2f);
      ++saturationCount;
  }
  ```
- **Qualification Results:**
  - Single D4 v127: `satCount = 0`
  - Chord4 v110: `satCount = 0`
  - Cluster8 v100: `satCount = 0`
  - Normal musical performance operates strictly in linear region with 0 saturation overhead.

---

## 8. Memory & Stack Footprint

- **`PreparedNote` Table Size:**
  - 128 MIDI notes * 7 modes * sizeof(PreparedModeCoeffs) (16 bytes) = 14,336 bytes in heap/BSS.
  - Symmetrically matches PAN (16 KB), Bell (20 KB), and Tongue (12 KB).
- **Integrity Canaries:**
  - `canaryPre_`: `0xB001CAFE`
  - `canaryMid_`: `0xB002CAFE`
  - `canaryMid2_`: `0xB003CAFE`
  - `canaryMid3_`: `0xB004CAFE`
  - `canaryPost_`: `0xB005CAFE`
  - Verified intact (`OK=1`) on both host test and ESP32-S3 boot.
