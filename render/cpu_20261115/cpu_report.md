# CPU budget: Fretwork engine, worst case (full sweep)

Source data: `cpu_full.csv` (412 rows, 120 s of audio per row, written 2026-11-15 19:29), `cpu_quick.csv`. Probe: `Tools/CpuProbe.cpp` (extended by agent CpuBudget; sweep modes quick and full). Machine: Intel64 Family 6 Model 154 Stepping 3, GenuineIntel | cores/threads: 12. Timings are from this machine only.

This report was written by Main from the CSV after the CpuBudget agent was cancelled before it produced a report. Nothing here was re-measured.

## Matrix actually swept (all 412 rows, tag=matrix)
- Sample rate: 44100, 48000, 96000. Block size: 64, 128, 256, 512.
- Body: Classical Nylon, Dreadnought Steel, Semi-Hollow, Solid Electric. Mic: 0, 70, 100. Room: 20, 100.
- Amp 0/1 and oversampling factor 1/4. In this sweep os_factor=4 occurs exactly when amp=1 (202 vs 210 rows in both), so the two effects are confounded and cannot be separated.
- Guitar: "6-String" only. No other guitar type or polyphony level was varied.

## Mean CPU (percent of real time)
- Median 7.86, min 6.25, max 30.34.
- Highest mean row: sr 96000, block 64, Solid Electric, mic 70, room 100, amp 1, os 4: 30.34% mean, p99 331%, max 5011%, 7712 overruns.
- Block 64 and sr 96000 raise the mean the most (mean 10.55% at block 64, 12.23% at 96000). Body, mic and room move the mean by under 0.5 points.

## Real-time safety (the bigger problem)
- 275 of 412 rows recorded at least one overrun. Total overruns: 319045.
- Every one of the 107 block-64 rows overran (273,211 overruns). Block 128: 74 of 94 rows. Block 256: 60 of 108. Block 512: 34 of 103. Sample rate 96000: 115 of 134 rows (263,332 overruns).
- Worst single block: 9132% of real time (sr 96000, block 256, Classical Nylon, mic 70, room 100, amp 0, os 1).
- audio_ok = 1 for all rows (the probe's audio check did not fail).

## What is NOT established
- Whether the overruns come from the engine or from the machine. IsoRender was being built and run on the same machine during this sweep (started about 17:57), and other work was active. A block taking 50 to 90 times real time looks like scheduler contention, not DSP cost. A sweep on a quiet machine is needed before calling this a plugin defect. Mean CPU is low (median 7.86%) and the heavy tail is the part to recheck.
- The amp and oversampling effects separately (confounded above).
- Other guitar types and polyphony levels. A DAW host with other plugins. Real-world buffer behavior.
- The probe's overrun definition. I did not read CpuProbe.cpp to confirm how an overrun is counted.
