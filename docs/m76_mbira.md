# M7.6.1 — MBIRA V1 FROZEN

MBIRA is the ninth instrument, after frozen VIBRAPHONE V1 and before PAN. The existing registry, BOOT handler, UI name, allocator, limiter and startup PreparedNote tables remain in use. MBIRA V1 = Candidate B + Buzz ON, explicitly accepted by listening. A and C remain historical host experiments only; no further voicing exploration is authorized.

## KALIMBA inspected before implementation

The current Kalimba has five modes with ratios `[1,2.7,5.4,8.9,13]`, gains `[1,.32,.15,.06,.02]` and T60 `[2.2,1.1,.55,.25,.12]` seconds. Its Pluck exciter has gain .78, noise .22, brightness 1400–11000 Hz, velocity knee .85 and slope .42. Hardness runs .30–.95; soft/hard upper coupling is `[.18,.05,.01,0]` / `[.65,.42,.22,.10]`. Register bounds are 146.83–440 Hz, T60 scales 1.15–.80, excitation gain 1.05–.92 and cutoff brightness 1–.80. Its three strike-bus body modes are 210/420/820 Hz, gains .15/.10/.05, T60 .35/.22/.12; body excitation/output gains .12/.08, lowpass 1200 Hz and bus gain 32. No sympathetic coupling or mechanical contact exists.

## Tine architecture and physical basis

MBIRA uses six existing modal recurrences and Process6 where all six modes survive the established Nyquist pruning. The coefficients are prepared through the existing ModalVoice routine at startup; pressure and restrikes retain the existing dynamic paths. No new rendering architecture, allocation, coefficient generation or transcendental math is introduced in the Mbira sample path. The existing exact MIDI-frequency and v^1.15 lookup infrastructure now also serves Mbira, and cached strikes reuse PreparedNote's register position. One shared 128-entry/512-byte table stores the exact .90/.55 velocity-knee power curve, warmed before audio starts. Non-MIDI float velocities and alternate knee/slope configurations keep the libm fallback. A separate host render forces all original trigger math and asserts exact PCM equality.

For an ideal uniform cantilever, frequency is proportional to the squared modal wavenumber. The first roots 1.875, 4.694 and 7.855 give frequency ratios approximately 1:6.267:17.55. [TU Graz's cantilever derivation](https://lampz.tugraz.at/~hadley/memm/mechanics/cbeam.php) provides this physical reference. A rectangular tine can bend on two axes; the ratio of their flexural frequencies depends on section aspect ratio. The reduced B hypothesis places a second coupled flexural family at 3.12 and 3.12×6.267 ≈19.55, and includes a weak torsional/bridge colour at 10.8. The latter placement and effective second-family factor are design assumptions, not measured Mbira data. A/C deliberately perturb upper spacing and coupling for listening. The accepted reduced model is frozen.

| Mode | B ratio | Preset gain | Base T60 s | D3 T60 s | D5 T60 s |
|---|---:|---:|---:|---:|---:|
| Fundamental | 1 | 1 | 3.60 | 4.50 | 2.34 |
| Coupled flexure | 3.12 | .55 | 1.20 | 1.50 | .78 |
| Main-axis flexure | 6.267 | .38 | .55 | .6875 | .3575 |
| Torsional/bridge colour | 10.8 | .16 | .22 | .275 | .143 |
| Main-axis flexure | 17.55 | .11 | .11 | .1375 | .0715 |
| Coupled flexure | 19.55 | .045 | .065 | .08125 | .04225 |

Detunes are zero. The fundamental lasts longer than Kalimba, while the upper modes rapidly lose energy; there is no sustained bell cloud. Mode spacing is sparse and asymmetric. Register scaling is 1.25–.65 over D3–D5, with excitation gain 1.02–.94 and cutoff brightness .82–1.10. Low notes sustain longer and excite darker upper content; high notes tighten. The fundamental exceeds combined upper-mode energy in the five-register/four-velocity probes.

## Pluck, contact and bridge

B reuses the Pluck exciter with gain .15, noise .06, cutoff 1800–15500 Hz, knee .90 and slope .55. Hardness is .42–1.0 with the existing v^1.15 curve. Soft/hard coupling is `[1,.10,.025,.006,.002,.001]` / `[1,1,.90,.70,.55,.42]`, blended over normalized velocities .20–.98. The shaped release impulse remains the primary attack; noise is minor. The low gain preserves headroom under eight simultaneous v127 plucks without changing other instruments' gain or the limiter.

At D4, approximate release snap durations at v30/70/110/127 are 5/4/3/3 samples; the slip burst is about 1.70/1.43/1.14/1.00 ms. Kalimba at v70/v110 uses roughly 4/3 samples and 1.56/1.23 ms. These are exciter durations, not the longer audible upper-mode twang.

One shared bridge contact is driven by the already-rendered tine mix: a 700 Hz one-pole highpass, asymmetric half-wave contact, 80 Hz DC rejection and a fixed 2350 Hz resonator with 25 ms T60. Its output is added to the intact dry tines and never fed into modal states. No noise source, clipping, bitcrushing or dry-signal distortion is used. Contact strength follows `max(0,(v-.25)/.75)^2`; v30 produces zero contact. A 35 ms envelope makes contact strongest at attack and silent below 1e-6 (approximately 0.48 s after a maximum strike). Repeated strikes refresh the envelope; reset, model switching and the buzz toggle clear its state. `setMbiraBuzzEnabled(false)` leaves the tine and bridge model playable. A shared rattle can respond to summed chord vibrations; this is a bridge-level contact approximation, not an independent bottle cap on every tine.

The restrained strike-bus body uses two weak, short resonances: 310 Hz/gain .065/T60 .25 and 730 Hz/gain .035/T60 .12. Excitation/output gains are .08/.055, bus gain 20 and lowpass 1500 Hz. There is no sympathetic feedback or box simulation. A Mbira-only 20 Hz output DC guard also removes the unipolar pluck/bridge impulse area when buzz is off. Fixed contact and DC coefficients are prepared once; no buzz/body coefficients are added to PreparedNote.

## Historical candidate differences

| Parameter | A: traditional/warm | B: balanced/runtime | C: raw/metallic |
|---|---|---|---|
| Exciter gain | .145 | .15 | .095 |
| Hardness | .16–.65 | .42–1 | .70–1 |
| Cutoff Hz | 900–9500 | 1800–15500 | 2800–19000 |
| Noise amount | .04 | .06 | .07 |
| Upper ratios | B×.94 | baseline | B×1.06 |
| Upper preset gains | B×.45 | baseline | B×1.65 |
| Upper T60 | B×.80 | baseline | B×1.15 |
| Contact gain | .12 | 1 | 4 |
| Body output gain | .11 | .055 | .025 |

These changes substantially alter upper-mode content, attack, rattle and warmth. C's lower excitation gain reserves headroom for its stronger metal/contact output; the listening pack matches RMS per fixture. No preset is chosen by an automatic score.

## Structural comparison and host qualification

[Host report](qualification/m76/host_report.md) and [full precision metrics](listening/m76/host_metrics.csv) quantify the distinction. At D4 v70, upper/fundamental modal-band energy is approximately −19.06 dB for Kalimba and −12.99 dB for Mbira; at v110 it is −15.72 and −8.09 dB. The diagnostic measures 1–151 ms at each preset's mode frequencies, rather than claiming a universal perceptual brightness score. The report lists buzz-on/off difference RMS and body RMS. Kalimba has five modes, shorter fundamental sustain, much weaker upper coupling, darker high-register behavior and no rattle; Mbira has six modes, a second flexural family, stronger decay contrast and a different short bridge.

All eleven host suites pass with Process6 ON and OFF. The new suite checks all three candidates, D3/A3/D4/A4/D5 at v30/70/110/127, fundamental dominance, monotonic RMS/upper energy/contact, chord4, v127 cluster8, 100/250 ms restrikes, rolls at 250/125/50 ms, interlocking groove, deliberate steals, partition invariance, cached/fallback and fast/reference equality, and two complete registry cycles. PreparedNote adds Mbira to the full 292-note/velocity grid, pressure/restrike/fallback tests and all 81 model boundaries. The allocation guard covers all nine models. The focused pack has 21 verified RMS-matched WAVs, plus two Kalimba analysis references. [Listening index](listening/m76/README.md).

Every reported host fixture has zero NaN/Inf, hard clamps, modal saturation, maximum/average limiter GR and samples above .1/1 dB GR. Peak, RMS, crest, DC, pre-limiter peak, active voices and steals are retained in the CSV. Finite phrase means are below 1e-4 full scale; they include boundary transients. Early gain and contact development probes that required limiter action were rejected before the final pack.

## Frozen regression

All nine production aggregate FNV64 hashes remain mandatory assertions over MIDI 24–96 × v30/70/110/127. Vibraphone additionally retains twelve exact dry/M1 fixture hashes. [Hash and memory evidence](qualification/m76/regression_hashes.log).

| Instrument | Frozen FNV64 |
|---|---|
| PAN | 9e9801244165101d |
| BELL | 7c2dc6f37fb99f4b |
| TONGUE | 83f75567fb47ebaf |
| BOWL | 14772092e35bc4a7 |
| KALIMBA | e9bcabdd0b6e3bd2 |
| GLASS | 14e0dd6ba566e1cf |
| MARIMBA | 236d684f05f5f1d2 |
| VIBRAPHONE V1 | 3cea893644aa0283 |
| MBIRA V1 | f5ee8755af2fa270 |

MBIRA V1 aggregate `f5ee8755af2fa270` is now a mandatory assertion, alongside all eight earlier models. Seven canonical B + Buzz ON exact stereo int32 PCM fixtures and their FNV64 assertions are retained in M7.6.1.

## Hardware and memory

[Physical qualification report](qualification/m76/hardware_report.md) and [manifest](qualification/m76/hardware_manifest.json) retain BLE-connected single, chord4, cluster8, roll, groove, switching and buzz-off/on single callbacks, including events and transitions. Diagnostic builds keep only three fixture records at once, preserving the previous measurement-memory footprint. BOOT and restored production evidence are described in [completion report](qualification/m76/completion.md).

PreparedNote remains 132 bytes; each 73-note model table is 9,640 bytes. Nine tables total 86,760 bytes, an increase of one table (9,640 bytes). Host `sizeof(SynthEngine)` is 100,928 bytes, up 9,936 from 90,992. Fixed Mbira contact state uses 44 bytes, with a model flag and two DC floats outside the table. The ninth prepared body and cache canary account for the remaining growth. The ESP32-S3 engine symbol is 100,672 bytes. The new velocity table adds 512 bytes plus its readiness flag outside the engine; the existing voice padding accommodates the Mbira cache flag. No realtime heap allocation was added. Internal free SRAM and largest block are reported from the device, not inferred from host sizes.

The cache remains linear in model count. There is enough measured memory for this milestone, but another instrument will consume about 9.6 KB more before other state; immutable table placement or a bounded active-model cache is a plausible later infrastructure milestone. No cache redesign is included here.

## Final listening decision

The user accepted MBIRA V1 = Candidate B + Buzz ON. Modal, exciter, register, contact and bridge values above are immutable for M7.7. See [freeze report](m761_mbira_freeze.md) for the final checks. No effects, sequencer, MIDI layout or VIBRAPHONE V1 parameter changes are included.
