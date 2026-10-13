# Fretwork articulations

## Reference model

The articulation set of **Ample Sound's Ample Guitar** (Ample Guitar M / T / LP
family), as its manual lists them: down/up strum, palm mute, mute (dead note),
slide, hammer-on, pull-off, bend, natural harmonic, vibrato, fret noise, string
(position-shift) squeak and pick attack, with legato articulations picked
automatically from overlapping MIDI. Ample is sample-based; Fretwork matches the
*set and triggering behaviour*, synthesised. Nothing below plays a sample.

## Algorithms (code review)

| Articulation | Model | Code |
|---|---|---|
| Pick attack | Excitation burst shaped by pick position comb + pick hardness / velocity low-pass, plus a short high-passed click. Harder velocity = brighter burst. | `dsp/Excitation.cpp` `renderExcitation` |
| Strum down / up | Per-string onsets offset in stroke order (spread from strum speed), plus `ArticulationNoise` scrape: white noise through a band-pass gliding 1.6 -> 3.4 kHz (down) or 3.4 -> 1.6 kHz (up), Q 1.2, 2 ms / stroke / 18 ms envelope, and a 3 ms 2.5 kHz-high-passed tick per string. | `engine/PerformanceEngine.cpp`, `dsp/ArticulationNoise.cpp` |
| Palm mute | Loop T60 cut (damping = PalmMute) and excitation low-passed; the string still pitches. | `dsp/LoopDesign.cpp` `computeT60*`, `Excitation.cpp` |
| Mute (dead note) | Damping = Dead: loop gain so low the string dies in tens of ms; burst is mostly the pick click. | same |
| Slide | Gliding legato: the delay-line length is swept from the held pitch to the new pitch (90 ms), with a squeak. | `GuitarString` legato glide |
| Hammer-on / pull-off | Legato event: delay length steps to the new pitch without a new pick burst; a small, dark finger-impact (hammer) or pluck (pull-off) excitation is injected. | `GuitarString` legato, `Excitation.cpp` |
| Bend | Pitch wheel scales the loop delay continuously (range = Wheel Range, default 2 semitones). | `GuitarEngine` pitch bend |
| Natural harmonic | String touched at its 1/n node vibrates in n segments: loop designed at exactly n x the open pitch; burst low-passed at 1.5 x the sounding pitch (3 one-pole passes), injected at -5 dB, loop dulled (finger drains upper partials). | `GuitarString::design`, `GuitarString::pluck` |
| Vibrato | Sinusoidal 5.5 Hz delay modulation, fading in after a note has been held 0.6 s (max of this and mod wheel). | `PerformanceEngine` / `GuitarEngine` |
| Fret noise | At note-off: band-limited noise burst (finger leaving the string) shaped by the Fret Noise amount. | `Excitation.cpp` fret-noise path |
| String squeak | Q-7 band-pass noise swept 1.2 <-> 3.8 kHz under a raised cosine, 40 ms + 12 ms per fret shifted, full level on wound strings, 0.25 on plain. | `dsp/ArticulationNoise.cpp` |

## Automatic detection (live path, Poly mode, no UI controls needed)

All rules run in `PerformanceEngine` on incoming MIDI, ahead of the voices that
produce the audio (the same path the host and the Standalone use).

- **Strum**: 3+ note-ons within 70 ms. Rising pitch order = down-stroke, falling = up.
  All within 1 ms = block chord (one scrape, no stroke direction).
  Tolerance: < 3 notes or a gap > 70 ms between consecutive note-ons never
  triggers (no false positive on two-note double stops or arpeggios slower
  than 70 ms/note); any 3+ notes inside 70 ms always trigger (no false negative
  for MIDI-keyboard strums, which land at 5-40 ms per string).
- **Hammer-on / pull-off**: new note on the string of a note held >= 30 ms, 1-4
  semitones up / down. Releasing the sounding note while another is held on
  that string pulls off to it.
- **Slide**: the same overlap at 5-12 semitones, or velocity >= 110 on a legato
  overlap.
- **Palm mute**: velocity 20-45. **Dead note**: velocity 1-19.
- **Natural harmonic**: velocity 127 on a pitch that is a 12th/19th/24th-fret
  harmonic of an open string.
- **Squeak**: on slides, and on picked notes 5+ frets from the previous note
  within 0.4 s.
- **Vibrato**: after 0.6 s with one or two notes held.
- **Bend**: pitch wheel. **Fret noise**: every note-off. **Pick attack**: every
  picked note, scaled by velocity.

The manual Damping / Transition / Harmonic controls and keyswitches still override.

## Verification

Tool: `ModeMatrix artic-vst3 <Fretwork.vst3> <outdir>` (hosts the VST3 and plays
the phrases) and `ModeMatrix artic-analyze <name> <capture.wav> <png>` (scores a
Standalone loopback capture). Phrases and pass criteria are fixed in
`Tools/ArticMatrix.h`; each phrase is 6 s of plain MIDI on plugin defaults.
Spectrograms: `Docs/articulations/` (`sa_` prefix = Standalone captures).

- **Code commit tested**: `614a13388ed6a34d7aff5d342da41b14837e8892`
- **Build date**: 2026-10-13 15:26 (Release, MSVC, Windows 11 x64)
- **VST3**: `C:\Program Files\Common Files\VST3\Fretwork.vst3` (SHA-256 `5AB062FA...E42A7`)
- **Standalone**: `build\Fretwork_artefacts\Release\Standalone\Fretwork.exe` (SHA-256 `FBE7A150...33942`), MIDI from `midisend` -> loopMIDI, audio captured by `loopcap` (WASAPI loopback, 48 kHz)
- **Tester**: Claude (omp coding agent), 2026-10-13
- **Unit suite at that commit**: 2431 checks, 0 failures

One criterion was changed after the first run: strum direction was first
judged by strict per-string arrival order, which failed on 0.1 ms ties because
each string rings up at its own rate. It is now the mean arrival of the high
three strings minus the low three (>= +10 ms down, <= -10 ms up). The strum
chord was also changed to E2 A2 D#3 G#3 C#4 F#4 so no string's pitch is a
low partial of another's. All other criteria are as first written.

### Checklist

| Articulation | Standalone Windows | VST3 Windows | Standalone macOS | VST3 macOS |
|---|---|---|---|---|
| Strum down | Pass: high strings arrive 24.0 ms after low; scrape flatness 0.297 vs 0.002 sustain | Pass: +24.0 ms; scrape 0.319 vs 0.002 | Untested [no Mac] | Untested [no Mac] |
| Strum up | Pass: high strings arrive 36.5 ms before low; scrape 0.221 vs 0.002 | Pass: -36.5 ms; scrape 0.193 vs 0.002 | Untested [no Mac] | Untested [no Mac] |
| Palm mute | Pass: -24.5 dB at +0.35 s vs -5.9 dB open | Pass: -24.4 vs -5.8 dB | Untested [no Mac] | Untested [no Mac] |
| Mute (dead) | Pass: -39.4 dB at +0.2 s vs -4.6 dB open | Pass: -41.2 vs -3.8 dB | Untested [no Mac] | Untested [no Mac] |
| Slide | Pass: 7 pitch frames between E3 and A3, lands 57.00 | Pass: 13 frames, lands 57.00 | Untested [no Mac] | Untested [no Mac] |
| Hammer-on | Pass: A3 -> 59.01, no glide, attack >4 kHz -14.3 dB vs pick | Pass: 59.01, -14.1 dB | Untested [no Mac] | Untested [no Mac] |
| Pull-off | Pass: B3 -> 56.92 still sounding, -17.6 dB vs pick | Pass: 56.90, -17.2 dB | Untested [no Mac] | Untested [no Mac] |
| Bend | Pass: 52.00 -> 54.08 (+208 c) -> 52.06 | Pass: 52.00 -> 54.05 -> 52.08 | Untested [no Mac] | Untested [no Mac] |
| Harmonic | Pass: 12th -0.5 c, 19th +0.1 c; partials 3+ -11.6 dB vs -4.2 fretted | Pass: -0.6 c, 0.0 c; -11.6 vs -3.9 dB | Untested [no Mac] | Untested [no Mac] |
| Vibrato | Pass: 0.2 c wobble before 0.6 s, 7.8 c after at 5.5 Hz | Pass: same | Untested [no Mac] | Untested [no Mac] |
| Fret noise | Pass: note-off 2.5-8 kHz burst +9.4 dB | Pass: +14.9 dB | Untested [no Mac] | Untested [no Mac] |
| String squeak | Pass: quick shift flatness 0.507 vs 0.118, +4.6 dB | Pass: 0.515 vs 0.104, +2.9 dB | Untested [no Mac] | Untested [no Mac] |
| Pick attack | Pass: >4 kHz first 8 ms +13.9 / +13.3 dB over sustain; vel 120 8.2 dB brighter | Pass: +14.3 / +12.7 dB; 8.8 dB brighter | Untested [no Mac] | Untested [no Mac] |

macOS cells: no Mac machine is available. They are not inferred from the
macOS CI workflow, unit tests or auval.
