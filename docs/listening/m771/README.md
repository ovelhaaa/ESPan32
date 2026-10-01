# M7.7.1 — B dynamic opening

Character B is retained. **Centered + R1 is a provisional engineering candidate;
there is no automatic listening winner and UDU is not frozen.**

Every WAV is six seconds, 48 kHz, stereo duplicated mono, 16-bit PCM. Each
comparison group is RMS-matched to its lowest raw RMS, without boosting.
Unnormalized peak/RMS/crest/DC and fault measurements are in
[host_metrics.csv](host_metrics.csv); [manifest.json](manifest.json) retains
WAV hashes and measured matched levels.

## Single pot, MIDI 60

- [v30](udu_B_dynamic_v30.wav)
- [v70](udu_B_dynamic_v70.wav)
- [v110](udu_B_dynamic_v110.wav)
- [v127](udu_B_dynamic_v127.wav)

These four files form one matching group. Their raw excitation still follows
the original B velocity law; matching removes playback loudness as a cue.
v70 is about 2.8% below the original PARTIAL cavity frequency; v127 is about
18% above it. No cavity coefficients change while a strike rings.

## Fixed PARTIAL versus provisional dynamic opening

- [Fixed PARTIAL](udu_B_groove_fixed_partial.wav)
- [Dynamic centered / R1](udu_B_groove_dynamic_opening.wav)

The two-bar 120 BPM groove is the same event sequence as M7.7. It uses MIDI
36/60/84 and the original accents and ghost notes. This pair forms its own
matching group. The fixed reference has exact M7.7 production PCM; its new
comparison gain differs from the original M7.7 opening comparison group.
The unchanged [original PARTIAL WAV](../m77/udu_B_partial.wav) remains available.

## Velocity curve comparison

- [Linear](udu_B_groove_dynamic_linear.wav)
- [Smoothstep](udu_B_groove_dynamic_smooth.wav)
- [Centered](udu_B_groove_dynamic_centered.wav)

These three files form a separate matching group. All use R1, and all preserve
the selected B character, pitch mapping, pressure and velocity excitation.

## Restrike diagnostics

- [R1: retain live cavity](udu_B_restrike_R1.wav)
- [R2: update live cavity, preserve state](udu_B_restrike_R2.wav)

This extra, focused pair tests soft/hard alternations on MIDI 60 with intervals
of 100–125 ms. It has its own RMS matching group. R1 leaves live poles alone
until cavity energy falls below 1e-9, then accepts the next opening. R2 keeps
resonator energy but changes the poles immediately. Both pass numerical safety;
that does not establish absence of an audible pitch jump or click in R2.
R1 is retained provisionally because it avoids changing a ringing cavity's
pitch. Rapid same-pot accents therefore inherit the previous opening until
the cavity dies; their excitation dynamics still change normally.

## Listening decision still required

1. Do ghosts feel deeper and rounder?
2. Do accents feel open and projected?
3. Does the groove gain physical expressiveness?
4. Does it suggest hand/hole technique rather than pitch modulation?
5. Are strong hits too tuned or cartoonish?
6. Does medium velocity remain close to accepted B/PARTIAL?

Compare the three curves and the R1/R2 tradeoff by ear. The measured pole
motion supports the intended deeper/longer to higher/shorter progression;
the host and hardware reports provide technical evidence, not a perceptual verdict.
