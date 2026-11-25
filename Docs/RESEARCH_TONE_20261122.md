# Fretwork tone research, 2026-11-22 (revision 2)

Question: why does the guitar sound off? This note ranks suspects, checks the scout findings against the files they cite, and lists the experiments that settle each suspect. Docs only. No source, build, or git change was made.

Labels. [VERIFIED] means read in the cited file this session. [SCOUT-REPORTED] means the scout said it and it was not reproduced here. [INFERENCE] means reasoning or arithmetic, not measured. Version tags: [HEAD] is commit 84716ba, [WT] is the working tree.

## 1. Versions and canonical copy

- Canonical copy: C:/Users/jeffr/Fretwork. HEAD 84716ba (2026-11-13, "Snapshot the current Fretwork working state"). [VERIFIED]
- Uncommitted changes (git diff --stat): Source/dsp/BodyModel.cpp, BodyModel.h, GuitarEngine.cpp, GuitarEngine.h, GuitarString.cpp (+6 -1), OutputStage.cpp; Source/Params.cpp (+6); Source/PluginProcessor.cpp (+7); Tests/main.cpp (+77); Tools/SoundJudge.cpp (+56, the passages block starts at line 219). [VERIFIED]
- GuitarString.cpp: HEAD blob 5d3c4cf, WT blob 5b0c99a. The only hunk is @@ -433,3 +433,7 @@, so lines 1-432 are identical in HEAD and WT. [VERIFIED]
- Sibling copy C:/Users/jeffr/FretworkListen/sliceA_src/Source/dsp/GuitarString.cpp has blob 5d3c4cf, the same as HEAD. It is a HEAD copy, not a separate version. [VERIFIED]
- GuitarString.h (blob 6625629), StringModelParams.h, ModelTables.h, and LoopDesign.cpp are identical in HEAD, WT, and the sibling. [VERIFIED]
- GuitarEngine.cpp: WT blob 8fa29bb differs from HEAD e6aadc2. The sibling matches HEAD. [VERIFIED]
- The other Fretwork* folders hold no GuitarString copy within four directory levels. [VERIFIED walk]
- The sympathetic gate at GuitarString.cpp:433-438 exists in WT only. Any claim that depends on it is WT-only. [VERIFIED]
- Line numbers below are WT unless tagged [HEAD]. Params.cpp and PluginProcessor.cpp are shifted by +6 lines in WT. The defaults are unchanged. Cited HEAD lines: Params.cpp release 72, material list 142-147, age 151, sustain 157; PluginProcessor.cpp body loss 194, scale length 195. [VERIFIED]
- Render folders (render/...) and build_vs goldens are not tracked by git. Every render-based number below rests on files that git cannot reproduce. [VERIFIED git ls-files]

## 2. Designed fundamental decay, from code, at defaults

Each input was read in the file named.

- baseT60 for string 0 (low E) = 19.6 s. LoopDesign.cpp:35 [HEAD].
- sustain01 = 130 / 100 = 1.30. Default 130, Params.cpp:157 [HEAD]; clamp and scale, GuitarString.cpp:198.
- materialScale for material 0 (Steel Light) = 1.00. Default index 0, Params.cpp:142-147 [HEAD]; the first entry is Steel Light, ModelTables.h:132; materialScale, LoopDesign.cpp:39-45.
- age01 = 20 / 100 = 0.20. Default 20, Params.cpp:151 [HEAD]; GuitarString.cpp:199. Age factor = 1 + (0.35 - 1) x 0.2 = 0.87, LoopDesign.cpp:81.
- bodyLossScale = kBodyLossScale[0] = 1.00. ModelTables.h:73. Body default 0 (Dreadnought Steel), Params.cpp:123 [WT].
- Damping: Open (enum value 0), Contracts.h:13; Articulation default, Contracts.h:24. Assumes plain MIDI notes carry the default articulation. [INFERENCE]
- Note 40 is routed to string 0 (lowest fitting string). Docs/PIANOTEQ_STUDY.md:39 describes the routing rule. [INFERENCE]

T60fund = 19.6 x 1.30 x 1.00 x 0.87 x 1.00 = 22.17 s (LoopDesign.cpp:78-83).

Designed fundamental decay = 60 / 22.17 = 2.71 dB/s. loopGainAt (LoopDesign.cpp:67-70) gives -60 dB per T60 by construction.

If the release-path multiplier applies the same gain a second time (S1), the held decay would be 2 x 2.71 = 5.41 dB/s. [INFERENCE until E2]

## 3. Decay readings: conditions and dates (S2)

| # | Source | Date | Condition and window | Reading | Status |
|---|---|---|---|---|---|
| a | Docs/PIANOTEQ_STUDY.md:12 | 2026-10-13 (title, line 1) | Held A3 and E4, ModeMatrix. Window not stated. Before the 10-14 table change (line 37 says "now"). | Fretwork default -18 / -22 dB/s; nylon -31 / -36; Pianoteq -4.9 / -7.8 | [VERIFIED] values; "before the change" is [INFERENCE] |
| b | Docs/PIANOTEQ_STUDY.md:36-37 | 2026-10-14 (follow-up) | After the base T60 table was multiplied by 2.2. Held A3 and E4. Window not stated. | Fretwork default A3 -8.4, E4 -10.9; nylon A3 -9.2, E4 -11.4; Pianoteq A3 -4.9, E4 -7.8 | [VERIFIED] values |
| c | Source/LoopDesign.cpp:34 (comment) | undated | Old table, held A3. | "fell 15 dB/s" | [VERIFIED] comment; no data file |
| d | render/judge_baseline_20261115/features-standard_hard_low_E.json | 2026-11-15 | Low E, note 40, velocity 0.99, held (note-off at 3.40 s, Tools/SoundJudge.cpp:211). Body rms 0.10-0.60 s, tail rms 2.80-3.40 s (SoundJudge.cpp:80, 82). Divisor 2.4 (SoundJudge.cpp:94) [HEAD]. | Reported -4.89 dB/s | [VERIFIED] value |
| d' | same, recomputed | 2026-11-15 | Divisor = body-centre to tail-centre spacing = 3.10 - 0.35 = 2.75 s. | -4.89 x 2.4 / 2.75 = -4.27 dB/s | [VERIFIED] arithmetic |
| g | Tests/SoundEvalTests.cpp:59-61 (comment) | undated, tracked | Held low E at velocity 0.85 (line 66). Body rms 0.05-0.15 s, tail rms 3.50-3.80 s (lines 78-79). | Comment says the tail is about 17 percent of body at 3.8 s. Our arithmetic from the window centres (0.10 s to 3.65 s, 3.55 s apart): -15.4 dB / 3.55 s = -4.3 dB/s. | Comment, not a test output. Lies between 2.71 and 5.41 dB/s. |
| e | Docs/EVALUATION.md:98-99 | 2026-10-18 | Judged against a -6 to -12 dB/s band. The band is "an approximate recollection" (EVALUATION.md:90-96). | "too_slow" at -4.8 to -5.0 | Provisional by its own text (EVALUATION.md:95-96) |

Consistency check: a / 2.2 = -8.2, against b's -8.4. That fits decay proportional to 1/T60 [INFERENCE].

Consequences.

- Line 21 of the Pianoteq study, "sustain is 3-6x shorter than Pianoteq", describes reading a. After the change, reading b gives about 1.7x at A3 (8.4 / 4.9). [INFERENCE from a and b]
- The SoundJudge number in d is 15 percent too fast because of the divisor (SoundJudge.cpp:94). The 2026-10-18 "too_slow" flag used a similar number, so it inherits the error. [VERIFIED arithmetic]
- The readings use different notes (A3, E4, low E), different windows, and different dates. None of them can be compared with a reference band. The reference band is unsourced (EVALUATION.md:90-96; PIANOTEQ_STUDY.md:23).

## 4. Ranked suspects

Ranks reflect how much a suspect could change the sound and how much evidence supports it. Evidence strength is stated separately.

### S1. Designed decay gain may be applied twice (rank 1; candidate)

Mechanism, from code:
- solveLoopOnePole chooses lpG so the lowpass gain at f0 equals g0 = loopGainAt(f0, t60Fund). LoopDesign.cpp:136, 163-164 [HEAD]. loopGainAt: LoopDesign.cpp:67-70 [HEAD].
- pluck() sets naturalGain_ = loopGainAt(baseF0_, design_.t60Fund), then releaseGain_ = naturalGain_ and releaseTarget_ = releaseGain_. GuitarString.cpp:277-279 [HEAD].
- tick() multiplies the feedback signal by releaseGain_ on every sample, after the lowpass and the dispersion stages. GuitarString.cpp:432 [HEAD].
- So per round trip at f0 the gain is g0 (filter) x g0 (releaseGain_) = g0 squared. Decay would be 2 x 60/T60fund.
- The header calls naturalGain_ "loop gain for the string's own T60" (GuitarString.h:85). That describes a gain the filter already realizes. [VERIFIED comment]

Evidence on magnitude: the designed value is 2.71 dB/s (section 2). The doubled value is 5.41 dB/s. The measured value is -4.27 dB/s (corrected, section 3 d'). The measurement sits between the two. The measurement window is the held tail, which is the right window for this question. No existing gate would catch a rate error: the decay gate asserts continuity only (Tests/SoundEvalTests.cpp:83-86). [INFERENCE] The candidate is not confirmed. E2 decides it.

### S2. Decay metrics disagree (rank 2; method)

Section 3 lists five readings from three tools and three dates. The divisor bug (SoundJudge.cpp:94) adds 15 percent to SoundJudge readings. No decay-based change can be judged until one window and one definition are fixed. E1 does this.

### S3. Linear fractional-delay reader adds unmodeled high-frequency loss (rank 3; code verified, magnitude computed)

- The reader is two-tap linear (GuitarString.cpp:87-99) [HEAD]. The comment at 83-86 says Catmull-Rom was tried and measured worse for pitch.
- LoopDesign.cpp:276-278 says the reader is four-point Catmull-Rom with no compensation. The code is linear. Either the comment is stale or the delay budget assumes the wrong reader. [VERIFIED]
- Two-tap magnitude at fraction f: sqrt((1-f)^2 + f^2 + 2f(1-f)cos w). At f = 0.5 this is |cos(w/2)|. At 48 kHz that is about -0.2 dB per pass at 3 kHz and -2.0 dB per pass at 10 kHz. [INFERENCE from the standard formula; not measured]
- The loop solver models only the lowpass (LoopDesign.cpp:133-169). The delay budget (256-274) does not model the reader loss.
- Predicts: upper partials decay faster than designed, and the loss changes with each note's fractional delay.

### S4. Nylon partials 6 to 8 sit below Pianoteq (rank 4; reading is pre-fix)

- Values, Docs/PIANOTEQ_STUDY.md:15, dated 2026-10-13 (reading a): partials 6-8 at A3: Pianoteq -24, -24, -32 dB; nylon -39, -42, -52 dB; steel default -27, -30, -26 dB. [VERIFIED]
- The excitation fix (study line 38, dated 2026-10-14; code at Source/dsp/Excitation.cpp:68-69 [HEAD]) changed the nylon behavior after this reading. The partial values are not current. [VERIFIED]
- harmRatio is 0.10 for nylon and 0.12 for Steel Light (ModelTables.h:134, 132) [HEAD]. That is a 17 percent difference. It cannot alone explain 15 to 20 dB. [INFERENCE]
- Candidate causes: the excitation (now changed), t60Scale 0.90 for nylon (ModelTables.h:134), and the Pianoteq comparison itself (different preset; octave offset, study lines 26-27).

### S5. Release path composes the release target with the designed filter (rank 5; candidate; design question)

- releaseSec is documented as the T60 target of the damped string (Source/Contracts.h:33 [HEAD]). Short release tails are therefore the designed behavior. They are not evidence of a defect, and this note does not use them as such.
- releaseTarget = min(naturalGain_, loopGainAt(f0, releaseSec)) (GuitarString.cpp:340-342) [HEAD]. The filter's own gain stays in the loop during release. So the release decay rate is the designed rate plus the release rate. At the default 0.4 s that is 2.71 + 150 = 152.7 dB/s, which is -30 dB in about 0.20 s. [INFERENCE from the same code]
- The ramp to the target is linear in gain over 10 ms (GuitarString.cpp:344-345, 421-432) [HEAD]. This is a design choice.
- The sympathetic gate (GuitarString.cpp:437-438) exists in WT only. It scales string-to-string drive by the release state.
- Open question: whether the designed rate should persist during release. E3 measures the rate and checks the sum.

### S6. Attack and velocity brightness (rank 6; reading is pre-fix)

- Reading a (10-13) gave nylon velocity brightness as flat (292 to 260 Hz, 341 to 338 Hz; study line 14) and attack to peak as 3 ms default, 30 ms nylon, 18 ms Pianoteq (study line 18). Both are pre-fix.
- Study line 38 (10-14) reports the share of energy above 1 kHz at 30 ms, A3. Nylon before the fix: -12.0, -15.9, -18.6 dB at velocity 40, 80, 120. After the fix: -31.4, -25.5, -21.7 dB. Default after the fix: -25.0, -17.9, -15.9 dB. Pianoteq rises -29 to -19 dB. This metric differs from the centroid in line 14. [VERIFIED values]
- Click burst: 2.5 ms, high-passed and band-limited by velocity (GuitarString.cpp:269-271, 109-139) [HEAD].
- Conclusion: the brightness behavior of nylon has changed since the readings. The current behavior is not measured. E5 measures it.

### S7. Dispersion (rank 7; low)

- Four identical first-order allpass stages share one coefficient (GuitarString.cpp:403-418; LoopDesign.cpp:265-270) [HEAD]. The header says so (GuitarString.h:23).
- Partial stretch: Fretwork about 0 cents, Pianoteq about -2 (study line 19). The difference is small. [INFERENCE] Not a likely main cause.

### S8. Fret-3 swell (rank 8; not reproduced; the gate metric is not in the repo)

- Measured from render/upscale_20261115/iso_lowE_env.csv (2026-11-15, frets f0 to f24 by column, [INFERENCE] from the header): fret 3 rises 11.1 dB from its 57 ms minimum (-31.2 dB) to its 177 ms peak (-20.1 dB). [VERIFIED, our own measure]
- The same rise measure gives a median of 11.8 dB across the 25 frets. Fret 2 gives 27.4 dB, fret 1 24.8, fret 0 21.2. Fret 3 is not an outlier on this measure. [VERIFIED]
- The scout's "about 4 dB" was not reproduced. [SCOUT-REPORTED, not reproduced]
- The gate threshold is 3.0 dB or more of smooth-envelope growth after the attack (render/quality_20261115/GATE.md:14, Gate 2). [VERIFIED]
- The gate metric is named "IsoRender swell gain dB". The name is not in any tracked file under Tests, Tools, or Source (git grep). Our rise measure is not that metric. Its 11.1 dB value cannot be compared with the 3.0 dB threshold, and it does not indicate a gate failure. [VERIFIED]
- Resolve the gate metric first (E7). Until then, no fret is a swell failure.

### S9. Body filtering (rank 9; wiring unverified)

- analysis_highE_body1_mic70.json shows harmonic 2 at -12.3 dB relative to the fundamental, against +1.3 dB for body 0 (render/upscale_20261115/analysis_*.json, field harm2to8Rel). [VERIFIED values]
- The producer of the analysis files is not tracked: git grep for the highE_body labels in Tools, Source, and Tests finds nothing. The body index in those renders is not verified. [VERIFIED]
- The bodies_run.log and hi_string_run.log rows (body_dreadnought, guitar_baritone, guitar_7string, hi_baritone, and others) have no tracked case definition. git grep finds none in Tools, Source, or Tests. Their parameter wiring cannot be checked. Treat these rows as null results for body and guitar-type questions. [VERIFIED]
- The judge's own not_measured field says bodies and guitar types were measured on a low-E pluck only (judge_scores.json). [VERIFIED]

### S10. Scale-length table (no audible path; data-only)

- Table: Source/dsp/ModelTables.h:112 [HEAD]. Index 0 (low E) 0.686; index 4 0.628 (unexplained); index 6 0.686 (7-string low B by the comment at 110-111). Default field 0.648, StringModelParams.h:17.
- Reads: git grep for scaleLengthM and kScaleLengthM over Source, Tests, and Tools finds the declarations, one assignment at Source/PluginProcessor.cpp:201 [WT; HEAD 195], and one in Tests/main.cpp:65 [WT]. No reader. [VERIFIED, exit 0]
- Ranked as a data-only inconsistency, not an audible suspect.

## 5. Scout findings checked

| Scout claim | Version | Result | Evidence |
|---|---|---|---|
| Decay is single-slope, no second stage | HEAD and WT (same lines) | VERIFIED for the code | No second stage in tick (GuitarString.cpp:379-467). The solver fits two points, f0 and 6 f0 (LoopDesign.cpp:133-169). |
| Pianoteq decay is two-stage | n/a | NOT FOUND (no claim in the repo) | git grep for two-stage, two-slope, bi-exponential, double-slope over Source, Docs, Tools, Tests: exit 1, no matches (exit 1 means no match; errors exit 2 or more). The study says "Decay is still one slope" for Fretwork (line 37) and does not describe Pianoteq's shape. |
| Nylon upper partials 15-20 dB low | pre-fix reading | VERIFIED against the Pianoteq column only, stale for current code | Study line 15. Reading a, 10-13. S4. |
| 10 ms release ramp | HEAD | VERIFIED | GuitarString.cpp:344-345. Linear in gain, not dB. |
| Linear fractional-delay interpolation | HEAD | VERIFIED; conflicting comment | GuitarString.cpp:87-99 and 83-86. LoopDesign.cpp:276-278 says Catmull-Rom. |
| One-pole loop lowpass | HEAD | VERIFIED | GuitarString.cpp:395-401; LoopDesign.cpp:60-61, 133-169. |
| Four identical dispersion allpasses | HEAD | VERIFIED | GuitarString.cpp:403-418; LoopDesign.cpp:265-270; GuitarString.h:23. |
| Scale-length table conflicts with the comment (0.686 vs 0.648) | HEAD | VERIFIED conflict; no reader | ModelTables.h:112; StringModelParams.h:17. S10. |
| Baritone and 7-string have no physical difference (GuitarEngine.cpp:125-127) | WT | CHANGED | Baritone: no DSP difference (GuitarEngine.cpp:127, WT). 7-string: differs in tuning (PluginProcessor.cpp:30, WT, adds a low B at 35 Hz). Table index 6 (0.686) has no reader. |
| Fret-3 swell about 4 dB | our data | NOT REPRODUCED; gate metric not in repo | S8. |
| Pianoteq study dated 2026-10-14 | n/a | CHANGED | The main table is dated 2026-10-13 (line 1). The 10-14 date belongs to the follow-up section (line 36). |
| 2026-10-18 too_slow flag contradicts the study | n/a | CHANGED; both readings are provisional | Section 3. EVALUATION.md:95-96 marks the decay finding as unproven. |
| No human listening verdicts on record | n/a | VERIFIED | EVALUATION.md:31-45 has round rows with no verdict fields. The 2026-10-18 verdict is an AI review of features (EVALUATION.md:65-100). |

## 6. Other findings

- Judge rules conflict. GATE.md Gate 3 (lines 21-31) requires one image per render, the features text, a label, 16 renders, a pinned five-question rubric (lines 24-29), and a pass of at least 3.0 on every question with confidence at least 0.5 (line 30). The 2026-11-15 judge_scores.json run used features only, an ad hoc question, 13 scored renders (6 stock, 7 bodies), and two rules of its own: a 2.85 gate (line 3) and a 0.3 confidence floor (line 4). render/quality_20261115/GATE_addendum.md:5 says 2.85 is not used. The judge_scores.json run therefore does not satisfy Gate 3. No pass is claimed in this note.
- SoundJudge feature names do not match the code windows. The JSON keys say body_0_6s and tail_2_8_3_4s, but the code uses 0.10-0.60 s and 2.80-3.40 s (SoundJudge.cpp:80, 82). [VERIFIED]
- The divisor bug is in HEAD (SoundJudge.cpp:94 [HEAD]).
- The four single-note SoundJudge cases (SoundJudge.cpp:210-217) are in HEAD. The passages (neck_scale_low_E at 233, sequencer_pattern_low_E at 246, strum_open_E at 256, single_note_A2 at 259, symp_ring at 264) exist only in WT. Any baseline built from them depends on uncommitted code. [VERIFIED]
- StringModelParams.h:14 defaults sustain to 100 percent. The plugin default is 130 (Params.cpp:157 [HEAD]). Code that builds StringModelParams with defaults does not match the plugin. [VERIFIED]

- The SoundEvalTests.cpp gates are wired. CMakeLists.txt:104 globs Tests/*.cpp into FretworkTests, and CMakeLists.txt:132 runs it. Eight gates (lines 47-200). None asserts a dB/s rate. The held-decay gate (62-87) asserts continuity only.
- The gate's brightness uses windows that differ from SoundJudge's under the same name (SoundEvalTests.cpp:39, 126, 149; SoundJudge.cpp:137-138). Readings under one name are not comparable until one window is fixed.
- Docs/LISTENING.md, which the gate names (SoundEvalTests.cpp:9-11), does not exist (checked).

## 7. Experiments, in order

E2 comes first. It tests S1 directly from the loop, so it does not depend on the decay metric in S2.

- E2 (S1). Two builds, one gate. Build A is HEAD. Build B is a throwaway branch with only GuitarString.cpp:278 changed, from releaseGain_ = naturalGain_ to releaseGain_ = 1.f. Run the held gate (Tests/SoundEvalTests.cpp:62-87, noteOn at line 66, no noteOff) on both. Build A is already logged: fit -4.36 dB/s (test-baseline.log). Prediction: if S1 holds, B is about -4.36 + 2.71 = -1.65 dB/s; if not, B is about -4.36 dB/s. [INFERENCE: the chain is linear, so the change adds in dB.] Needs build approval. Release branch GuitarString.cpp:340-342 is not changed.
- E1 (S2). One window and one definition. Render low E A2 and A3 from the same MIDI at velocity 0.5. Compute the decay with the corrected SoundJudge divisor and with the ModeMatrix held-note window, on the same WAV. Pass: both agree within 1 dB/s.
- E3 (S5). Release sweep: releaseSec at 0.05, 0.2, 0.4, 2, and 20. Measure the decay rate after note-off. Predicted rate = 2.71 + 60 / releaseSec: about 1202, 302, 152.7, 32.7, and 2.71 dB/s (at 20 s the code uses naturalGain_, so the rate is the designed one). Compare.
- E4 (S3). Loop high-frequency loss. Impulse response of the loop at fractions 0, 0.25, 0.5, and 0.75 with the linear reader and with a fractional allpass or Lagrange reader. Measure the 5 kHz and 10 kHz levels after 10 passes. Predicted for the linear reader at f = 0.5: about -2.0 dB per pass at 10 kHz. Also settle LoopDesign.cpp:276-278 against the code.
- E5 (S6). Re-measure nylon velocity brightness and attack to peak on current HEAD, at velocities 40, 80, and 120, with the study's metric (study line 38) and the centroid metric (line 14). Compare with the 10-14 numbers and with Pianoteq (-29 to -19 dB).
- E6 (S4). Re-measure partials 6 to 8 at A3 for nylon and steel after the excitation fix. Then swap harmRatio 0.10 to 0.12 for nylon in a throwaway branch. Whichever change moves the partials by 10 dB or more is the main cause.
- E7 (S8). Find the IsoRender "swell gain dB" definition. It is not in the repo. If it cannot be found, record S8 as not reproducible and stop there.
- E8 (S9). Write a tracked case file that sets body index and guitar type explicitly. Render bodies 0 to 3 at one pluck. Check that the parameter values reach the render, then compare harmonics 2 to 8. Check Docs/PARTS_MODEL.md for each body's intended target.
- E9 (S7). Dispersion: current single coefficient against zero dispersion. Measure partial stretch at low E and high E. Pianoteq about -2 cents.
- E10 (S10). Null test: change kScaleLengthM in a throwaway branch and render. Expect bit-identical output.

## 8. Limits

- No build or render was run. Every magnitude comes from code reads, existing renders, and hand arithmetic, labelled [INFERENCE] where not measured.
- Not read in this pass: Source/dsp/BodyModel.cpp, OutputStage.cpp, PickupModel.cpp, and the PerformanceEngine articulation path (which decides the default Damping value, section 2).
- Render folders are untracked. These numbers cannot be reproduced from git alone.
