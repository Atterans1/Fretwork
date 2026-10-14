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
| Pick attack | Excitation = triangle displacement with its apex at the pick position, plus 6 % noise; two-pole low-pass at f0 x (6 + 34 x hardness) x (0.12 + 0.88 vel^2) (finger x0.35, thumb x0.5); RMS normalised to 0.35 vel^1.5; plus a 1.5 ms pick click (0.03 x hardness x vel, high-passed at 2.5-6 kHz by velocity). | `dsp/Excitation.cpp` `renderExcitation`, `GuitarString::pluck` |
| Strum down / up | Per-string onsets offset in stroke order (spread from strum speed), plus `ArticulationNoise` scrape: white noise through a band-pass gliding 1.6 -> 3.4 kHz (down) or 3.4 -> 1.6 kHz (up), Q 1.2, 2 ms / stroke / 18 ms envelope, and a 3 ms 2.5 kHz-high-passed tick per string. | `engine/PerformanceEngine.cpp`, `dsp/ArticulationNoise.cpp` |
| Palm mute | Damping = PalmMute: loop T60 x 0.22 and excitation cutoff x 0.6; the string still pitches. | `dsp/LoopDesign.cpp` `computeT60*`, `Excitation.cpp` |
| Mute (dead note) | Damping = Dead: loop T60 cut so the string dies in tens of ms, plus a body thump (0.25 x vel) instead of a tone. | same |
| Slide | Gliding legato: the string's pitch offset ramps linearly from the held pitch to the new one over Glide Time (default 90 ms), with a squeak and fret burst. | `GuitarString::legato` |
| Hammer-on / pull-off | Legato event: pitch steps to the new note in 3 ms without a pick; a finger-type excitation (soft 6 ms attack, dark) is injected at 0.35 x vel (hammer) or 0.25 x vel (pull-off), plus an 8 ms 4 kHz fret burst. | `GuitarString::legato` |
| Bend | Pitch wheel x Wheel Range (default 2 semitones) sets each string's bend offset, which retunes the loop delay continuously. | `GuitarEngine::setPitchBend`, `GuitarString::setBendSemis` |
| Natural harmonic | String touched at its 1/n node vibrates in n segments: loop designed at exactly n x the open pitch; burst low-passed at 1.5 x the sounding pitch (3 one-pole passes), injected at -5 dB, loop dulled (finger drains upper partials). | `GuitarString::design`, `GuitarString::pluck` |
| Vibrato | Sinusoidal pitch modulation at Vibrato Rate (default 5.5 Hz); depth = max(mod wheel, auto-vibrato), where auto-vibrato reaches 0.5 over 0.4 s, starting once a note has been held 0.6 s. | `PerformanceEngine` (`kAutoVib*`), `GuitarEngine` |
| Fret noise | At note-off: an 8 ms noise burst high-passed at 4 kHz, at 0.006 x note velocity x Fret Noise (finger lifting off); legato notes add a 0.02 x Fret Noise burst. | `GuitarString::release`, `GuitarString::legato` |
| String squeak | Q-7 band-pass noise swept 1.2 <-> 3.8 kHz under a raised cosine, 40 ms + 12 ms per fret shifted, full level on wound strings, 0.25 on plain. | `dsp/ArticulationNoise.cpp` |

All rows above were checked against the source at the tested commit.

## Automatic detection (live path, Poly mode, no UI controls needed)

MIDI is guitar notation: it sounds an octave below the written note, as on a real
guitar (written E3, MIDI 52, is the open low E at 82.4 Hz). Pitches below are
the SOUNDING notes; Transpose applies on top. Keyswitches (MIDI 12-37) are keys
and are not shifted.

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
| Bend | Pass: 52.00 -> 54.08 (+208 c) -> 52.06 (release returns within +6 c) | Pass: 52.00 -> 54.05 -> 52.08 | Untested [no Mac] | Untested [no Mac] |
| Harmonic | Pass: 12th -0.5 c, 19th +0.1 c; partials 3+ -11.6 dB vs -4.2 fretted | Pass: -0.6 c, 0.0 c; -11.6 vs -3.9 dB | Untested [no Mac] | Untested [no Mac] |
| Vibrato | Pass: 0.2 c wobble before 0.6 s, 7.8 c after at 5.5 Hz | Pass: same | Untested [no Mac] | Untested [no Mac] |
| Fret noise | Pass: note-off 2.5-8 kHz burst +9.4 dB | Pass: +14.9 dB | Untested [no Mac] | Untested [no Mac] |
| String squeak | Pass: quick shift flatness 0.507 vs 0.118, +4.6 dB | Pass: 0.515 vs 0.104, +2.9 dB | Untested [no Mac] | Untested [no Mac] |
| Pick attack | Pass: >4 kHz first 8 ms +13.9 / +13.3 dB over sustain; vel 120 8.2 dB brighter | Pass: +14.3 / +12.7 dB; 8.8 dB brighter | Untested [no Mac] | Untested [no Mac] |

macOS cells: no Mac machine is available. They are not inferred from the
macOS CI workflow, unit tests or auval.


## Playability update (2026-10-13, code `f2d1bf2` + timing tool `0d03fbf`)

Tester: Claude (omp agent). VST3 `C:\Program Files\Common Files\VST3\Fretwork.vst3`
(SHA-256 `DFA7EBCF...7D3C08`, built 2026-10-13 19:52). Unit suite 2516 checks, 0 failures (after `4c7144d`, which clears stale hammer-on state on pedal release and mode change; VST3 22/22, articulations 13/13).

### Out of the box
- MIDI is guitar notation: it sounds an octave below the written note.
- Strum mode strums a pressed chord by itself (one down-stroke 19 ms after the first key); stroke keys still work and suppress it for 4 s.
- Seq plays while a chord is held (Seq Play not required); slots 1-4 ship with Pop, Folk, Rock and Funk patterns.
- Every factory preset sounds a held chord with no other input and without clipping (18/18, peaks -3.4 to -13.1 dBFS). Style starters: **Pop Strum**, **Rock Rhythm**, **Campfire Strum** / **Fingerstyle Folk** (folk).
- Drive is saturation, not volume; all bodies play at the same loudness.

### Every articulation in every mode (engine events, `Tests/ModeArticTests.cpp`)

| Articulation | Poly | Strum | Arp | Seq | How it is triggered outside Poly |
|---|---|---|---|---|---|
| Strum down / up | Pass | Pass | Pass | Pass | stroke keys; Strum mode auto-strums; Seq pattern rows |
| Palm mute | Pass | Pass | Pass | Pass | chord velocity 20-45 |
| Dead note | Pass | Pass | Pass | Pass | chord velocity 1-19 |
| Hammer-on | Pass | Pass | Pass | Pass | second note 1-4 st over a single held note |
| Pull-off | Pass | Pass | Pass | Pass | release the hammered note |
| Slide | Pass | Pass | Pass | Pass | second note 5-12 st (or vel >= 110) over a single held note |
| Harmonic | Pass | Pass | Pass | Pass | chord velocity 127; notes move to their 12/19/24th-fret harmonic |
| Vibrato | Pass | Pass | Pass | Pass | automatic after 0.6 s with 1-2 notes held |
| Bend | Pass | Pass | Pass | Pass | pitch wheel |
| Fret noise | Pass | Pass | Pass | Pass | letting the chord go releases every fretted string |
| String squeak | Pass | Pass | Pass | Pass | voicing lands 5+ frets away within 0.4 s of letting go |
| Pick attack | Pass | Pass | Pass | Pass (pattern accents) | key velocity |

Limit: in Strum/Arp/Seq, hammer-on/pull-off/slide apply over a single held note.
Notes added to a held chord are treated as chord-building.

### Response times vs a real guitar (`ModeMatrix timing`, VST3 render)

| Measure | Fretwork | Real guitar | Result |
|---|---|---|---|
| Note-on to sound | 0.19 ms | 0-10 ms | Pass |
| Pick transient | 7 ms | 0.5-8 ms | Pass |
| Strum sweep, 6 strings | 30 ms | 10-80 ms | Pass |
| Hammer-on pitch change | 0 ms | 0-10 ms | Pass |
| Slide glide | 161 ms | 40-250 ms | Pass |
| Vibrato onset | 0.71 s | 0.3-1.2 s | Pass |
| Vibrato rate | 5.4 Hz | 4-7.5 Hz | Pass |
| Vibrato depth | 22 c | 5-50 c | Pass |
| Palm mute to -20 dB | 0.26 s | 0.1-0.6 s | Pass |
| Dead note to -30 dB | 0.153 s | 0.01-0.15 s | **Fail** |

Dead note: the first 20 dB fall in ~0.12 s; below that the dreadnought's ~100 Hz
air resonance rings at about -75 dBFS for ~0.4 s T60. The 0.15 s reference was
written for the string alone. Left as a recorded Fail.

### Cells not tested
- **Standalone macOS, VST3 macOS**: Untested, no Mac available. Not inferred from CI.

### Swing and time signatures (2026-10-13)
- **Swing** (the knob formerly "Arp Swing", same parameter id) now delays every second step of the Arpeggiator **and the Sequencer** by that share of a step (0-60 %). Measured at 40 %: odd steps late by 60 ms at 100 BPM, in 3/4, 4/4, 6/8, 8/4 and 9/8.
- **Signatures** 8/4 and 9/8 were added after the existing seven (saved projects keep their meaning); a bar holds 32 and 18 sixteenths.
- The metronome now clicks eighths in x/8 meters (6/8 gives six clicks a bar, not six quarters).
- Dead notes now also stop the body resonance: dead-note decay 0.153 s -> 0.072 s, timing table 10/10 within the real-guitar ranges.

### Standalone Windows (2026-10-13, code `41bbcf2`)
Run with the other audio apps (Discord, Edge, limusic: 38 processes) suspended and
resumed afterwards, as the user chose. The output was verified digitally silent
(-240 dBFS) before and after. MIDI went through loopMIDI to the real
Standalone, audio was captured from the speaker output by WASAPI loopback, and
the same fixed criteria scored it.

- Modes and factory presets: **22/22 Pass** (C4 +0.6 c in Poly; Seq 7 attacks; every preset holds C-E-G).
- Articulations: **13/13 Pass** (strum down +24.0 ms, up -36.5 ms; palm mute -24.3 dB vs -5.9 open;
  dead -42.2 dB; hammer-on A3 to B3 with no glide; pull-off -10.5 dB under a pick; harmonic -0.3 c / 0.0 c;
  vibrato 7.8 c at 5.5 Hz; bend +192 c wheel-up, 51.90 on release).
- A first attempt with a 4.5 s start-up wait read silent for most rows: a quiet output device
  needs about 8 s before the first note is heard. That is the capture timing, not the plugin;
  with a 9 s wait the full run was clean and 3/3 repeat takes of one row agreed within 0.8 c.
- Not re-run on the Standalone: the chord-mode audio check and swing/signature rows (VST3 and unit tests only).
- **macOS (Standalone and VST3): Untested, no Mac.**

### Listening test (2026-10-13, Windows VST3, user)
Clips rendered from the VST3 and played through the speakers with a spoken label before each;
the user rated each group. Tester for the clips: Claude; listener: the user. macOS not tested.

| Group | Round 1 | Round 2 | Round 3 |
|---|---|---|---|
| Strums and mutes (strum down/up, palm mute, dead note) | Mostly, with flaws | Yes, convincing | |
| Legato (slide, hammer-on, pull-off) | Mostly, with flaws | not replayed | |
| Pitch (bend, harmonics, vibrato) | Mostly, with flaws | not replayed | |
| Noises (fret noise, squeak, pick attack) | Sounds fake | Better but still fake | **Natural now** |
| Play modes (Poly, Strum, Arp, Seq) | No, not musical (stiff, thin) | Better, still stiff | **Yes, musical** |
| Presets (pop, rock, folk) | Mostly, some are off | Yes, usable | |

Changes made from the listening: softer pick click and darker strum scrape; Humanization default 40 %
and Room 20 %; fret noise as a short low rustle; squeak as a rough stick-slip band; wider Strum and
Arp timing with Arp beat accents; Poly chords get a 0-6 ms spread. Spectrum measurements showed no low-end
deficit for the "thin" remark (94 % of energy under 500 Hz), so only Room was raised; the user later rated the modes musical.

Not re-listened after the fixes: Legato and Pitch groups (still "Mostly, with flaws" from round 1; no details were given,
and none of the changes touched them). The unit suite (2542 checks), VST3 rows (22/22), articulations (13/13) and
timing (10/10) pass on the final build.

### Listening test, Legato and Pitch re-listen (2026-10-14, Windows VST3, user)
Round 1: "Mostly, with flaws". After round 2 the user named hammer-on/pull-off and more than one pitch item as off.
Changes: louder hammer-on/pull-off body with a softer 1.8 kHz fret contact, bend smoothing 5 -> 25 ms, harmonic fundamental level
raised, automatic vibrato depth 0.5 -> 0.3, sustain lengthened (see PIANOTEQ_STUDY.md), acoustic stereo spread. Re-rendered from
the committed build (4c8273d plus e15274d). Rating: Legato Pass, Pitch Pass. The vibrato gate moved from >0.30 to >0.25 with the depth.
