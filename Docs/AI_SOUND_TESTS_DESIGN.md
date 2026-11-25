# Fretwork AI sound tests: inventory and design (revision 2)

Date: 2026-11-22. Scope: design only. Nothing was built, run, or changed in source. Labels: EXISTS means a file in the repo does it. PARTIAL means part of it exists. NOT EXISTS means no code or data in the repo. [INFERENCE] marks a guess. Version tags: [HEAD] is commit 84716ba, [WT] is the working tree. Companion: Docs/RESEARCH_TONE_20261122.md.

## 1. Inventory

### 1.1 Tools

| Item | Path | State | What it does |
|---|---|---|---|
| SoundJudge | Tools/SoundJudge.cpp [HEAD] | EXISTS | Renders cases through FretworkAudioProcessor at 48 kHz in 480-sample blocks (render(), ending at SoundJudge.cpp:65). Writes features-<case>.json (SoundJudge.cpp:279). Built as target SoundJudge (Docs/EVALUATION.md:73-74). |
| SoundJudge features | Tools/SoundJudge.cpp:25-36 | EXISTS | body01, mid05, tail30, peak, decayDbPerSec, onsets, onsetSpan, brightAttack, brightBody (first-difference), pitchCentsDev, weakPeriodic. |
| SoundJudge decay | Tools/SoundJudge.cpp:80-94 [HEAD] | EXISTS, BUG | Body rms 0.10-0.60 s, tail rms 2.80-3.40 s. Decay = (tail dB - body dB) / 2.4. The comment says "body centre to tail centre", and those centres are 2.75 s apart. The divisor should be 2.75. Reported values are about 15 percent too fast. |
| SoundJudge passages | Tools/SoundJudge.cpp:219-270 [WT only] | EXISTS, uncommitted | neck_scale_low_E (233), sequencer_pattern_low_E (246), strum_open_E (256), single_note_A2 (259), symp_ring (264). Not in HEAD. |
| SoundJudge single-note cases | Tools/SoundJudge.cpp:210-217 [HEAD] | EXISTS | standard_soft_low_E, standard_hard_low_E, pure_nickel_hard_low_E, twelve_string_low_E. |
| ModeMatrix | Tools/ModeMatrix.cpp [HEAD] | EXISTS | Commands probe, render, artic-analyze <name> <recording.wav> <png> (argv handling at ModeMatrix.cpp:192-263). Onset rule: 10 ms RMS up 4 dB in 30 ms, above -45 dBFS, 80 ms apart (ModeMatrix.cpp:102-113). Dead-note decay to -30 dB (ModeMatrix.cpp:399). Spectrogram PNG via artic::spectrogramPng (ModeMatrix.cpp:262). |
| ArticMatrix | Tools/ArticMatrix.h [HEAD] | EXISTS | Articulation rows with real-guitar ranges. Dead-note decay range 0.01-0.15 s (ArticMatrix.h:46). |
| PluginE2E | Tools/PluginE2E/ (README.md, Audio.cpp, Learner.cpp, Verifier.cpp, PluginE2E.cpp, E2E.h) [HEAD] | EXISTS, README read | Learns a recording's notes from its audio and an independent reference, plays them through a VST3 instrument, and checks pitch and onset. Gates: pitch tolerance 1 semitone, onset 50 ms, minimum rate 0.8 (README:14-15). Exit 0 pass, 1 fail, 2 bad input (README:19). Measures transcription fidelity, not timbre. No overlap with SoundTest metrics. |
| runtests | Tools/runtests.cjs [HEAD] | EXISTS | Runs FretworkTests.exe and writes test-baseline.log (runtests.cjs:2-3). Pattern for a wrapper. |
| Others | Tools/midisend.cpp, loopcap.cpp, HostCheck.cpp, GlideRender.cpp | EXISTS | Not read in this pass. |
| On disk only | Tools/AudioDump, Tools/CpuProbe.cpp, Tools/build | ON DISK | Not tracked (git ls-files returns nothing). |

### 1.2 Tests (tracked)

| Item | Path | State | Note |
|---|---|---|---|
| Golden parameter contract | Tests/ParamGolden.inc | EXISTS | Must not change (render/quality_20261115/GATE.md:9). |
| Unit and harness tests | Tests/Harness.h, Tests/ModeArticTests.cpp, Tests/README.md, others | EXISTS | |
| Sound evaluation gates | Tests/SoundEvalTests.cpp | EXISTS, built and run | Eight FW_TEST gates: pitch 82.41 Hz within 20 cents (line 47); smooth held decay with no late rise (62-87); release damps faster with no bump (93-114); attack brighter than body (119-131); hard brighter than soft (135-155); strum onsets spread (159-181); loud strum finite and unclipped (184-195); CPU profile with no ratio assertion (200-239). The decay gate asserts continuity only, with no dB/s band (83-86). Built into FretworkTests by file(GLOB) (CMakeLists.txt:104) and run by add_test (CMakeLists.txt:132). Named in Docs/EVALUATION.md:4 and Tools/SoundJudge.cpp:5. |

### 1.3 Outside the repo

| Item | Path | State | Note |
|---|---|---|---|
| Listening clips | C:/Users/jeffr/FretworkListen | EXISTS, not in repo | clips.json lists 7 clips in 3 groups (Nylon velocity, Poly chords, Sustain), files u00-u06.wav. |
| Playback helper | C:/Users/jeffr/FretworkListen/play.ps1 | EXISTS | Speaks each clip label through System.Speech, plays the wav with SoundPlayer, waits 500 ms (lines 1-13). Records no verdict. |
| Event lists | C:/Users/jeffr/FretworkListen/t00.txt to t05.txt | EXISTS | Lines of the form "ms on|off note velocity" (for example "500 on 64 90"). |
| Pianoteq study outputs | C:/Users/jeffr/PianoteqStudy, FretworkListen/cmp | EXISTS, not committed | Docs/PIANOTEQ_STUDY.md:7. |

### 1.4 Renders and judge logs (on disk; none tracked)

git ls-files render returns nothing. Every path below is untracked, so none of these results can be reproduced from git alone.

| Item | Path | State | Note |
|---|---|---|---|
| Judge baseline 2026-11-15 | render/judge_baseline_20261115/ | EXISTS, untracked | baseline_run.log, bodies_run.log, hi_string_run.log, judge_scores.json, 11 features-*.json. |
| bodies_run.log and hi_string_run.log rows | same folder | NO TRACKED CASE | body_*, guitar_baritone, guitar_7string, guitar_nylon12, hi_* have no case definition in Tools, Source, or Tests (git grep). Their parameter wiring cannot be checked. |
| Upscale analysis 2026-11-15 | render/upscale_20261115/ | EXISTS, untracked | analysis_*.json (field harm2to8Rel). Producer not tracked (git grep finds no highE_body labels). iso_lowE_env.csv: 400 ms by 25 frets. |
| Quality gate | render/quality_20261115/ | EXISTS, untracked | GATE.md, GATE_addendum.md, golden_sha256_before.txt, guard_before_manifest.txt. |
| Golden audio | build_vs/sound_renders/head_wav/*.wav | ON DISK, untracked | GATE.md:6 names the 4 goldens. |
| Listening and AI review record | Docs/EVALUATION.md [HEAD] | EXISTS | Rounds table (lines 31-45), AI review (65-100). |
| Pianoteq comparison | Docs/PIANOTEQ_STUDY.md [HEAD] | EXISTS | Main table dated 2026-10-13 (line 1). Follow-up dated 2026-10-14 (line 36). |

### 1.5 Judge

- The pinned judge is defined in render/quality_20261115/GATE.md Gate 3 (lines 21-32). The pinned model id is openrouter/respan/span-02-20261015 (GATE_addendum.md:6), read from a one-item identity probe.
- The 2026-11-15 run (render/judge_baseline_20261115/judge_scores.json) did not follow Gate 3. It used features only, an ad hoc question, 13 scored renders (6 stock, 7 bodies), a 2.85 gate (line 3), and a 0.3 confidence floor (line 4). GATE_addendum.md:5 says 2.85 is not used. The run's model id is not recorded in the file.
- No judge call or judge script is tracked (git grep for judgeBatch and the model id finds nothing).
- Image input: ModeMatrix can write a spectrogram PNG (ModeMatrix.cpp:262). The envelope panel that Gate 3 requires is not in tracked code. The image input is PARTIAL.

### 1.6 Human verdicts

- Docs/EVALUATION.md:31-45 has a table of three rounds: what was played and the outcome. No verdict fields, no listener, no clip hash.
- The 2026-10-18 verdict (Docs/EVALUATION.md:83-100) is an AI review of features, marked provisional (lines 95-96).
- Docs/LISTENING.md is named by Tests/SoundEvalTests.cpp:9-11 as the one-line-per-round record. The file does not exist (checked). The rounds table in Docs/EVALUATION.md is the only record today.

### 1.7 Gaps (NOT EXISTS)

- One command that renders a fixed case set offline and writes a verdict per case.
- A two-stage decay fit.
- A held-decay rate gate. Tests/SoundEvalTests.cpp:83-86 asserts continuity only.
- Release time to -30 dB after note-off. ModeMatrix.cpp:399 measures dead-note decay, a different event.
- Stereo side against mid. No tracked code computes it.
- Centroid-based velocity brightness. Only first-difference brightness exists (SoundJudge.cpp:137-138).
- Sound baselines in git. The build_vs goldens and all render folders are untracked.
- Metric tolerances and a regression exit code.
- A judge stage in the repo, and the envelope image panel.
- A listening record format.

## 2. Design

### 2.1 Entry point

- Target SoundTest, source Tools/SoundTest.cpp. It compiles Tests/Harness.h (fwtest::Plugin and fwtest::Take, Harness.h:113-215), the same render and measurement path that Tests/SoundEvalTests.cpp uses, so there is one render path. [INFERENCE] A Tools target can compile Harness.h without the test runner; check at build step 1. SoundJudge's own render path is retired once SoundTest reproduces its features (clean cutover, no alias).
- Build: cmake --build build --config Release --target SoundTest. Same pattern as Docs/EVALUATION.md:73.
- Commands:
  - SoundTest.exe run --cases Tools/SoundTest/cases.json --out build/sound/<runId>. Renders each case offline through FretworkAudioProcessor. Writes <case>.wav, <case>.features.json, <case>.verdict.json, and summary.json. Exit 0 on success, 2 on bad config.
  - SoundTest.exe compare --run build/sound/<runId> --baseline tests/sound/baselines/v1. Sets each verdict status and writes compare.json. Exit 0 if all pass, 1 on any regression, 3 if the baseline or its manifest is missing.
  - SoundTest.exe baseline --run build/sound/<runId> --out tests/sound/baselines/v2 --reason "text". Writes a new version only. Refuses to overwrite. Requires --reason.
  - node Tools/SoundTest/run.cjs [--judge]. Runs run then compare, following Tools/runtests.cjs. --judge adds the AI stage.
  - node Tools/SoundTest/ai_judge.cjs build/sound/<runId>. The AI stage alone (2.6).
  - node Tools/SoundTest/record_listening.cjs --case <id> --run <dir>. Writes a listening record (2.7).

### 2.2 Case set

File: Tools/SoundTest/cases.json (new). One entry per case:

{
  "id": "low_E_hard_default",
  "group": "pluck",
  "seconds": 3.6,
  "events": [ {"t": 0.02, "on": true, "note": 40, "vel": 0.99}, {"t": 3.4, "on": false, "note": 40} ],
  "params": {},
  "metrics": ["decay_db_per_s", "partials_db", "attack_to_peak_ms", "brightness"],
  "purpose": "protects held decay and attack at the default low E"
}

note is the MIDI number SoundJudge sends. SoundJudge adds fw::kWrittenOctave when it builds the note-on (SoundJudge.cpp render()). The case file keeps that convention. The values above mirror SoundJudge.cpp:211.

Judge set: the eight GATE passages (render/quality_20261115/GATE.md:23), each rendered with body 0 (acoustic) and body 2 (electric). Sources: standard_soft_low_E (SoundJudge.cpp:210), standard_hard_low_E (:211), pure_nickel_hard_low_E (:212), twelve_string_low_E (:215), neck_scale_low_E (:233, WT only), sequencer_pattern_low_E (:246, WT only), strum_open_E (:256, WT only), single_note_A2 (:259, WT only). Four of the eight are uncommitted. A baseline cannot be built from them until they are committed (build step 0).

Metric set: the same cases, plus the release sweep (release_0.05, release_0.2, release_0.4, release_2.0; RESEARCH_TONE E3). Cases from render/judge_baseline_20261115/bodies_run.log and hi_string_run.log are excluded until their parameter wiring is in tracked code.

Adding or removing a case is a versioned change (2.5).

### 2.3 Metrics

Every metric states its window. The window is part of the definition.

| Metric | Definition | State |
|---|---|---|
| decay_db_per_s | Slope in dB of the 10 ms RMS envelope, from the body window to the tail window. Body 0.10-0.60 s, tail 2.80-3.40 s on a held note (note-off at 3.40 s). Divisor = centre spacing, 2.75 s. | EXISTS in SoundJudge with divisor 2.4 (SoundJudge.cpp:94, HEAD). Fix the divisor. The corrected value for the 2026-11-15 low E reading is -4.27 dB/s (reported -4.89). |
| decay_two_stage | Piecewise-linear fit to the same dB envelope. Break time searched on a 10 ms grid from 0.3 to 2.0 s. Reports fast_db_per_s, slow_db_per_s, break_s, rms_error_db. | NOT EXISTS |
| partials_db | Harmonic h (2 to 8) level relative to the fundamental, read at attack + 0.1 s and at 0.5 s. | PARTIAL. harm2to8Rel exists in render/upscale_20261115/analysis_*.json; the producer is not tracked. |
| attack_to_peak_ms | From the ModeMatrix onset (ModeMatrix.cpp:102-113) to the envelope peak within 100 ms. | PARTIAL. The onset rule exists. Attack-to-peak is not computed in tracked code. |
| brightness_attack_hz, brightness_body_hz | Brightness metric: the gate's version is Harness.h:181 (definition not read here); SoundJudge's is SoundJudge.cpp:137-138. Three window sets exist under this name: SoundJudge 0.02-0.10 s and 0.60-1.20 s; the gate's attack 0.02-0.06 s and body 0.3-0.5 s (SoundEvalTests.cpp:39, 126); the gate's hard versus soft 0.02-0.12 s (SoundEvalTests.cpp:149). Fix one window per metric before any baseline. | EXISTS, windows inconsistent |
| velocity_brightness | brightness_attack_hz at velocity 1.0 minus the same at 0.3, same note. | Derived from SoundJudge. NOT EXISTS as a case. |
| release_to_minus30_s | Time from note-off to 30 dB below the envelope at note-off. | NOT EXISTS. ModeMatrix.cpp:399 measures dead-note decay. |
| stereo_side_vs_mid_db | 10 log10(side energy / mid energy) over the body window. | NOT EXISTS in tracked code. Docs/PIANOTEQ_STUDY.md:13 reports it from untracked outputs. |
| pitch_dev_cents | SoundJudge pitchCentsDev. | EXISTS |
| onset_count | SoundJudge onsets. | EXISTS |

### 2.4 Verdict JSON

Schema id: fretwork.soundtest.verdict/1. Illustrative. The decay value is the corrected one (-4.27), derived from the 2026-11-15 features file, which reports -4.89 with the old divisor. Brightness values are copied from that file. Null means not computed yet. A check on a null metric has status error, never pass.

{
  "schema": "fretwork.soundtest.verdict/1",
  "case_id": "low_E_hard_default",
  "run_id": "20261122-120000",
  "engine": {"git_head": "84716ba", "git_dirty": true, "binary_sha256": "<hex>", "tool": "SoundTest 0.1"},
  "render": {"sample_rate": 48000, "block": 480, "channels": 2, "seconds": 3.6, "wav": "low_E_hard_default.wav", "wav_sha256": "<hex>"},
  "metrics": {
    "decay_db_per_s": -4.27,
    "decay_two_stage": {"fast_db_per_s": null, "slow_db_per_s": null, "break_s": null, "rms_error_db": null},
    "partials_db": {"h2": null, "h3": null, "h4": null, "h5": null, "h6": null, "h7": null, "h8": null},
    "attack_to_peak_ms": null,
    "brightness_attack_hz": 264.1,
    "brightness_body_hz": 124.7,
    "release_to_minus30_s": null,
    "stereo_side_vs_mid_db": null,
    "pitch_dev_cents": 2.93
  },
  "checks": [{"metric": "decay_db_per_s", "expected": -4.27, "tolerance": 0.5, "actual": -4.27, "status": "pass"}],
  "status": "pass",
  "ai_judge": null,
  "human": null
}

status values: pass, regression, error (a metric is null or missing), new (no baseline for the case). The "git_dirty": true value must be rejected for a baseline (2.5).

### 2.5 Baselines

- Location: tests/sound/baselines/v1/ (new folder, tracked in git). Files: manifest.json (version, created, git_head, cases_sha256, tool version, tolerances, reason) and one <case_id>.json per case with expected values and per-metric tolerance.
- Not build_vs and not render/. Neither is tracked (section 1.4).
- A baseline is written only from a clean tree (git_dirty false). Build step 0 must commit the WT-only passages first.
- Versioned: any intended change makes vN+1. The baseline command refuses to overwrite a version and requires a reason string, stored in the manifest.
- Determinism: the string's RNG is seeded per instance and advanced per pluck (GuitarString.h:108; GuitarString.cpp:231-232) [HEAD]. Expect identical WAV hashes on reruns of one build. [INFERENCE] Check with three reruns (build step 3). Across compilers, float differences are possible, so checks use tolerances. Hashes are used only for same-build reruns.
- Proposed tolerances, to be set from three repeat runs: decay plus or minus 0.5 dB/s; partials plus or minus 1.5 dB; attack plus or minus 5 ms; release plus or minus 20 ms; brightness plus or minus 5 percent; stereo plus or minus 1.5 dB; pitch plus or minus 1 cent. [INFERENCE]
- Rule: any metric outside its tolerance is a regression for that case. Any regression makes compare exit 1.

### 2.6 AI judge stage (optional, pinned to GATE.md Gate 3)

Input per render (GATE.md:22): one image, at most 512x768 (log-frequency spectrogram on top, amplitude envelope in dB below), the measured features as text, and the label "acoustic" or "electric". The judge does not hear audio.

Rubric (GATE.md:24-29). Each question scored 0 to 4: attack, decay, noise, harmonics, artifacts. The older five questions in Docs/EVALUATION.md:79-81 are superseded by the pinned set and are not used.

Pass rule (GATE.md:30; GATE_addendum.md:5): every one of the 16 renders scores 3.0 or higher on all 5 questions, with judge confidence 0.5 or higher. Acoustic and electric pass separately. This is the rule that gates.

Not used: the 2.85 gate and the 0.3 confidence floor in render/judge_baseline_20261115/judge_scores.json (lines 3-4). Those belong to an unapproved earlier task (GATE_addendum.md:5).

Other rules from GATE.md: one judgment per render; any rerun is disclosed with every attempt; a call that errors is reported as an error, not retried silently (GATE.md:31); the report names the judge model id from the batch status (GATE.md:21, 32).

Output per render: {"model": "<id from batch status>", "render": "<filename>", "voice": "acoustic|electric", "scores": {"attack": 0, "decay": 0, "noise": 0, "harmonics": 0, "artifacts": 0}, "confidence": 0.0, "pass": false}. pass means every score at least 3.0 and confidence at least 0.5.

Status: the rubric and the pass rule are pinned in render/quality_20261115/GATE.md (untracked). The image panel is PARTIAL (section 1.5). No judge call is tracked. No tracked code sets the 0.5 confidence floor.

The AI stage never fails a build by itself. It gates only as GATE.md states, and only after its inputs are tracked.

### 2.7 Human listening verdict

- Record: Docs/LISTENING.md, the file Tests/SoundEvalTests.cpp:9-11 already names (not yet created). One line per round: what was played, what changed, and the reasoning, as the gate comment specifies. Optional JSON per round in Docs/listening/ (new folder). The rounds table in Docs/EVALUATION.md links to LISTENING.md.
- Schema fretwork.listening/1: {"schema", "date", "listener", "case_id", "run_id", "wav_sha256", "reference": {"name", "level_matched": true}, "heard": "free text written before any change", "axes": {"attack", "decay", "release", "overall"} with values close, off, or fail, "action": "none", "tune", or "fix <commit>"}.
- Order, from Docs/EVALUATION.md:56-63: render with the settings under test; listen to attack, decay, and release separately; write down what was heard before changing anything.
- Playback: FretworkListen/play.ps1 plays clips from clips.json and records nothing. record_listening.cjs (new) would prompt for each axis and write the JSON.
- Status: the record format NOT EXISTS. The rounds table has no verdict fields (Docs/EVALUATION.md:31-45).

### 2.8 Build order (not started)

0. Decide what to commit. The four WT-only passages (SoundJudge.cpp:219-270) and the WT-only source changes that the baseline depends on must be committed, or the baseline must exclude them. Check: git diff --stat is empty for every file a baseline depends on.
1. Build SoundTest on Tests/Harness.h. Check: its features for the four single-note cases match the SoundJudge features JSON within rounding, or each difference is explained by the windows in section 2.3.
2. Fix the decay divisor (SoundJudge.cpp:94) to the centre spacing, and fix the feature names (body_0_6s, tail_2_8_3_4s) to match the windows. Check: recomputed low E decay equals -4.27 from the same WAV's envelope.
3. Pick one window and one definition for decay (RESEARCH_TONE E1). Check: SoundJudge and ModeMatrix agree within 1 dB/s on one WAV. Then three reruns give equal WAV hashes.
4. Add the SoundTest target, cases.json, and run. Check: two runs give equal WAV hashes.
5. Add the new metrics (two-stage, release, attack_to_peak, velocity brightness, stereo) and the held-decay rate gate. Check: unit tests on synthetic signals with known slopes and levels. New gates go in Tests/SoundEvalTests.cpp, which FretworkTests already compiles (CMakeLists.txt:104) and runs (CMakeLists.txt:132), not in a second suite.
6. Three repeat runs, set tolerances, write baseline v1 with a reason from a clean tree. Check: compare exits 0 on the same build. On a throwaway branch with release_0.05 forced, compare exits 1.
7. Wire run.cjs and the exit codes. Check: the wrapper returns the compare exit code. CI is not checked in this pass.
8. Add the judge stage per GATE.md Gate 3, after the image panel is built and tracked.
9. Add listening records for the existing rounds in Docs/EVALUATION.md.

### 2.9 Not in scope

- No auto-tuning. No change to the golden contract (Tests/ParamGolden.inc, GATE.md:9). No change to build_vs goldens. No change to the GATE rubric or threshold.

## 3. Open items

- The judge run's model id is not recorded in judge_scores.json.
- PluginE2E: read (section 1.1). Different question, no overlap with the timbre gates.
- The stereo and release metrics exist only in untracked study outputs.
- The body and guitar-type render rows have no tracked case definitions.
