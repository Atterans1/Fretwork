# Sound evaluation

Fretwork is judged two ways: objectively by the gates in
`Tests/SoundEvalTests.cpp`, and by ear against reference material. Both are
recorded so a change can be judged without replaying history.

## Objective gates

The gates measure the properties that make a plucked string read as a real
instrument. They are the part that runs in CI, so a regression in any of them
fails the build rather than waiting for someone to notice.

| Gate | What it measures | Why it matters |
| --- | --- | --- |
| plucked pitch | fundamental against the written note | a quarter-tone-flat string sounds broken even to a non-musician |
| held decay | monotonic envelope, no truncation | a sawtooth or hard-zero tail is the clearest synthetic tell |
| release damping | released tail against held tail, no click | damping must shorten the sound and add no pop |
| attack brightness | first-difference energy early against late | a plucked string starts bright; a flat attack reads as a pad |
| velocity response | hard pluck against soft pluck | velocity must change timbre, not only level |
| strum spread | onsets and their span | notes landing together read as a chord machine |
| output integrity | finite, unclipped | nothing NaN or over full scale |

Run them with the rest of the suite:

    Tools/runtests.cjs

The full run takes about 220 seconds. Give the runner the whole time: the
gates render four-second takes, so an invocation under a shell timeout dies
mid-test and reports the test that was in flight as failed.

## Listening tests

The gates cannot hear tone, so each change that affects the sound is also
played against reference material and the verdict is recorded in this file:

- what was played, with the settings that produced it
- what the reference material was, if any
- what changed as a result
- the reasoning, when the verdict is not obvious

Rounds so far:

| Round | Played | Outcome |
| --- | --- | --- |
| nylon and sustained notes | held notes across bodies | base T60 and palm-mute tuning recorded in the handoff |
| legato and pitch | hammer-ons, slides, bends | softer squeak, looser timing, more room |
| Poly chords and sustain | strummed triads | excitation lowpass made continuous across the loop wrap |

## Reference material

Comparisons that shaped the current tuning: Ample Guitar M III's articulation
set for the articulation list, and a classical-guitar library for body decay.
The measurements that came out of them are in `Docs/PIANOTEQ_STUDY.md` and
`Docs/ARTICULATIONS.md`.

## What a listening round needs

1. Render the passage with the settings under test, not the default patch.
2. Listen to the attack, the decay, and the release separately. Most synthetic
   tells live in one of the three, not in the sustained body.
3. Compare against a reference at a matched loudness, not at a matched level.
4. Write down what you heard before you change anything. The gates will catch
   the measurable part afterward.

## AI review

The gates assert physics; a model reads the same measurements and commits to a
verdict about whether the result sounds like an instrument. The review runs on
the features from the `SoundJudge` tool (`Tools/SoundJudge.cpp`, built as its
own target), which renders four low-E cases through the real processBlock and
writes `features-*.json`:

    cmake --build build --config Release --target SoundJudge
    build/SoundJudge_artefacts/Release/SoundJudge.exe <outdir>

The feature set is what a listener judges by: body/mid/tail envelope, decay
slope in dB/s, onset count and span, attack-versus-body brightness, and pitch
deviation. The reviewer is given those numbers plus published reference ranges
for a real plucked electric low E and must answer five questions: overall
verdict, decay plausibility, attack brighter than body, intonation, and
whether the 12-string octave course shows up the way physics says it should.

### Verdict recorded 2026-10-18

Input: the four feature sets from `SoundJudge` (standard soft, standard hard,
pure-nickel hard, 12-string hard; all low E, 3.4 s renders), with reference
ranges stated as decay -6 to -12 dB/s, attack 1.5-2x body brightness,
intonation drift 2-8 cents.

Those ranges were stated to the reviewer as if authoritative; they are not.
The -6 to -12 dB/s figure is an approximate recollection (see
PIANOTEQ_STUDY.md), and that study measured A3/E4, not the low E this render
used. Pianoteq's measured classical-guitar decay at A3 was -4.9 dB/s,
essentially the same as the -4.8 to -5.0 dB/s the reviewer flagged. The
verdict below is provisional and the decay finding should be treated as
unproven rather than as a defect.

- **VERDICT: close** — mostly matches, one dimension slightly off
- **DECAY: too_slow** — measured -4.8 to -5.0 dB/s against the -6 to -12 band
- **ATTACK_BRIGHTER: yes** — 1.7x attack-to-body ratio
- **INTONATION: in_tune** — 2.9 cents of drift
- **TWELVE_STRING_COURSE: course_present** — attack 371 Hz vs 280 Hz, body
  219 Hz vs 162 Hz, pitch deviation 1.46 vs 2.94 cents, exactly the direction
  an octave course should move every one of them

The reviewer's own summary: "The attack-to-body brightness ratio (~1.7x),
cent-level intonation drift, and correct octave-course behaviour in Case C
(attack 371 Hz, body 219 Hz, lower pitch deviation) all match real
plucked-string physics. The only flaw is the ~-4.8 to -5.0 dB/s decay, which
is slightly slower than the -6 to -12 dB/s of a real low E, keeping this just
short of convincing."

Actionable: the decay was the one dimension the model flagged, but the flag
rests on the unmeasured reference band above. Before tuning anything, the
band needs real numbers for a low E on a solid-body electric, measured the
way the study measured Pianoteq. Until then the held-note decay gate
(17% of body amplitude at 3.8 s) is the only grounded observation.

Limits: the reviewer sees numbers, not waveforms. A spectrogram review would
be the next step if a judge model that accepts images is available.
