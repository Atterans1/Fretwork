# Isolated per-note render: low E (string 0) and high E (string 5)

Harness: `IsoRender.cpp` (CMake target `IsoRender` in this directory, built Release with the existing `build_upscale_fix` tree). Each fret 0..24 is plucked once at sample 0 in a fresh `GuitarEngine`, rendered for 1.5 s at 48 kHz, 480-sample blocks. Engine setup is the same as ScaleRender's defaults: body 0, mic 70, room 12, stiffness 100, age 20, velocity 0.5. Analysis signal is mid = 0.5 (L+R). No onset grid exists, so nothing here is circular.

Raw outputs (all in this directory): `iso_lowE.csv`, `iso_highE.csv`, `iso_lowE_v08.csv` (low E at velocity 0.8), the matching `*_env.csv` (2 ms RMS in dBFS at 1 ms steps, first 400 ms, one column per fret) and `*.wav` (25 notes x 1.5 s, concatenated stereo).

## Definitions

- **Envelope.** 2 ms trailing RMS of mid. Onset N% is the first sample where it reaches N% of the note's own envelope peak. Values are in ms from the pluck event.
- **Period.** 1/f0 with f0 = 440 * 2^((MIDI-69)/12). Onset10/period divides the 10% onset by that period.
- **Smooth envelope.** RMS over two fundamental periods, used only for classification and the "smooth peak". It removes within-cycle ripple. Attack level is its maximum in the 25 ms after its 1% onset. Swell gain is the smooth peak minus the attack level in dB.
- **Classes.** `late_onset`: onset10 above 2.5 periods. `swell`: smooth peak more than 25 ms after the smooth 1% onset and swell gain at least 3 dB. `slow_rise`: smooth peak more than 30 ms after the smooth 10% onset but gain under 3 dB. Otherwise `normal`. The thresholds are my choices, not derived from the engine. **The 3 dB and 25/30 ms values were set after I had seen fret 3's numbers, so the class labels are partly circular.** Treat the continuous `swell gain dB` column as the measurement and the class column as a convenience label. An earlier classifier (first run, 70% dip rule on the 2 ms envelope) labelled almost every low-E fret "swell" because the 2 ms RMS ripples about 10 dB within each cycle of a low string (fret 0, 4, 14 and 3 all show it, e.g. fret 3 goes -26.6 dB at 13 ms to -36.8 dB at 15 ms). That version was discarded; its CSVs were overwritten and are not kept.
- **Tail.** RMS of mid over 0.7-0.9 s in dBFS, and relative to the envelope peak.

## Low E, string 0 (velocity 0.5)

| fret | MIDI | f0 Hz | onset 1% ms | onset 10% ms | onset 50% ms | onset10 / period | env peak ms (2 ms RMS) | smooth peak ms | abs peak dBFS | tail dBFS (0.7-0.9 s) | tail - peak dB | swell gain dB | class | prior steepest-rise delay ms |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| 0 | 40 | 82.4 | 11.96 | 12.08 | 13.83 | 1.00 | 27.00 | 38.29 | -21.8 | -40.6 | -17.3 | 0.3 | normal |  |
| 1 | 41 | 87.3 | 11.29 | 11.42 | 13.33 | 1.00 | 36.90 | 47.17 | -20.9 | -38.9 | -16.6 | 0.6 | slow_rise | +35 |
| 2 | 42 | 92.5 | 10.65 | 10.81 | 13.69 | 1.00 | 56.71 | 77.06 | -19.0 | -33.9 | -13.4 | 1.7 | slow_rise | +88 |
| 3 | 43 | 98.0 | 10.04 | 10.21 | 22.42 | 1.00 | 176.90 | 175.62 | -19.5 | -28.2 | -8.1 | 4.1 | swell | +195 |
| 4 | 44 | 103.8 | 9.46 | 9.58 | 11.08 | 1.00 | 22.42 | 49.00 | -21.9 | -37.6 | -14.2 | 0.8 | slow_rise | +40 |
| 5 | 45 | 110.0 | 8.94 | 9.04 | 10.48 | 0.99 | 21.21 | 37.04 | -21.8 | -41.5 | -17.6 | 0.3 | normal |  |
| 6 | 46 | 116.5 | 8.42 | 8.54 | 9.96 | 1.00 | 19.98 | 34.92 | -21.9 | -44.9 | -21.1 | 0.0 | normal |  |
| 7 | 47 | 123.5 | 7.94 | 8.06 | 9.42 | 1.00 | 18.92 | 24.29 | -21.4 | -44.3 | -20.4 | 0.0 | normal |  |
| 8 | 48 | 130.8 | 7.48 | 7.60 | 8.92 | 0.99 | 17.94 | 23.00 | -21.2 | -42.2 | -18.2 | 0.0 | normal |  |
| 9 | 49 | 138.6 | 7.06 | 7.17 | 8.42 | 0.99 | 17.08 | 21.62 | -21.3 | -38.3 | -14.1 | 0.0 | normal |  |
| 10 | 50 | 146.8 | 6.65 | 6.75 | 7.94 | 0.99 | 16.25 | 20.35 | -22.1 | -43.7 | -18.9 | 0.0 | normal |  |
| 11 | 51 | 155.6 | 6.27 | 6.38 | 7.48 | 0.99 | 15.40 | 19.21 | -23.0 | -44.0 | -18.6 | 0.0 | normal |  |
| 12 | 52 | 164.8 | 5.90 | 6.00 | 7.08 | 0.99 | 14.65 | 22.52 | -22.3 | -42.7 | -17.2 | 0.0 | normal | +25 |
| 13 | 53 | 174.6 | 5.56 | 5.67 | 6.75 | 0.99 | 13.23 | 29.88 | -21.1 | -40.4 | -15.3 | 0.0 | normal | +35 |
| 14 | 54 | 185.0 | 5.25 | 5.35 | 6.54 | 0.99 | 50.71 | 57.71 | -20.7 | -34.5 | -10.4 | 0.7 | slow_rise |  |
| 15 | 55 | 196.0 | 4.94 | 5.06 | 6.19 | 0.99 | 26.94 | 41.23 | -21.5 | -36.0 | -11.6 | 0.4 | slow_rise | +40 |
| 16 | 56 | 207.7 | 4.65 | 4.77 | 5.79 | 0.99 | 20.96 | 21.65 | -22.3 | -43.3 | -18.1 | 0.0 | normal |  |
| 17 | 57 | 220.0 | 4.38 | 4.48 | 5.42 | 0.99 | 19.79 | 20.44 | -23.0 | -45.1 | -19.0 | 0.0 | normal |  |
| 18 | 58 | 233.1 | 4.12 | 4.23 | 5.17 | 0.99 | 9.94 | 15.10 | -22.9 | -42.0 | -16.1 | 0.0 | normal |  |
| 19 | 59 | 246.9 | 3.90 | 4.00 | 4.94 | 0.99 | 13.42 | 14.19 | -22.3 | -42.0 | -16.4 | 0.0 | normal |  |
| 20 | 60 | 261.6 | 3.67 | 3.77 | 4.77 | 0.99 | 20.38 | 30.31 | -21.0 | -38.8 | -13.7 | 0.1 | normal | +58 |
| 21 | 61 | 277.2 | 3.44 | 3.56 | 4.60 | 0.99 | 30.17 | 41.44 | -21.5 | -33.0 | -8.2 | 0.3 | slow_rise |  |
| 22 | 62 | 293.7 | 3.25 | 3.33 | 4.23 | 0.98 | 18.25 | 17.29 | -23.6 | -40.9 | -14.9 | 0.0 | normal |  |
| 23 | 63 | 311.1 | 3.04 | 3.15 | 3.96 | 0.98 | 7.54 | 17.83 | -23.8 | -42.6 | -15.7 | 0.0 | normal |  |
| 24 | 64 | 329.6 | 2.88 | 2.96 | 3.77 | 0.98 | 10.10 | 10.00 | -23.1 | -44.7 | -18.0 | 0.0 | normal |  |

## High E, string 5 (velocity 0.5)

| fret | MIDI | f0 Hz | onset 1% ms | onset 10% ms | onset 50% ms | onset10 / period | env peak ms (2 ms RMS) | smooth peak ms | abs peak dBFS | tail dBFS (0.7-0.9 s) | tail - peak dB | swell gain dB | class |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| 0 | 64 | 329.6 | 2.88 | 2.96 | 3.77 | 0.98 | 10.10 | 10.00 | -23.1 | -50.3 | -23.5 | 0.0 | normal |
| 1 | 65 | 349.2 | 2.69 | 2.79 | 3.60 | 0.97 | 9.75 | 12.31 | -23.2 | -45.0 | -18.3 | 0.0 | normal |
| 2 | 66 | 370.0 | 2.54 | 2.62 | 3.42 | 0.97 | 11.94 | 16.38 | -22.9 | -47.2 | -20.2 | 0.0 | normal |
| 3 | 67 | 392.0 | 2.38 | 2.50 | 4.31 | 0.98 | 29.04 | 30.17 | -22.5 | -41.2 | -15.9 | 0.2 | normal |
| 4 | 68 | 415.3 | 2.23 | 2.35 | 4.02 | 0.98 | 13.15 | 21.83 | -21.9 | -46.7 | -20.9 | 0.0 | normal |
| 5 | 69 | 440.0 | 2.10 | 2.19 | 2.92 | 0.96 | 7.94 | 11.52 | -24.5 | -52.6 | -24.7 | 0.0 | normal |
| 6 | 70 | 466.2 | 1.98 | 2.06 | 2.77 | 0.96 | 9.65 | 9.35 | -24.3 | -51.7 | -23.6 | 0.0 | normal |
| 7 | 71 | 493.9 | 1.85 | 1.94 | 2.81 | 0.96 | 11.17 | 11.35 | -22.6 | -48.4 | -21.0 | 0.0 | normal |
| 8 | 72 | 523.3 | 1.73 | 1.83 | 3.44 | 0.96 | 20.92 | 14.38 | -22.6 | -41.6 | -15.2 | 0.0 | normal |
| 9 | 73 | 554.4 | 1.62 | 1.73 | 3.10 | 0.96 | 10.77 | 11.90 | -22.9 | -49.9 | -22.5 | 0.0 | normal |
| 10 | 74 | 587.3 | 1.52 | 1.60 | 2.29 | 0.94 | 5.08 | 6.02 | -25.2 | -54.2 | -25.2 | 0.0 | normal |
| 11 | 75 | 622.3 | 1.42 | 1.50 | 2.29 | 0.93 | 6.42 | 6.96 | -24.6 | -51.4 | -22.6 | 0.0 | normal |
| 12 | 76 | 659.3 | 1.33 | 1.44 | 2.77 | 0.95 | 11.23 | 11.62 | -24.2 | -50.2 | -22.7 | 0.0 | normal |
| 13 | 77 | 698.5 | 1.25 | 1.38 | 2.79 | 0.96 | 25.79 | 19.42 | -22.3 | -44.5 | -18.3 | 0.0 | normal |
| 14 | 78 | 740.0 | 1.17 | 1.25 | 2.40 | 0.92 | 8.46 | 8.92 | -23.6 | -54.1 | -25.1 | 0.0 | normal |
| 15 | 79 | 784.0 | 1.08 | 1.15 | 2.06 | 0.90 | 4.19 | 11.06 | -25.1 | -59.1 | -28.7 | 0.0 | normal |
| 16 | 80 | 830.6 | 1.00 | 1.06 | 2.00 | 0.88 | 11.23 | 10.71 | -24.7 | -54.7 | -24.1 | 0.0 | normal |
| 17 | 81 | 880.0 | 0.92 | 1.00 | 1.98 | 0.88 | 8.40 | 8.46 | -26.2 | -66.1 | -35.5 | 0.0 | normal |
| 18 | 82 | 932.3 | 0.85 | 0.98 | 2.04 | 0.91 | 15.42 | 15.29 | -23.9 | -55.1 | -26.8 | 0.0 | normal |
| 19 | 83 | 987.8 | 0.79 | 0.90 | 1.83 | 0.88 | 12.15 | 12.17 | -25.2 | -58.5 | -28.7 | 0.0 | normal |
| 20 | 84 | 1046.5 | 0.73 | 0.79 | 1.62 | 0.83 | 4.60 | 4.60 | -26.9 | -66.2 | -34.1 | 0.0 | normal |
| 21 | 85 | 1108.7 | 0.67 | 0.73 | 1.58 | 0.81 | 10.65 | 10.65 | -26.1 | -63.9 | -32.4 | 0.0 | normal |
| 22 | 86 | 1174.7 | 0.60 | 0.69 | 1.46 | 0.81 | 7.54 | 7.54 | -27.6 | -83.3 | -51.1 | 0.0 | normal |
| 23 | 87 | 1244.5 | 0.54 | 0.75 | 2.17 | 0.93 | 22.71 | 22.71 | -24.4 | -77.7 | -48.7 | 0.0 | normal |
| 24 | 88 | 1318.5 | 0.50 | 0.56 | 1.27 | 0.74 | 7.38 | 7.38 | -27.6 | -92.1 | -59.5 | 0.0 | normal |

## Findings (measured)

- **No late onsets on either string.** No fret is classed `late_onset`. Low E onset (10%) runs 2.96 to 12.08 ms and high E 0.56 to 2.96 ms. Both strings reach 10% at 0.98 to 1.00 and 0.74 to 0.98 fundamental periods respectively. Every isolated note starts sounding at about one period after the pluck (measured ratio; [INFERENCE] that it reflects a string-loop propagation delay is a guess, not tested), and the first sample above 1% of the sample peak (not tabulated; in the CSV as `firstabs1_ms`) agrees with the 1% envelope onset to within about 0.1 ms.
- **The low-E delays of +25 to +195 ms are not reproduced as late onsets in isolation.** No isolated low-E fret starts later than 12.1 ms. That says nothing about what the 25-note grid run does, and this test does not explain the grid-based delays. The prior agent's steepest-rise delays do line up with the late envelope maxima in these renders: fret 3 peaks at 176.9 ms (prior +195), fret 2 at 56.7 ms (prior +88), fret 1 at 36.9 ms (prior +35), fret 14 at 50.7 ms. The match is loose for the others (fret 4 prior +40 vs 22.4 ms, fret 12 prior +25 vs 14.7 ms, fret 13 prior +35 vs 13.2 ms, fret 20 prior +58 vs 20.4 ms). [INFERENCE] The prior metric may have been following amplitude changes inside the note's own envelope. Only fret 3 has a measured swell (+4.1 dB). The rest of the earlier delays remain unresolved in isolation, and carry-over from neighbouring notes was not tested.
- **Low-E swell classes.** Class `swell`: frets 3. Class `slow_rise`: frets 1, 2, 4, 14, 15, 21. Fret 3 is the only real swell: the smooth envelope grows 4.1 dB above its attack level and peaks at 175.62 ms; its tail is also the highest of the string (-8.1 dB below peak at 0.7-0.9 s versus about -17 dB for fret 0). The slow_rise frets gain under 3 dB (fret 2 is the largest at 1.7 dB; the rest are 0.6, 0.8, 0.7, 0.4, 0.3 dB).
- **High E.** All 25 frets are `normal`. The largest smooth peaks are late for fret 3 (30.17 ms), fret 23 (22.71 ms) and fret 13 (19.42 ms), but swell gain is at most 0.2 dB. The prior outliers at frets 3, 4, 5 do not reproduce as late onsets (onset10 = 2.50, 2.35, 2.19 ms).
- **Velocity.** Low E at velocity 0.8 (`iso_lowE_v08.csv`) gives the same onsets and the same swell/slow_rise frets; peak level is about 6 dB higher (fret 0 abs peak -15.9 dBFS versus -21.8). Swell gain at fret 3 is 4.1 dB in both runs.
- **High-E tail.** Tail level falls from -23.5 dB below peak at fret 0 to -59.5 dB at fret 24, so upper frets are almost silent by 0.7-0.9 s. Measured, not judged.

## Verdict on the low-E delays

- **Late onsets: not present in isolated notes** for string 0 and string 5 under these default args. This does not rule out late onsets or delays in the 25-note grid render.
- **Secondary swell: established for low E fret 3 only** (+4.1 dB, smooth peak 175.62 ms). Frets 1, 2, 4, 14, 15 and 21 show a smaller late rise of under 3 dB; whether a listener would hear that as a swell is undetermined.
- **Cause of the fret 3 swell: undetermined.** The harness does not say whether it comes from the string model, body or room (these were not varied), or from a string/pickup resonance near that pitch. [INFERENCE, untested] A resonance near MIDI 43 (about 98 Hz) is plausible, but nothing here tests it. Varying body, mic or room for fret 3 alone would separate those.

## What is not established

- Isolation proves the onset delays are not caused by carry-over from earlier notes in the 25-note run. It does not show what the 25-note run itself does at the grid: that render was not re-analysed here, because the previous note's tail would still be sounding at every scheduled onset.
- Only string 0 and string 5, one parameter set (body 0, mic 70, room 12, stiffness 100, age 20), velocities 0.5 and 0.8, one pluck type. Other strings, dynamics and settings were not rendered.
- Repeatability: one more full low-E render at velocity 0.5 (same binary, same args) produced a byte-identical CSV. That is one repeat on one string, not a variance study; other strings, parameters and a fresh process per run were not checked beyond this.
- The 3 dB and 2.5 period thresholds are arbitrary classification choices. The onset and peak numbers do not depend on them.
- The table's "prior steepest-rise delay" column is copied from the previous agent's reported figures; I did not re-run that analysis.

## Housekeeping: JUCE file count

`Fretwork/JUCE` holds **4,377 working-tree files** (`find` excluding `.git`, equal to `git ls-files | wc -l` in that repo) and **25 files under `JUCE/.git`**, so a plain `find . -type f` gives **4,402**. The earlier 4,402 versus 4,377 difference is explained by whether `.git` is included in the count. Nothing was changed. `git status --porcelain --ignored` in JUCE is empty (no untracked, modified or ignored files).

## Provenance of the engine under test

IsoRender was configured at 17:57 and built after that. The newest modification in `Source/` is 2026-11-15 11:47 (`dsp/LoopDesign.cpp/.h`) and the root `CMakeLists.txt` was last modified 2026-11-14 20:59, so no `Source/` file or root CMake file changed during this task. The working tree has many uncommitted `Source/` modifications that predate it. The existing ScaleRender executable was built at 14:10, after the last `Source/` change, so IsoRender and that ScaleRender build were compiled from the same engine source. [INFERENCE] from file mtimes only; I did not diff or hash the sources.
