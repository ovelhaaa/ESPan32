# PAN M5C: shared body and sympathetic coupling

M5C adds a single, allocation-free `BodyResonator` after the raw eight-voice sum. It has six fixed-Hz shell modes (110, 205, 390, 730, 1280, and 1980 Hz), deliberately separate from pitch-dependent voice modes. Their conservative gains, T60 values (0.22–1.0 s), low-pass excitation (1.8 kHz), and 0.11 output mix are centralized in `PanBodyConfig`.

The signal path is `voices -> body excitation -> dry + body -> M5A poly headroom -> lookahead limiter`. The body is never fed with post-limiter output. This prevents the shell from behaving as an added reverb or a per-note resonator bank.

Sympathetic coupling only joins currently allocated/ringing voices. The allocator sums each sample, low-passes it at 1.5 kHz, stores a weak bus for the following sample, and injects that delayed value into active voice exciters. There is no N-squared route and no hidden chromatic allocator. The initial product is intentionally extremely small (`inputGain=0.005`, `feedbackGain=0.002`); self contribution is accepted because it is negligible at this gain. A linear `maxBusLevel` clamp is a defensive safety guard and increments `sympatheticSafetyCount`; normal fixtures require zero.

Body and sympathetic controls can each be disabled with `SynthEngine::setBodyEnabled` and `setSympatheticEnabled`. With both disabled, the established dry allocator/render path is used unchanged. A sympathetic runtime transition clears only its delayed-bus/filter audio memory; `resetSympatheticDiagnostics()` is intentionally separate, so telemetry is never accidentally erased by an audio-state transition. Host M5C fixtures export the dry/body/sympathetic/full A/B WAVs and `pan_m5c_metrics.md`, including body and bus diagnostics, limiter activity, and clamp/safety counters.

Current body voicing may emphasize strike coloration more than long shell reinforcement; physical listening determines whether tuning changes are desirable. These metrics do not claim that the model reproduces a measured handpan.

The host stability suite covers 10-second single, eight-note, and roll fixtures plus a 15-second decay-after-input fixture. It checks finite output, zero hard clamps, zero sympathetic safety clamps, and body energy decay. Hardware validation is still required:

- [ ] body audible but subtle
- [ ] body does not sound like reverb
- [ ] body does not dominate first 20 ms
- [ ] body still contributes after 500 ms
- [ ] D3/A3/D4/A4 remain balanced
- [ ] chords feel more physically connected
- [ ] sympathetic effect is subtle
- [ ] sympathetic does not smear pitch
- [ ] rolls remain dynamic
- [ ] limiter remains transparent
- [ ] no stale burst after body/sympathetic toggles

M5C is not a complete physical handpan model: sympathetic resonance is limited to allocated/ringing voices plus the global body. Persistent tone-field resonators are explicitly future work.
