# PiLab AI Project Generation Guide

This guide is for AI assistants that generate PiLab Ladder Editor JSON projects.

The goal is to generate projects that import cleanly, simulate correctly, and produce useful JavaScript/AngelScript output.

---

## Hard rules

1. Every ladder rung has exactly 8 `main` slots.
2. Every branch has exactly 8 `cells`.
3. Empty slots are `null`.
4. Branch `end` must be greater than `start`.
5. Branch symbols only go in `cells[start]` through `cells[end - 1]`.
6. Tags must be valid identifiers: `^[A-Za-z_][A-Za-z0-9_]*$`.
7. `TON`, `TOF`, `CTU`, and `CTD` require positive `preset`.
8. `CTU` reset and `CTD` reload use `resetTag`.
9. Do not use an `NO` or `NC` contact with the same tag as an `ONS`, `TON`, `TOF`, `CTU`, or `CTD` block.
10. To use a timer/counter done bit in ladder, place the block inline.
11. To use timer/counter state in script, use `.Q()`, `.ET()`, or `.CV()`.

---

## Function block rule

These are function block instances:

```text
ONS
TON
TOF
CTU
CTD
```

Their `tag` is the instance name.

Correct:

```json
[
  { "id": "no_start", "type": "NO", "tag": "StartPB" },
  { "id": "ton_delay", "type": "TON", "tag": "T_Delay", "preset": 500 },
  { "id": "out_ready", "type": "OUT", "tag": "Ready" }
]
```

Wrong:

```json
[
  { "id": "no_start", "type": "NO", "tag": "StartPB" },
  { "id": "ton_delay", "type": "TON", "tag": "T_Delay", "preset": 500 },
  { "id": "no_delay_done", "type": "NO", "tag": "T_Delay" },
  { "id": "out_ready", "type": "OUT", "tag": "Ready" }
]
```

---

## Use ladder for visible machine logic

Good ladder use cases:

```text
Start/stop latch
E-stop interlock
Motor permissive
Alarm latch/reset
Timer driving output
Counter driving done coil
```

---

## Use script for dense math/data logic

Good script use cases:

```cpp
TankLevelPct = ((RawAI0 - RawMin) * 100.0f) / (RawMax - RawMin);
TankLevelPct = max(0.0f, min(100.0f, TankLevelPct));
Q_TankHigh = TankLevelPct >= 80.0f;
```

---

## Browser simulator script subset

Safe:

```text
assignments
compound assignments
++ / --
if / else with braces
local variables
numeric math
comparisons
Boolean logic
min/max/abs/sqrt/sin/cos
T1.Q(), T1.ET(), C1.CV()
```

Avoid for simulator-oriented examples:

```text
loops
arrays
functions
classes
switch/case
multi-line expressions
complex library calls
```

---

## Recommended rung order

For a small generated project, use:

1. Input conditioning / one-shots
2. Safety and permissives
3. Start/stop latch
4. Timers
5. Counters
6. Script math / scaling
7. Outputs
8. HMI/status helper tags

Scan order matters. Later rungs see tags written by earlier rungs in the same scan.
