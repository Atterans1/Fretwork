# Addendum to GATE.md (2026-11-15, before any render was judged)

GATE.md itself is unchanged. This file only records clarifications.

- Threshold: the single pass threshold is the one in GATE.md, 3.0 or higher on every question with confidence 0.5 or higher. The "frozen gate 2.85" in an older todo item belongs to an earlier, separate verification task that the user has not approved. It is not used in this pass.
- Judge model id, pinned: openrouter/respan/span-02-20261015. Read from a one-item identity probe (a text question unrelated to any render; 1 judged, 0 failed). That probe is the only judge call made so far.
- The model id reported by the real run's batch status is recorded with the verdicts. If it differs from the pinned id, the run counts as a deviation and is reported as one, not accepted as a pass.
