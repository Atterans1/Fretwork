# PluginE2E

Learns a real recording from its audio plus an independent reference, plays the
learned notes through **any VST3 instrument**, and checks the playback against
the recording.

```
PluginE2E --plugin X.vst3 --audio song.mp3 (--tab song.txt | --midi ref.mid) --out dir [options]
  --tuning "E2 A2 D3 G3 B3 E4"   open strings low to high (default: from the tab's string count)
  --param "Name=value"           plugin parameter by name, value in the plugin's own text (repeatable)
  --program N                    select program N first
  --velocity 0..1                note-on velocity (default 0.8)
  --pitch-tol SEMITONES          default 1
  --onset-ms MS                  default 50
  --min-rate 0..1                share of notes that must pass pitch and onset (default 0.8)
PluginE2E --plugin X.vst3 --list-params
```

Exit code: 0 pass, 1 fail, 2 bad input. Build: `cmake --build build --config Release --target PluginE2E`.

## Pipeline

1. **Decode**: MP3/WAV/FLAC through JUCE's decoders, mono mix, 4th-order low-pass, Lagrange resample to 22.05 kHz.
2. **Reference**: ASCII tab (any string count; 4/5 = bass, 6 = guitar, 7 = 7-string, or `--tuning`; slides `/ \`, `p`, `h` are kept as notes) or a MIDI file (pitch order only; timing always comes from the audio).
3. **Learn (joint)**: the reference pitches are aligned to the audio in one dynamic programme. Each note scores pitch-specific salience rise, pitch dominance and broadband onset at its start; consecutive notes are 50 ms to 1.5 s apart; a note can be skipped at a fixed penalty (the audio vetoes it). The audio picks the transpose (best of -12..+12) and the tuning reference, refines each onset on a 3 ms grid, sets durations, and can move a note one semitone when the neighbour is clearly what sounds.
4. **Play**: learned notes to `learned.mid` and through the plugin via JUCE's VST3 host at 48 kHz.
5. **Verify (independent methods)**: HFC onsets on both signals, YIN on the render, harmonic product spectrum (MIDI 48-84) on the source. A note passes pitch if render and source agree within the tolerance, onset if the detected onsets agree within the tolerance.

Outputs in `--out`: `report.md` (per-note table), `learned.mid`, `learned_render.wav`, `ab_source_left_render_right.wav`, `pianoroll.png` (source spectrum by semitone, learned notes green/red, verifier source pitch as cyan dots).

All analysis constants were fixed before the first real run. Do not tune them to make one song pass.

## Limits

- Instruments only (MIDI in, audio out). Effects are loaded but warned about.
- The source pitch check reads one predominant pitch. In chord-melody or band recordings it often lands on a chord tone, so a correct melody note can be scored as a pitch miss. Prefer monophonic or lightly accompanied recordings for a clean gate.
- Source durations are tracked through that same predominant pitch, so duration numbers in polyphonic material are not meaningful (reported, not gated).
- Legato notes (slides, pull-offs) have weak attacks and may not register as source onsets.

## First real run: Sparkle (guitar cover)

| | |
|---|---|
| Recording | YouTube `IyygSKsxmew`, "Tatsuro Yamashita - Sparkle (guitar cover with tabs & chords)", downloaded with yt-dlp as MP3 (48 kHz stereo, 247.2 s), SHA-256 `ef053f5cf83eb6f062c370da74fd95f1d4cb3e485c2e57cc626a66422acea946`. Not stored in the repo (copyrighted cover). |
| Tab | https://www.guitartabsexplorer.com/tatsuro-yamashita/sparkle-tab (intro, 38 notes; transcribed by ear from Tomo Fujita's tutorial, not from this cover), copied verbatim, SHA-256 `5a193addbeb848b84878a5587288ae52183f4cfbd0a3538d3acaad339713b5ad` |
| Plugin | Fretwork 1.0.0 VST3, default parameters |
| Command | `PluginE2E --plugin "C:/Program Files/Common Files/VST3/Fretwork.vst3" --audio sparkle_cover.mp3 --tab sparkle_intro.tab.txt --out final` |
| Learned | transpose 0, tuning +1.6 cents, 38/38 notes placed at 70.7-93.0 s, none vetoed or corrected |
| Result | **FAIL**: pitch 60.5 % (23/38), onset 57.9 % (22/38); matched onsets mean +16.4 ms, mean abs 17.6 ms; 1 octave error |

Where it diverges, from the per-note table and piano roll:

- **When it matches, it matches closely.** 23 notes agree with the recording within 0.25 semitone (most within 0.1), and matched onsets are within 30 ms, apart from three at 124-144 ms.
- **13 pitch misses are mostly chord tones, not melody errors.** On the misses the source reading is G#4, B3, B4, C#4 or C4 where the tab and the render have E4/C#4. These are mostly tones of the chords the cover plays under the melody (the uploader says the phrases are partly re-arranged). The verifier's single-pitch reading cannot separate melody from chord in this mix.
- **12 onset misses are mostly undetected source attacks.** The source onset detector found no attack within 150 ms for those notes, including the slide and pull-off notes 36-37.
- **Durations are not meaningful here.** The source duration tracker loses the melody after about 100 ms in the chordal mix, while learned durations run to the next note.
- **Choosing the octave was close.** Transpose 0 beat -12 by 260.6 to 259.5. The source readings on the matched notes confirm 0 was correct.

The same notes played with `--param "Body=Solid Electric"` scored 65.8 % / 65.8 %. The render's timbre affects detection, so record the parameters with any result.

The A/B file and piano roll are the listening comparison. The numbers above are measurements, not a listening test.
