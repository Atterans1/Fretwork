# Electric tone: pickups, amp and cabinet

What the electric signal chain does after the 2026-11-25 tone pass, and what
the tests measure. Code: `dsp/PickupModel.*`, `dsp/AmpFx.*`, `dsp/ModelTables.h`,
`dsp/GuitarString.cpp`. Tests: `Tests/ElectricTests.cpp`.

## Pickups

| Change | Detail |
| --- | --- |
| Positions | Distance from the bridge as a share of the scale: bridge 0.060, middle 0.152 (was 0.120), neck 0.245 (was 0.190). The new middle and neck sit where a Strat's and a Les Paul's do, so the neck pickup is round and the middle sits between. |
| Comb read | The pickup's comb (bridge signal minus the same signal `position x period` samples ago) reads that delay with linear interpolation instead of rounding to a whole sample, so its notches land on the right partial on every note. History 2048 samples. |
| 7-string and 12-string | The comb keeps a channel per string and per 12-string course (`2 x kMaxStrings`). Before, a 7th string fell outside the table and skipped the pickup, and a course's octave string shared its main string's history. |

Measured: on the neck pickup the 4th harmonic of an open string is cancelled
(-26 dB against -6.5 dB on the bridge); brightness runs bridge > middle > neck.
On the clean clips the neck pickup's spectral centroid is 309 Hz against the
bridge's 476 Hz (before: 340 and 501).

## Amp

| Stage | Detail |
| --- | --- |
| Before the clipper | A low shelf thins the lows so the drive doesn't turn to mud: Clean 120 Hz -2 dB, Crunch 200 Hz -4.5 dB, Lead 250 Hz -6 dB (plus 150 Hz -2.5 dB before its second stage). |
| Lead, two stages | An asymmetric first stage, a 50 Hz high-pass between stages, then a second tanh stage. |
| After the clipper | A low-pass like a valve stage's Miller capacitance takes the fizz off: Clean 11 kHz, Crunch 7.5 kHz, Lead 9 kHz then 6.5 kHz. |
| Power-amp sag | A level envelope (5 ms attack, 120 ms release) pulls the gain back on loud chords and lets it bloom back: Clean 0.4, Crunch 1.0, Lead 0.6. |
| Level | Makeup gain set so each amp sits at the dry level on power chords: Clean 0.675, Crunch 0.478, Lead 0.277. |

## Cabinets

Eight biquads each: a high-pass, the cone's low resonance, a low-mid dip
(2x12, 4x12), the presence rise, the cone break-up notch and the peak above
it, then two low-passes (about 24 dB/octave).

| Cab | High-pass | Cone | Low-mid | Presence | Break-up notch / peak | Low-passes |
| --- | --- | --- | --- | --- | --- | --- |
| 1x12 | 90 Hz | 110 Hz +2 dB | 1.6 kHz +1.5 dB | 2.6 kHz +3 dB | 3.8 kHz -4 dB / 4.8 kHz +2 dB | 6.0, 7.2 kHz |
| 2x12 | 75 Hz | 115 Hz +3 dB | 500 Hz -1.5 dB | 2.4 kHz +3 dB | 3.6 kHz -4 dB / 4.5 kHz +2 dB | 5.4, 6.4 kHz |
| 4x12 | 70 Hz | 95 Hz +4.5 dB | 420 Hz -3 dB | 2.8 kHz +4 dB | 3.9 kHz -5 dB / 4.6 kHz +1.5 dB | 4.4, 5.0 kHz |

Measured on Lead through the 4x12: energy above 8 kHz 0.0176 % -> 0.0059 %,
below 70 Hz 1.39 % -> 0.92 %, and 23.7 % in the 70-160 Hz chug band.

## String delay read (all bodies)

The string loop now reads its delay line with 4-point Lagrange interpolation,
then a fixed two-tap smoother `(1 - a) x[n] + a x[n-1]`, a = 0.2113. A linear
read loses treble by an amount that depends on each note's fractional delay
and compounds every pass, so neighbouring notes rang with uneven treble
(decay near 3 kHz across E3 to E4 swung between 26 and 69 dB/s). The smoother
is the linear read's average loss, so the overall voicing is unchanged while
every note gets the same: 42-53 dB/s across the same notes now, rising
smoothly up the neck. The smoother's delay comes off the read, so tuning holds.

## CPU

Median of the CpuProbe matrix on the shared test machine: 14.4 % of one core
on main, 19.6 % on this branch (worst case 31.4 % -> 34.3 %, Solid Electric,
Lead 4x12, 96 kHz, 64-sample blocks). The test machine is noisy; main shows
block overruns too.
