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
