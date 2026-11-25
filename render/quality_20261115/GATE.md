# Fretwork quality pass: verification gate (pinned 2026-11-15, before any render or judge run)

Pinned by Main from the user's brief and two answers. Applied unchanged. Any deviation is reported as a deviation.

## Gate 1: goldens
- Golden audio: build_vs/sound_renders/head_wav/*.wav (4 files, commit 84716ba). Golden contract: Tests/ParamGolden.inc.
- These files are never regenerated, edited, moved or re-pathed. Their SHA-256 is recorded before work starts and checked at the end.
- New renders of the same 4 passages are compared with head_wav. Byte-identical is reported as identical. Any difference is reported per passage (sample count differing, peak and RMS delta in dB) and must trace to a named fix. A difference with no named cause is a defect.
- ParamGolden.inc and the parameter layout stay unchanged ("params: layout" contract).

## Gate 2: sweep
- Coverage: every fret 0..24, strings 0 (low E) and 5 (high E), every body type the engine ships, each note rendered in isolation (IsoRender method), velocity 0.5, default mic/room. Baseline run before fixes, same run after.
- Defect thresholds, same before and after:
  - Swell: smooth-envelope growth after the attack of 3.0 dB or more (IsoRender "swell gain dB").
  - Late onset: 10% onset later than 2.5 fundamental periods.
  - Level outlier: a note's peak more than 3.0 dB from the median peak of its string and body.
  - Noise: non-harmonic residual in the 0.7-0.9 s tail more than 6.0 dB above the median residual of its string and body (residual = tail energy outside +/-3% bands around the first 20 harmonics).
- Pass: no note over any threshold on any fret, string or body, and each previously reported defect is either gone or shown by measurement never to have existed in the current tree (with the evidence).

## Gate 3: judge
- Judge: the omp judge tool (judgeBatch). Its model id is read from the batch status and recorded with the verdicts. Expected from config: modelRoles.judge = openrouter/respan/span-02. If the reported id differs, it is reported, not hidden.
- The judge sees images and numbers. It does not hear audio. Input per render: one image, at most 512x768 (log-frequency spectrogram on top, amplitude envelope in dB below), plus the measured features as text and the label "acoustic" or "electric".
- Renders judged: the 8 SoundJudge passages (standard_soft_low_E, standard_hard_low_E, pure_nickel_hard_low_E, twelve_string_low_E, neck_scale_low_E, sequencer_pattern_low_E, strum_open_E, single_note_A2), each rendered once with an acoustic voice (body 0, Dreadnought Steel) and once with an electric voice (body 2, Solid Electric). 16 renders, all judged, none dropped.
- Rubric, each scored 0 to 4 (0 = severe defect visible, 1 = clear defect, 2 = minor defect, 3 = clean with small imperfections, 4 = clean):
  1. attack: each note has one clear onset, with no double attack, pre-echo or late swell.
  2. decay: each note decays smoothly, with no re-growth or bursts.
  3. noise: no broadband bursts, buzz bands or rising noise floor between or under notes.
  4. harmonics: stable harmonic partials that fit a plucked string of the labelled voice.
  5. artifacts: no clicks, dropouts, or aliasing lines.
- Pass: every one of the 16 renders scores 3.0 or higher on all 5 questions with judge confidence 0.5 or higher. Acoustic and electric are passed separately; acoustic evidence does not count for electric.
- One judgment per render. Any rerun is disclosed with every attempt and verdict. A judge call that errors is reported as an error, not retried silently.
- Report: judge model id, this rubric, per-passage verdicts, and the filenames judged.

## Out of scope this pass
Ship build, test suite, pluginval, CPU rerun, refactors, sample playback.
