# Glass / Crystal V1 Baseline

Status: **CANDIDATE**  
Human listening: PENDING  

This document records the **M7.3 Glass / Crystal V1** (`InstrumentModel::Glass`) specification and baseline configuration, verified directly against source, host test suite, and hardware execution.

Source of truth: `main/dsp/modal_preset.h`, `main/dsp/instrument_model.cpp`, `main/dsp/instrument_model.h`, `main/dsp/dsp_config.h`, `main/dsp/exciter.h`, `main/dsp/exciter.cpp`, `main/dsp/synth_engine.h`, `main/dsp/synth_engine.cpp`.

Production platform: ESP32-S3 @ 240 MHz, 48 kHz, 128 frames/block, 8 voices.

---

## 1. Sonic Concept & Target Identity

The Glass model reproduces struck glass / crystal percussion (crystal bowls, glass marimba, glass cups, crystal rods):
- **Acoustic Character:** Pure, transparent, bright, fragile, cold, and crystalline.
- **Clean Impulse:** Hard, crisp strike onset (`ExciterShape::Strike`) with very low broadband noise and high upper cutoff.
- **Inharmonic Resonance:** A modest set of high-Q inharmonic modes capturing transverse and bending modes of glass resonators.
- **Long Crystalline Decay:** Smooth, long fundamental sustain (~4.8 s) with high partials fading gracefully, leaving an immaculate ringing pitch.
- **Excluded Characteristics:**
  - No wooden warmth or box resonance (Kalimba box is OFF).
  - No dense brass/bronze bell clang (Bell V1 has 10 modes and heavy high cluster; Glass has 6 modes).
  - No handpan ding/gu coupling or metallic dome rumble (PAN has 8 modes + 2 body modes).
  - No deep acoustic doublet wash (Bowl V1 has prominent doublet beating; Glass V1 doublet is OFF).

---

## 2. Modal Topology (`kPresetGlass`)

`main/dsp/modal_preset.h`: 6 modes per voice (Total for 8 voices = 48 resonators, identical resonator count to Tongue).

| # | Ratio | Gain | T60 (s) | Detune | Role |
|---:|------:|-----:|--------:|-------:|------|
| 0 | 1.0000 | 1.00 | 4.80 | 0.0000 | Fundamental / Clear glass pitch |
| 1 | 2.3200 | 0.42 | 3.80 | 0.0000 | First glass partial |
| 2 | 3.8500 | 0.30 | 2.80 | 0.0000 | Bright crystal partial |
| 3 | 5.5500 | 0.18 | 2.00 | 0.0000 | High glass mode |
| 4 | 7.7500 | 0.09 | 1.20 | 0.0000 | Crystalline color |
| 5 | 10.4000 | 0.04 | 0.70 | 0.0000 | Short top shimmer |

- **Resonator Count Comparison (8 voices):**
  - Kalimba: 40 resonators (5 modes/voice)
  - **Tongue: 48 resonators (6 modes/voice)**
  - **Glass: 48 resonators (6 modes/voice)**
  - Bowl: 56 resonators (7 modes/voice)
  - PAN: 64 resonators (8 modes/voice)
  - Bell: 80 resonators (10 modes/voice)
- **Bank Normalization:**
  $$\sum g_i^2 = 1.0^2 + 0.42^2 + 0.30^2 + 0.18^2 + 0.09^2 + 0.04^2 = 1.0 + 0.1764 + 0.0900 + 0.0324 + 0.0081 + 0.0016 = 1.3085$$
  $$\text{bankNorm} = \frac{1}{\sqrt{1.3085}} \approx 0.8742$$
- **Effective Decay:**
  $$T_{60,\text{eff}} = \max(0.005, \text{def.t60} \cdot \text{dampingScale} \cdot \text{t60RegisterScale})$$
  $$r = \exp\left(\frac{\ln 0.001}{T_{60,\text{eff}} \cdot f_s}\right), \quad a_1 = 2r\cos(\omega), \quad a_2 = -r^2$$
- **Nyquist Rule & Pruning:** Natural mode pruning occurs at higher MIDI pitches; modes above $0.48 f_s$ fade smoothly. Upper modes (7.75× and 10.4×) vanish gracefully in high octaves without aliasing or level jumps.

---

## 3. Register Behavior (`kGlassVoicing`, `instrument_model.cpp`)

| Parameter | Value | Role |
|-----------|------:|------|
| `strikeHardnessMin` | 0.60 | Hard glass contact floor even at soft velocity |
| `strikeHardnessMax` | 0.98 | Crystalline rod / mallet contact ceiling |
| `lowRegisterGain` | 1.04 | Controlled bass level |
| `highRegisterGain` | 0.85 | Clean treble without piercing glare |
| `lowRegisterBrightness` | 1.00 | Bright open bass |
| `highRegisterBrightness` | 0.80 | Treble bandpass limit to prevent alias-adjacent glare |
| `upperModeSoftVelocity` | 0.15 | Upper modes emerge smoothly above v=0.15 |
| `upperModeHardVelocity` | 0.85 | Upper modes reach full saturation at v=0.85 |
| `t60LowRegisterScale` | 1.15 | Bass notes ring longer (~5.52 s on prime) |
| `t60HighRegisterScale` | 0.75 | Treble notes decay faster (~3.60 s on prime) |
| `silenceThreshold` | 1.0e-7 | Clean extinction |

---

## 4. Velocity Mapping & Dynamic Coupling

- **Hardness:** $h = 0.60 + 0.38 \cdot v^{1.15}$
- **Upper Mode Blend:** $\text{blend} = \text{clamp}\left(\frac{v - 0.15}{0.85 - 0.15}, 0, 1\right)$
- **Mode Coupling Tables (`kGlassVoicing`):**

| Mode | Role | Soft Strike ($v \le 0.15$) | Hard Strike ($v \ge 0.85$) |
|-----:|------|---------------------------:|---------------------------:|
| 0 | Fundamental | 1.00 | 1.00 |
| 1 | First glass partial | 0.32 | 0.80 |
| 2 | Bright crystal partial | 0.16 | 0.65 |
| 3 | High glass mode | 0.05 | 0.50 |
| 4 | Crystalline color | 0.01 | 0.32 |
| 5 | Short shimmer | 0.00 | 0.18 |

Soft strikes ($v \le 0.15$) emphasize a pristine, pure fundamental and soft 1st partial. Hard strikes ($v \ge 0.85$) activate shimmering upper crystal partials with balanced energy, avoiding metallic bell clang.

---

## 5. Exciter Configuration (`ExciterShape::Strike`)

Glass uses a customized, high-precision configuration of the standard `ExciterShape::Strike`:

```cpp
constexpr ExciterConfig kGlassExciterConfig{
    .gain = 0.72f,
    .noiseAmount = 0.08f,
    .brightnessMinHz = 2400.0f,
    .brightnessMaxHz = 16000.0f,
    .velocityKnee = 0.85f,
    .velocityKneeSlope = 0.40f,
    .shape = ExciterShape::Strike
};
```

- **Clean Impulse:** Noise amount is constrained to 0.08 (vs Kalimba 0.35, Bell 0.15, PAN 0.20), producing an impulse dominated by the clean raised-cosine pulse.
- **Broadband Extension:** Brightness reaches up to 16 kHz for crisp attack onsets.
- **StrikeBus:** Inactive (`strikeBusGain = 0.0f`), as Glass body resonator is disabled.

---

## 6. Body, Sympathetic & Doublet State

- **Body Resonator:** `bodyEnabled = false`. No wooden or cavity resonance.
- **Sympathetic Coupling:** `sympatheticEnabled = false`. Voice isolation preserved.
- **Doublets / Beating:** Disabled (`detune = 0.0` on all modes). The timbre relies entirely on the pure, stationary inharmonic modal relationships.

---

## 7. Process6 Microkernel Acceleration

Because Glass features exactly 6 modes per voice:
- On ESP32-S3, `ModalResonatorBank::refreshMicroKernel()` automatically selects `Process6Normal`, fully unrolling the 6 biquad state updates into register variables with zero loop overhead.
- When upper modes are pruned near Nyquist ($> 0.48 f_s$), the voice smoothly switches to scalar processing.
- Measured callback steady execution time for 8 voices is **1250 µs** (46.9% of the 2666.7 µs block deadline), providing an immense 53.1% timing margin.

---

## 8. Safety & Dynamic Headroom

Measurements from test suite and qualification fixtures:
- **Single Note D4 (v=127):** Peak = 0.73, RMS = 0.14, Limiter GR = 0.00 dB, Modal Saturation = 0, Hard Clamps = 0.
- **Chord4 (v=110):** Peak = 0.94, RMS = 0.18, Max Limiter GR = -4.80 dB, Modal Saturation = 0, Hard Clamps = 0.
- **Cluster8 (v=100):** Peak = 0.94, RMS = 0.19, Max Limiter GR = -9.88 dB, Modal Saturation = 0, Hard Clamps = 0.
- **Monotonicity:** Verified across velocities 20, 40, 60, 80, 100, 120, 127 on D4.
- **Numerical Integrity:** 0 NaN, 0 Inf, 0 hard clamps, 0 modal saturation.

---

## 9. Memory Footprint

- `sizeof(PreparedNote) = 128 bytes`
- `sizeof(PreparedNoteTable) = 9348 bytes` (73 notes × 128 bytes + 4-byte header)
- Total cache for 6 models: $6 \times 9348 = 56,088$ bytes
- `sizeof(SynthEngine) = 66,536 bytes`
- Free internal SRAM heap on boot: **105,143 bytes** (largest contiguous block: 31,744 bytes).
- Free heap after 11 continuous model cycles: **105,135 bytes** (0 byte leak, canary checks 100% OK).
