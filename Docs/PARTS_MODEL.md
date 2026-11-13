# Body and component-parts model

Twelve physical controls, all analytic (no samples, no impulse responses). Each default is the **neutral position**: every
transform is the exact identity there, so a project that never touches them renders bit-for-bit as before
(`Tests/PartsModelTests.cpp` checks it). Code: `Source/dsp/PartsModel.h` (pure functions), wired through `BodyModel`,
`LoopDesign`, `GuitarString`, `Excitation`, `PickupModel`, `GuitarEngine` and `PluginProcessor::buildEngineParams`.
UI: BODY-adjacent **PARTS** tab (ten controls) and WINDINGS / MAGNET on the PICKUP tab.

`t` below is the bipolar position of a percent control, `(pct - 50) / 50`, and `s` is the scale length over 25.5 in.

| Control (id) | Range (neutral) | Formula | Active on | Where it acts |
|---|---|---|---|---|
| Air Volume (`airVolume`) | 0.7-1.3 x (1) | air mode hz x sqrt(hole / V) | Dread, Classical, Semi | body mode 0 |
| Sound Hole (`soundHole`) | 0.6-1.5 x (1) | same mode: hz x sqrt(A / V), Q x A^-0.7 | Dread, Classical, Semi | body mode 0 |
| Top Thickness (`topThickness`) | 0.7-1.4 x (1) | plate modes hz x T, level -6 log2 T dB; sustain x T^0.6 | all (Solid: sustain only) | modes 1+, string T60 |
| Bracing (`bracing`) | 0-100 % (50) | modes above ~600 Hz: hz x 2^(0.35 t w), Q x 2^(0.6 t w); sustain x 2^(0.25 t) | all (Solid: sustain only) | modes, string T60 |
| Back Coupling (`backCoupling`) | 0-100 % (50) | +/- 6 dB t on modes 200-700 Hz (half-octave skirts) | Dread, Classical, Semi | body modes |
| Wood Density (`woodDensity`) | 0.7-1.4 x (1) | sustain x D^0.8, harmonic T60 ratio x D^0.25 | all | string loop |
| Neck Mass (`neckMass`) | 0-100 % (0 = off) | extra mode, hz 420 -> 150, level -24 -> +2 dB, Q 6 | all | own path, see below |
| Bridge Mass (`bridgeMass`) | 0-100 % (50) | T60 x 2^(0.45 t), harmonic ratio x 2^(-0.5 t), piezo highpass 40 x 2^-t Hz | all | string loop, piezo |
| Saddle Hardness (`saddleHardness`) | 0-100 % (50) | harmonic ratio x 2^(0.35 t), pick lowpass and click x 2^(0.5 t), piezo high shelf 4 t dB at 3 kHz | all | string loop, pluck, piezo |
| Scale Length (`scaleLength`) | 22-30 in (25.5) | B x 1/s^2, harmonic ratio x s^0.8, pick brightness x s, pick and pickup positions / s | all | loop, pluck, pickup combs |
| Pickup Windings (`pickupWindings`) | 0-100 % (50) | resonance hz x 0.7..1.4 (more turns = lower), Q x 2^(0.6 t), +/- 3 dB | all | pickup tone and gain |
| Magnet Pull (`magnetPull`) | 0-100 % (50) | string T60 x 0.75^(t share), pickup +/- 2 dB | all, scaled by pickup share | string loop, pickup gain |

## Per-body notes

* **Solid Electric** has no cavity or back plate: Air Volume, Sound Hole and Back Coupling are dimmed and inert
  (`partActive`). Its single slab mode is mixed 40 dB down (the -20 dB table level is applied twice, and the factory Mic Blend is 0),
  so moving it could not be heard; Top Thickness and Bracing therefore act on its sustain instead.
* **Neck Mass** bypasses the body blend. A mode routed through the mic path would be buried on a solid body, so it is added to the
  output at its own absolute level (up to +2 dB), never when 0, and fades in below 5 %.
* **Magnet Pull rule:** the engine always mixes the pickup (0.25 on acoustic bodies, 0.7 on Solid/Semi) with the acoustic path
  (mix level from the body table). Share = electric / (electric + acoustic): Dread and Classical 0.2, Semi 0.64, Solid 0.875.
  The drag is T60 x 0.75^(t share): up to -22 % on Solid, -6 % on an acoustic.
* **Semi-Hollow** at its factory Mic Blend of 25 % hears the cavity controls only faintly (Air 0.05 dB, Hole 0.2 dB, Back 0.5 dB, measured);
  raise MIC to bring them out. The tests measure at the plugin's parameter defaults (Mic Blend 70 %).
* Controls glide with the same one-pole smoothing as the other knobs; string-loop controls apply at the next pluck.
* Seventh course of the 7-string: takes these controls like the rest (it has never taken the per-body loss scale; unchanged).

## Measured effect (min to max, one fixed note per control, 3.5 s, default parameters)

Metric: largest change of any of five octave-ish bands (60-180-700-2k-6k-16k Hz, noise-floor bands excluded), spectral centroid, decay time.
Thresholds: 1 dB, 3 %, 8 % (any one). `Tests/PartsModelTests.cpp` prints the full table on every run; sample (band dB / centroid % / T60 %):

| Control | Dread | Classical | Solid | Semi |
|---|---|---|---|---|
| airVolume | 7.9 / 38 / 1 | 2.3 / 19 / 1 | inactive | 0.6 / 4.5 / 0 |
| soundHole | 0.4 / 6.3 / 1 | 6.9 / 44 / 1 | inactive | 1.7 / 13 / 0 |
| topThickness | 5.4 / 32 / 29 | 5.2 / 14 / 24 | 0.5 / 0 / 20 | 1.0 / 3.6 / 21 |
| bracing | 7.1 / 5.1 / 16 | 0.8 / 9.9 / 19 | 0.4 / 0 / 12 | 1.1 / 5.4 / 13 |
| backCoupling | 3.5 / 4.5 / 1 | 7.6 / 12 / 2 | inactive | 2.8 / 20 / 2 |
| woodDensity | 5.7 / 1 / 45 | 2.1 / 1 / 46 | 1.5 / 0.5 / 43 | 3.5 / 1 / 44 |
| neckMass | 13.3 / 46 / 2 | 11.3 / 50 / 3 | 14.2 / 62 / 3 | 13.2 / 59 / 3 |
| bridgeMass | 1.1 / 1.9 / 39 | 1.2 / 1.6 / 42 | 0.6 / 0.5 / 33 | 0.7 / 1.2 / 36 |
| saddleHardness | 3.4 / 5.8 / 1 | 1.9 / 2.4 / 0 | 2.4 / 7.0 / 1 | 2.6 / 7.2 / 1 |
| scaleLength | 5.2 / 21 / 1 | 5.7 / 6.2 / 0 | 4.8 / 26 / 3 | 5.0 / 24 / 2 |
| pickupWindings | 3.0 / 3.8 / 1 | 2.1 / 2.3 / 0 | 9.4 / 4.2 / 3 | 9.0 / 6.9 / 2 |
| magnetPull | 1.5 / 2.0 / 7 | 1.0 / 1.3 / 8 | 3.2 / 0.4 / 26 | 3.1 / 2.5 / 21 |
