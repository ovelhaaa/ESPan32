# Singing Bowl V1 Listening Pack (M7.1)

This directory contains deterministic 48 kHz / 16-bit stereo WAV renders of **Singing Bowl V1** (`InstrumentModel::Bowl`), generated during host qualification (`test_dsp.cpp`).

---

## 1. File Catalog

| File | Description | Musical Target |
|:---|:---|:---|
| `bowl_D3_v30.wav` | Low register soft strike | Deep fundamental, slow 0.70 Hz beating doublet, minimal attack noise |
| `bowl_D3_v90.wav` | Low register medium strike | Warm body, sustained beating, gentle metallic overtones |
| `bowl_D3_v127.wav` | Low register hard strike | Complex metallic onset, zero modal saturation, long 6+ second tail |
| `bowl_D4_v30.wav` | Mid register pianissimo | Clean pure singing tone, round mallet impulse |
| `bowl_D4_v60.wav` | Mid register piano | Organic doublet motion, subtle mid-partial shimmer |
| `bowl_D4_v90.wav` | Mid register mezzo-forte | Balanced singing bowl voice, metallic but non-aggressive |
| `bowl_D4_v110.wav` | Mid register forte | Rich upper modes, clear pitch center |
| `bowl_D4_v127.wav` | Mid register fortissimo | Maximum strike velocity, zero hard clipping or saturation |
| `bowl_A4_v90.wav` | High register note | Tamed high frequency profile, no alias-adjacent harshness |
| `bowl_interval2.wav` | 2-note interval | Harmonic interplay between adjacent singing bowls |
| `bowl_chord4.wav` | 4-voice sustained chord | Meditative harmonic field, natural acoustic beating |
| `bowl_chord4_v80.wav` | 4-voice chord at v=80 | Gentle polyphonic bowl wash |
| `bowl_cluster8.wav` | 8-voice cluster | Full polyphonic density (56 resonators), zero voice corruption |
| `bowl_restrike_softhard.wav` | Soft strike followed by hard | Dynamic transition, smooth resonator energy buildup |
| `bowl_restrike_hardsoft.wav` | Hard strike followed by soft | Mallet damping and re-excitation response |
| `bowl_roll.wav` | Rapid succession of strikes | Dynamic mallet roll, stable limiter headroom |
| `bowl_register_sweep.wav` | Chromatic sweep across playable range | Consistent timbre and T60 scaling across registers |

---

## 2. Listening Focus Points

1. **Fundamental Stability & Pitch Center:** Notice the firm, stable fundamental on prime (1.0000) that gives the bowl its grounded, meditative quality.
2. **Doublet Motion:** Listen to the prime beating (~0.70 Hz fixed-Hz split) on sustained notes; notice how it avoids artificial LFO or chorus coloration.
3. **Decay Hierarchy:** The highest partials (modes 4–6) decay first, leaving the warm mid/low modes, which eventually fade down to the beating fundamental prime.
4. **Velocity Expressivity:** Low velocity produces a dark, rounded fundamental; high velocity excites rich metallic overtones without becoming clanging or harsh.
