# Fretwork tone research and AI probe design

Date: 2026-11-22. Scope: docs only. No source, build, render, or test was changed or run.

Labels. [VERIFIED] means read in the cited file this session. [SEARCH] means a web_search snippet only; the full text was not read. [INFERENCE] means reasoning, arithmetic from a formula, or general knowledge, not measured in this repo. Version tags: [HEAD] is commit 84716ba. [WT] is the working tree as read today. Line numbers are [WT] unless tagged [HEAD].

Companions. Docs/RESEARCH_TONE_20261122.md holds the decay ledger and suspects S1 to S10. This note does not repeat it; it cites it where a new finding changes a suspect. Docs/AI_SOUND_TESTS_DESIGN.md holds the test inventory and build order. Section 4 here extends it.

Working tree note. git status shows 30+ modified source files, and Tests/ParamGolden.inc is modified [WT]. The AI design doc says that file must not change. See section 5.

## 1. Current signal path

The render path is GuitarEngine::render, Source/dsp/GuitarEngine.cpp:292-451. Order per block, with the file and line of each stage.

| # | Stage | File:line | What shapes tone | Version |
|---|---|---|---|---|
| 1 | Event dispatch | GuitarEngine.cpp:210-271 | Pluck, legato, release per string. Optional 12-string course at 229-236. | [WT] |
| 2 | Excitation | Excitation.cpp:16-124 | Triangle at pick position (default 15 percent, Params.cpp:159). Two-pole lowpass with cutoff fc = f0 * (6 + 34 * pickHard) * (0.12 + 0.88 * vel^2) (lines 56-86). Finger x0.35, Thumb x0.5, PalmMute x0.6 (58-61). Level 0.35 * vel^1.5 rms (106-111). | [WT] |
| 3 | Loop delay | GuitarString.cpp:392 | Fractional read with linear interpolation (readInterp, lines 87-99). Only caller is line 392. | [HEAD] |
| 4 | Loop lowpass | GuitarString.cpp:395-401 | One-pole, gain set so the loop gain at f0 equals the designed value. | [HEAD] |
| 5 | Dispersion | GuitarString.cpp:403-418 | Four identical first-order allpass stages. | [HEAD] |
| 6 | Release gain | GuitarString.cpp:420-432 | x *= releaseGain_ every sample. Target from GuitarString.cpp:340-342. | [HEAD] |
| 7 | Sympathetic gate | GuitarString.cpp:437-438 | Scales string-to-string drive by release state. | [WT only] |
| 8 | Bridge sum | GuitarEngine.cpp:356-404 | Sympathetic mix (couple = sympathetic * 0.004, line 354). Course output and string gain summed (385-388). | [WT] |
| 9a | Electric branch | GuitarEngine.cpp:139-140, 391-396; PickupModel.cpp:142-170 | Only for SolidElectric and SemiHollow (level 0.7). Comb taps from the bridge at 0.06, 0.12, 0.19 of scale length (ModelTables.h:85-87), RLC lowpass, tanh drive. | [HEAD] for PickupModel |
| 9b | Acoustic branch | GuitarEngine.cpp:408; BodyModel.cpp:78-87 | Piezo highpass 40 Hz (line 12). Parallel bandpass bank (kDreadnoughtModes etc., ModelTables.h:27-51). Top damp lowpass 14 kHz to 4 kHz (BodyModel.cpp:13, 72-73). Acoustic level from body mix dB (ModelTables.h:63-66), plus 2.5 dB makeup when no pickup (GuitarEngine.cpp:141-146). | [WT] |
| 10 | Acoustic blend | GuitarEngine.cpp:420-421 | (1 - mb) * 0.6 * piezo + mb * mic. Default Mic Blend 70 percent (Params.cpp:145). | [WT] |
| 11 | Stereo spread | GuitarEngine.cpp:426-431 | High part of string difference (about 500 Hz highpass) scaled by Width. | [WT] |
| 12 | Pick noise | GuitarEngine.cpp:435 | ArticulationNoise. | [WT] |
| 13 | Amp, cab, chorus, delay | AmpFx.cpp:166-190 | processAmp runs only if ampModel > 0 (line 185). Default Amp is Off (Params.cpp:184). Cabinet default is 2x12 (Params.cpp:190) but is inert while the amp is Off. | [WT] |
| 14 | Output stage | OutputStage.cpp:110-172 | One-pole tone lowpass on the mono sum (45-48, 143-146). Default Tone 2811 Hz (Params.cpp:175). Drive is pass-through at default 0 (122-128). Reverb if Room > 0 (149-151); default Room Level 20 (Params.cpp:178), wet = 0.6 * sqrt(room) * kWetScale[type] (line 65). Default Room Type is Room (Params.cpp:177), scale 1.0, so about 0.27. Output gain 10^(dB/20) * 0.631 (116-117). Soft clip (159-160). | [HEAD] for tone; [WT] for reverb wet |

At defaults the sound is: Dreadnought body (Params.cpp:123, index 0), so the pickup branch is off. Amp is off. Output path is stages 1 to 6, 8, 9b, 10, 11, 12, 14. Tone is shaped at five points: excitation (stage 2), loop lowpass and dispersion (4, 5), reader loss (3), body modes and top damp (9b), and the output tone lowpass (14). Stage 14 acts on every sound on every path.

Stale or conflicting comments found while reading.

- LoopDesign.cpp:276-278 says the delay line is read with four-point Catmull-Rom. The code uses linear (GuitarString.cpp:87-99). GuitarString.cpp:83-86 documents linear and says Catmull-Rom measured worse. [VERIFIED]
- GuitarEngine.cpp:160-162 says body mic output is fed back through the bridge. GuitarEngine.cpp:406-407 says no bridge-to-string return path exists. The code matches the second. [VERIFIED]
- Params.cpp:173-174 says 2811 Hz is the old default "as it actually sounded". It is an ear-tuned value, not a physical one. [VERIFIED]

## 2. Likely causes of the off sound

Ordered by how much each could change the sound times how much evidence supports it.

### A. Designed decay gain applied twice [HEAD, UNCONFIRMED until E2]

Mechanism, all [HEAD]. naturalGain_ = loopGainAt(f0, t60Fund) (GuitarString.cpp:277). releaseGain_ is set to the same value (278). tick multiplies by releaseGain_ every sample (432). The loop lowpass already realizes that gain at f0 (395-401), so each pass at f0 sees g0 twice. Per the prior note (section 2, S1), that doubles the designed decay from 2.71 to 5.41 dB/s [INFERENCE]. The only Fretwork decay reading in the repo that is not a formula is one features file: render/judge_baseline_20261115/features-standard_hard_low_E.json, decay_db_per_sec -4.89. Its body rms (0.04945) and tail rms (0.01280) give -11.74 dB. The code divides by 2.4 (SoundJudge.cpp:94, comment says "body centre to tail centre"). The actual window centres are 0.35 s (body 0.10 to 0.60) and 3.10 s (tail 2.80 to 3.40), a spacing of 2.75 s. Dividing the same dB difference by 2.75 gives -4.27 dB/s. So the code's divisor overstates the slope by 2.75/2.4, about 15 percent. That is a real bug in the metric, independent of the decay question. Two other figures in the earlier notes do not hold up as Fretwork readings: the -4.36 fit attributed to test-baseline.log is not in that log, and the -4.47 figure is a CC0 reference clip (build_vs/reference/reference_features.json), not a Fretwork render. Neither is used in this doc. The code path is verified. The doubling and the 5.41 dB/s figure are arithmetic from the formula, not a runtime result. Status: UNCONFIRMED until E2 (prior note section 7) runs on a throwaway branch.

### B. Output tone lowpass at 2811 Hz, always on [HEAD, new]

OutputStage.cpp:45-48 sets a one-pole coefficient 1 - exp(-2 pi fc / fs). Line 143 applies it to the mono sum of every sound. The stereo difference is kept (146). Default fc is 2811 Hz (Params.cpp:175).

Magnitude of that one-pole at 48 kHz, computed from the formula [INFERENCE, not measured]:

| Freq | 0.5 kHz | 1 kHz | 2 kHz | 2.8 kHz | 4 kHz | 6 kHz | 8 kHz | 10 kHz | 15 kHz |
|---|---|---|---|---|---|---|---|---|---|
| dB | -0.1 | -0.5 | -1.8 | -3.0 | -4.7 | -7.2 | -9.2 | -10.7 | -13.3 |

At the top of the Tone range (16 kHz) the loss at 10 kHz is under 1 dB [INFERENCE from formula]. So the default is the cause, not the range. The prior note's brightness gap (S6) may partly come from this. That is inference. It is also a deliberate, ear-chosen default, so changing it is a listening decision, not a bug fix.

### C. Body upper-mode Q cap defeats the decay floor above 1.1 kHz [WT only, new, UNCONFIRMED until a runtime qEff trace]

BodyModel.cpp:46-53 sets a t60 floor of 0.05 s and derives Q = min(0.05 * hz / 2.198, 25). The cap of 25 binds for any mode above 1.10 kHz. Comment at line 52 says the cap is there because a Q of 68 whistles. The cap is a deliberate trade, but it means the floor is not met for upper modes. This code is absent from HEAD. [VERIFIED by git show HEAD:Source/dsp/BodyModel.cpp, no match]

Dreadnought table (ModelTables.h:29-32), effective T60 after the cap. Values are from the formula t60 = 2.198 Q / f with the qEff rule copied from BodyModel.cpp:50-53, computed by hand, not by running the code. Status: UNCONFIRMED until a trace of qEff and the biquad impulse response from the built binary.

| Mode (Hz) | 1250 | 1700 | 2300 | 3100 | 4200 |
|---|---|---|---|---|---|
| T60 without floor (ms) | 17.6 | 10.3 | 7.6 | 4.3 | 2.6 |
| T60 with cap (ms) | 44.0 | 32.3 | 23.9 | 17.7 | 13.1 |
| Intended floor (ms) | 50 | 50 | 50 | 50 | 50 |

The Classical table (ModelTables.h:37-40) has the same problem above 1.1 kHz. The code comment (BodyModel.cpp:46-49) says a real top rings 40 to 80 ms at 3 kHz. The effect on the sound is an audible claim, so it is [INFERENCE] until a test.

### D. Linear fractional-delay loss, with no budget [HEAD, prior S3, reconfirmed]

GuitarString.cpp:392 uses the linear reader. Its two-tap magnitude at half a sample is |cos(w/2)|. [INFERENCE from the formula] That is -0.2 dB per pass at 3 kHz and -2.0 dB per pass at 10 kHz at 48 kHz. LoopDesign.cpp:276-278 budgets for Catmull-Rom, which is not what runs, so the loop delay and lowpass design do not include this loss. Upper partials lose more per period than designed, by an amount that depends on the note's fractional delay. [VERIFIED code; magnitude INFERENCE]

### E. Excitation spectrum at the attack [HEAD/WT, prior S6]

Default pickHard is 50 percent (Params.cpp:160). For low E (f0 = 82.41 Hz), velocity 0.8, the cutoff from Excitation.cpp:56 is about 1295 Hz [INFERENCE from the formula]. That two-pole lowpass sets most of the attack brightness. The prior note's brightness readings are pre-fix (prior S6). The Excitation wrap-continuity fix (Excitation.cpp:68-71) changed the spectrum after those readings. Current attack brightness is unmeasured. [VERIFIED code]

### F. Output reverb on by default [WT, new, low]

Room Level 20 with Room type (Params.cpp:177-178) gives wet 0.27 (OutputStage.cpp:65) and dry 1.0 (68). That colours the tail and can blur the attack. Audible effect is [INFERENCE]. Low rank.

### G. Sympathetic coupling on by default [WT, minor]

Sympathetic default 30 percent (Params.cpp:161). The code comment at GuitarEngine.cpp:352-353 says this adds about -2 dB/s at default. That is a comment, not a measurement. Minor.

### Ruled out at defaults

- Amp and cabinet: inert while Amp is Off (AmpFx.cpp:185; Params.cpp:184).
- Pickup branch: off on the default acoustic body (GuitarEngine.cpp:139-140).

### Carried from the prior note, not re-checked

S4 nylon partials (pre-fix readings), S7 dispersion (low), S8 fret-3 swell (not reproduced; gate metric absent from repo), S9 body routing (no tracked case), S10 scale-length table (no reader). See that note for the evidence.

## 3. Modeling techniques relevant to those causes

Source status. [SEARCH] means a web_search snippet only; the full text was not read. Claims below are limited to what the snippet supports, and anything beyond is marked [INFERENCE] from general knowledge.

- Loop-filter decay design. Jaffe and Smith, "Extensions of the Karplus-Strong Plucked-String Algorithm", Computer Music Journal, 1983. http://musicweb.ucsd.edu/~trsmyth/papers/KSExtensions.pdf [SEARCH: snippet describes the string as a high-order digital filter plus a short noise burst.] Relevant to A: one loop gain should set the decay, applied once. [INFERENCE for the mechanism]
- Fractional delay. Laakso, Valimaki, Karjalainen, Laine, "Splitting the Unit Delay", IEEE Signal Processing Magazine, 1996. https://ieeexplore.ieee.org/document/482137 [SEARCH: abstract covers fractional delay for music technology.] Aalto review of FIR versus allpass fractional delay: https://www.researchgate.net/publication/246232971_Fractional_Delay_Filters-Design_and_Applications [SEARCH: abstract compares FIR and allpass approaches.] Relevant to D: linear interpolation attenuates high frequencies per pass; allpass or Lagrange readers reduce that loss. [INFERENCE for the loss magnitude]
- Plucked-string models and body coupling. Karjalainen, Valimaki, Tolonen, "Plucked-String Models: From the Karplus-Strong Algorithm to Digital Waveguides and Beyond", Computer Music Journal, 1998. http://users.spa.aalto.fi/vpv/publications/cmj98.htm [SEARCH: abstract.] Columbia guitar synthesis page describing the body effect after the same group: https://www.ee.columbia.edu/~ronw/dsp/ [SEARCH: "accounts for the effect of the guitar body on the plucked string sound".] Relevant to C and E: body as a modal bank, excitation shaping at the pluck. [INFERENCE]
- Pick-position comb. A pluck at fraction beta of the scale length puts comb notches at multiples of f0 / beta. Excitation.cpp:32-34 already uses a triangle apex at the pick point, so this is in the code. [INFERENCE: general knowledge; source not read]
- Output tilt. A one-pole fixed at a Hz value tilts the whole sound. It costs about -0.5 dB at 1 kHz and under 0.1 dB at the low E fundamental (82 Hz) [INFERENCE from formula]. A shelf or a velocity-linked tilt is the usual alternative. [INFERENCE: general practice]

Mapping to causes.

- A: single loop gain applied once. Fix the release path, not the loop filter.
- B: replace or retune the fixed one-pole with a shelf, or tune it by a reference match (listening decision).
- C: separate decay floor from Q. Options are a damped resonator, or keeping Q and accepting a shorter floor. Choice is a listening decision.
- D: allpass or Lagrange reader, plus the loss in the loop budget.
- E: none needed beyond measuring current attack brightness.

## 4. AI-driven sound testing design

Extends Docs/AI_SOUND_TESTS_DESIGN.md. The probe measures properties and fails on the ones the code states it should have. It does not judge taste. Taste calls stay with listening (Docs/EVALUATION.md) and the pinned judge in render/quality_20261115/GATE.md (untracked).

Constraints. Tests/ParamGolden.inc is frozen (AI design doc section 2.9), so no new parameters. Stage probes therefore call the dsp classes directly, not the processor's parameters. End-to-end probes use the existing harness.

Hook points.

- P0, end to end: fwtest::Plugin in Tests/Harness.h (class at line 215). Note convention: noteOn(n) sends n + 12, so n is the pitch that sounds (Harness.h:247-250; the octave offset is fw::kWrittenOctave = 12 (Source/Contracts.h:11)). Existing gates in Tests/SoundEvalTests.cpp run inside FretworkTests (CMakeLists.txt:104 glob, :132 test). No new binary.
- P1, stage probes: new file Tests/StageProbeTests.cpp, picked up by the same glob (CMakeLists.txt:104, to be confirmed at build step). Instantiate fw::dsp::OutputStage, feed an impulse at 48 kHz in 480-sample blocks, measure response at fixed frequencies. Instantiate fw::dsp::BodyModel, measure each mode's decay from its impulse response.
- P2, decay self-check: compare held-note decay from the harness with the rate the loop design predicts. Flags candidate A without a build toggle. Needs the design values; accessor not confirmed [INFERENCE: check GuitarString for a design accessor before writing].

Probes and pass criteria. Pass criteria are initial. Set tolerances after the E experiments.

| Probe | Metric and window | Pass criterion | Expected today |
|---|---|---|---|
| P1a output tone | gain in dB at 1, 2, 4, 8, 10 kHz for toneHz 2811 | within 0.5 dB of the one-pole formula | pass (code matches formula) |
| P1b body modes | effective T60 per mode of the Dreadnought and Classical tables | at least 50 ms, or an exemption with a written reason | fail above 1.1 kHz (finding C) |
| P2 held decay | measured dB/s, body 0.10 to 0.60 s, tail 2.80 to 3.40 s, divisor = true centre spacing 2.75 s (not the 2.4 in SoundJudge.cpp:94) | within 1 dB/s of the designed rate | fail: designed 2.71, one-file reading -4.27 at the correct divisor (finding A or other) |
| P0 gates | existing SoundEvalTests | unchanged | unchanged |

P1a checks the code against its own formula. It does not check whether 2811 Hz sounds right. That needs a reference match.

Metric windows. Each metric states its window (AI design doc section 2.3). Fix one window per metric before the baseline. Decay divisor at SoundJudge.cpp:94 is 2.4, not the 2.75 s window spacing. Fix it before using P2.

Verdict and output. Use the schema fretwork.soundtest.verdict/1 from the AI design doc (section 2.4). Add stage metrics as keys. A null metric is status error, never pass.

Minimal build order. Adds to AI design doc section 2.8.

1. Run E2 from the prior note (a throwaway branch with releaseGain_ = 1.f). Decides A.
2. Run a reference-matched tone test (listening, not automated) before any change to the 2811 Hz default.
3. Add Tests/StageProbeTests.cpp with P1a and P1b, expected values from the formulas, report mode first (no failure), then gate once decided.
4. Add P2 after E1 fixes the decay window.
5. Only then wire the probes into the SoundTest verdict flow from the AI design doc.

Not in scope. No auto-tuning. No change to ParamGolden.inc. No change to the judge rubric. No default change without a listening record.

## 5. Open questions

1. Is 2811 Hz the final Tone default? It was ear-tuned (Params.cpp:173-174). Which reference should it be matched against?
2. For the body cap (finding C): keep the 25 cap and accept a shorter floor, or switch to a damped-resonator decay that does not change Q?
3. Should the release path keep the designed rate (prior note S5, E3)?
4. Which reference recording is approved for the match? The HANDOFF names Freesound 250147 (CC0). Is that the one?
5. Tests/ParamGolden.inc is modified in the working tree. Was that an approved contract change? The AI design doc and GATE.md say it must not change.
6. The LoopDesign comment (276-278) and the reader (GuitarString.cpp:87-99) disagree. Which one is the intent? The fix changes either the code or the comment.
7. Judge model id is not recorded in the 2026-11-15 run (AI design doc section 1.5). Approve a rerun under GATE.md Gate 3?

## Limits

- No build, render, test, or listening was run. Every magnitude comes from a formula or from a file read, labelled as such.
- Web sources are search snippets. Full text of each was not read.
- Working-tree line numbers are as read today. HEAD and working-tree differences are tagged where they matter.
- Render folders are untracked. Numbers that depend on them cannot be reproduced from git.
