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
