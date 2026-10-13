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
- Durations are reported, not gated. The render's length is note-off plus the instrument's own release tail; the source's length is how long its pitch stays readable as the plucked note decays. The two do not agree within 50 ms on plucked material even when the notes are right.
- "Heard in mix" (diagnostic, never gated): the played pitch is among the 4 strongest semitones of the source and stronger than both neighbours. In chordal recordings this separates "wrong note" from "right note under a chord".
- Legato notes (slides, pull-offs) have weak attacks and may not register as source onsets.

## Real runs

Both through Fretwork 1.0.0 VST3 at default parameters. Recordings and tabs live in `C:/Users/jeffr/FretworkE2E` (not in the repo; the recordings are copyrighted).

| | Ode to Joy (solo melody) | Sparkle (chord-melody cover) |
|---|---|---|
| Recording | YouTube `vVQB0w7SEn4`, "Ode to joy (Beethoven) EASY single guitar melody tutorial" (Henri's guitar school), 40.5 s, SHA-256 `30ddc09a...` | YouTube `IyygSKsxmew`, "Tatsuro Yamashita - Sparkle (guitar cover with tabs & chords)", 247.2 s, SHA-256 `ef053f5c...` |
| Tab | guitartabcreator.com/tabs/briangreen335/ode-joy-beethoven (ukulele melody in F, 62 notes, copied verbatim, SHA-256 `e027996a...`) | guitartabsexplorer.com/tatsuro-yamashita/sparkle-tab (intro, 38 notes, transcribed from Tomo Fujita's tutorial, SHA-256 `5a193add...`) |
| Extra args | `--tuning "G4 C4 E4 A4"` | none |
| Learned | transpose -5 (F to C, chosen by the audio), tuning +1.7 cents, 62/62 notes | transpose 0, tuning +1.6 cents, 38/38 notes at 70.7-93.0 s |
| Pitch within 1 semitone | **100 %** (62/62) | 55.3 % |
| Onset within 50 ms | **100 %** (mean abs 2.7 ms, max 6.6 ms) | 57.9 % (matched: mean abs 17.6 ms, max 143.9 ms) |
| Heard in mix (diagnostic) | 100 % | 84.2 % |
| Octave errors | 0 | 3 |
| Verdict | **PASS** | **FAIL** |

Command pattern: `PluginE2E --plugin "C:/Program Files/Common Files/VST3/Fretwork.vst3" --audio <mp3> --tab <tab> [--tuning ...] --out <dir>`.

### Verifier change made after the first Ode to Joy run

The first Ode to Joy run failed pitch at 48.4 %: 32 notes were exactly 12 semitones off. The verifier read every E, F and G as E5/F5/G5, while D4 and C4 matched exactly. Two things show the verifier was wrong:

- The melody those readings imply (E5 D4 C4 D4 E5) leaps a ninth up and down inside a stepwise tune.
- The heard-in-mix check found the played E4/F4/G4 sounding in the recording on every note.

The cause is the known weak point of a harmonic product spectrum: on plain strings it can pick the 2nd harmonic. I added the standard sub-octave check: take f/2 when a real peak of at least 20 % of the chosen peak's amplitude sits there. The check never looks at the learned notes, and it applies to every run. It moved Sparkle from 60.5 % to 55.3 % pitch, so it did not favour either song.

### Why Sparkle fails

- The cover is chord-melody, and the uploader says the phrases are partly re-arranged. The verifier's source pitch reading often lands on a chord tone (G#4, B3, B4 against the tab's E4/C#4). 84 % of the played notes are still sounding in the mix, so most pitch misses are right notes under a chord.
- 12 source attacks were never detected, including the slide and pull-off notes 36-37.
- Choosing between transpose 0 and -12 was close (260.6 vs 259.5).

The piano roll (`pianoroll.png`) and A/B file (`ab_source_left_render_right.wav`) in each output folder are the listening comparison. The numbers here are measurements, not a listening test.
