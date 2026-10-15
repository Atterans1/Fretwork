# Pianoteq 9 Classical Guitar: black-box comparison (2026-10-13)

Method: Pianoteq 9.0.1 (installed here) was driven only through its own VST3 host interface and its documented
command line (`--headless --preset "Classical Guitar" --midi ... --wav ...`). Nothing was disassembled and no data
files were read. The same MIDI notes went through Pianoteq and Fretwork (`ModeMatrix render`), and the same code
measured both. One preset (Classical Guitar), single notes at three pitches and three velocities. Tools: `ModeMatrix probe`,
`ModeMatrix render`, `ModeMatrix artic-analyze` (outputs in C:/Users/jeffr/PianoteqStudy and FretworkListen/cmp, not committed).

## What it does well (measured)
| Behaviour | Pianoteq Classical Guitar | Fretwork nylon | Fretwork default |
|---|---|---|---|
| Decay while held (dB/s, A3 / E4) | -4.9 / -7.8 | -31 / -36 | -18 / -22 |
| Stereo side vs mid (dB) | -12 / -9 | -21 / -31 | -15 / -22 |
| Brightens with velocity (early centroid, vel 40 -> 120, A3 / E4) | 201 -> 258, 284 -> 426 Hz | 292 -> 260, 341 -> 338 Hz (flat) | 366 -> 498, 529 -> 604 Hz |
| 6th-8th partial level vs fundamental (A3, dB) | -24, -24, -32 | -39, -42, -52 | -27, -30, -26 |
| Release to -30 dB after note-off | 0.32 s | 0.12 s | 0.18 s |
| Velocity dynamic range (peak, vel 40 -> 120) | 16 dB | 15-17 dB | 15 dB |
| Attack time to peak | 18 ms | 30 ms | 3 ms |
| Partial stretch (low partials, cents) | about -2 | about 0 | about 0 |

Takeaways for Fretwork: (1) sustain is 3-6x shorter than Pianoteq's; (2) the nylon/finger preset is nearly mono and
does not brighten with velocity; (3) nylon has far less upper-partial content; (4) release is quicker and more abrupt.
The "6-12 dB/s" figure I used as a real-guitar reference is an approximate recollection, not a measured source.

## Caveats
- Pianoteq's guitar sounds one octave ABOVE the MIDI note sent (MIDI 57 sounds A4); Fretwork follows the guitar
  convention (sounds an octave below written). Phrases were shifted by -12 for Pianoteq.
- MIDI 33 (sounding A2 with that shift) produced no usable sound, so the lowest pitch was dropped from the comparison.
- Fretwork's articulation phrases use its own triggers (velocity mutes, wheel); Pianoteq does not, so most articulation
  rows read FAIL for Pianoteq (strum up, palm/dead mute, slide, hammer-on legato, harmonic, vibrato, squeak). Only
  strum down, bend (+193 c, returns), fret noise and pick attack scored. That says nothing about its articulation quality.
- Pianoteq exposes guitar controls (Guitar Legato, Guitar easy fingering, Guitar Body, Fret) that were not reachable
  from the CLI render, so legato was not tested on its own terms.
- Not a listening comparison; no licensing or demo-mode effects were checked.

## Follow-up fixes (2026-10-14)
- **Sustain:** base T60 table x2.2 in total; a held A3 now falls 9.2 dB/s (nylon) and 8.4 dB/s (default) against Pianoteq's 4.9 (E4: 11.4 and 10.9 vs 7.8). Decay is still one slope; Pianoteq's is fast then slow.
- **Nylon got darker as the key was struck harder** (share of energy above 1 kHz at 30 ms, A3, velocity 40/80/120: -12.0 / -15.9 / -18.6 dB; Pianoteq rises -29 -> -19 dB). Cause: the excitation lowpass started from zero state on a buffer the string loop repeats, so its lag left a step at the wrap that added broadband energy, largest at low cutoff; the finger's 6 ms attack ramp (longer than one period above ~167 Hz) did the same. Fix in `Excitation.cpp`: two warm-up passes so the filter is continuous across the wrap, and no finger ramp (the low cutoff already softens it). Now -31.4 / -25.5 / -21.7 dB (E4: -38.8 / -26.8 / -22.6); the default preset still brightens (-25.0 / -17.9 / -15.9).
- **Poly chords lost notes** (found while checking the above, older than this session): a key with no string yet scored every voicing slot and took the cheapest regardless of pitch, so C4+E4 struck together plucked C4 on two strings and never E4. Fixed in `PerformanceEngine.cpp` (own slot only; free string where the pitch fits if the slot's string is taken; the pluck reports the played pitch). The first note of a chord is still placed alone on the lowest fitting string before the rest arrive, by design for single notes. Regression test: "poly: keys struck together each sound at their own pitch on their own string".
- Earlier chord checks in ModeMatrix that passed were satisfied by harmonics of the one note that did sound (C's 3rd and 5th harmonics are G and E).
