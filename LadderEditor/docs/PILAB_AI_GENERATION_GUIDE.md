# PiLab AI Project Generation Guide

This guide is for AI assistants that generate PiLab Ladder Editor JSON projects.

The goal is to generate projects that import cleanly, simulate correctly, and produce useful JavaScript/AngelScript output.

Use this together with:

```text
schema/pilab-ladder-project.schema.json
docs/LADDER_PROJECT_SCHEMA.md
```

---

## Hard rules

1. Every ladder rung has exactly 8 `main` slots.
2. Every branch has exactly 8 `cells`.
3. Empty slots are `null`.
4. Branch `end` must be greater than `start`.
5. Branch symbols only go in `cells[start]` through `cells[end - 1]`.
6. Tags must be valid identifiers: `^[A-Za-z_][A-Za-z0-9_]*$`.
7. `TON`, `TOF`, `CTU`, and `CTD` require a positive `preset`.
8. `CTU` reset and `CTD` reload use `resetTag`.
9. Do not use a normal `NO` or `NC` contact with the same tag as an `ONS`, `TON`, `TOF`, `CTU`, or `CTD` block.
10. To use a timer/counter done bit in ladder, place the block inline.
11. To use timer/counter state in script, use `.Q()`, `.ET()`, or `.CV()`.
12. Use `OUT`, `SET`, and `RST` as the JSON symbol type names. Do not emit `COIL` or `RESET` in project JSON.

---

## Project skeleton

Return real JSON, not JavaScript object literal syntax.

```json
{
  "schema": "pilab.ladder.project",
  "schema_version": 1,
  "name": "Generated PiLab Project",
  "scan_ms": 5,
  "rungs": []
}
```

---

## Function-block rule

These are stateful function-block instances:

```text
ONS
TON
TOF
CTU
CTD
```

Their `tag` is the instance name. `TON`, `TOF`, `CTU`, and `CTD` require a positive `preset`; `ONS` does not.

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

The wrong example treats `T_Delay` as an ordinary Boolean tag. PiLab expects `T_Delay` to be a timer instance.

---

## Inline timer/counter pattern

To use a timer/counter done bit in ladder, put the block in the power path before the output coil.

```json
{
  "id": "r_delay_ready",
  "kind": "ladder",
  "comment": "Start delay before Ready output",
  "main": [
    { "id": "no_start", "type": "NO", "tag": "StartPB" },
    { "id": "ton_delay", "type": "TON", "tag": "T_Delay", "preset": 500 },
    null,
    null,
    null,
    null,
    null,
    { "id": "out_ready", "type": "OUT", "tag": "Ready" }
  ],
  "branches": []
}
```

Meaning:

```text
StartPB powers T_Delay.
T_Delay.Q() powers Ready after 500 ms.
```

---

## Counter reset/load rule

Use `resetTag` for both CTU reset and CTD reload.

```json
{ "id": "ctu_parts", "type": "CTU", "tag": "C_Parts", "preset": 3, "resetTag": "ResetBatch" }
```

For `CTU`, `resetTag` true means `CV = 0` and `Q = false`.

For `CTD`, `resetTag` true means `CV = preset` and `Q = false`.

Allowed `resetTag` expression syntax:

```text
identifiers
true
false
!
&&
||
parentheses
whitespace
```

Example:

```json
"resetTag": "ResetBatch || !AutoMode"
```

---

## Use ladder for visible machine logic

Good ladder use cases:

```text
Start/stop latch
E-stop interlock
Guard permissive
Motor permissive
Alarm latch/reset
Timer driving output
Counter driving done coil
```

---

## Use script for dense math/data logic

Good script use cases:

```cpp
float span = RawMax - RawMin;

if (span > 0.0f) {
    TankLevelPct = ((RawAI0 - RawMin) * 100.0f) / span;
} else {
    TankLevelPct = 0.0f;
}

TankLevelPct = max(0.0f, min(100.0f, TankLevelPct));
Q_TankHigh = TankLevelPct >= 80.0f;
```

Persistent state must be stored in tags, not local variables.

Correct persistent edge pattern:

```cpp
Q_RisingEdge = InputA && !InputA_Last;
InputA_Last = InputA;
```

---

## Browser/Node simulator script subset

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
min/max/abs/sqrt/sin/cos/tan/atan2/floor/ceil/round/pow/exp/log
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

Use those only when explicitly targeting AngelScript export and not browser/Node simulation.

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

---

## Output placement guidance

For simple ladder examples, place output coils near slot 7.

Good:

```json
"main": [
  { "id": "no_enable", "type": "NO", "tag": "Enable" },
  null,
  null,
  null,
  null,
  null,
  null,
  { "id": "out_run", "type": "OUT", "tag": "Run" }
]
```

This keeps examples readable and matches the visual expectation of a ladder rung.

---

## Branch guidance

A branch connects wire node `start` to wire node `end`.

For a standard seal-in branch around slot 0:

```json
{
  "id": "b_seal",
  "start": 0,
  "end": 1,
  "cells": [
    { "id": "no_motor", "type": "NO", "tag": "Q0_Motor" },
    null,
    null,
    null,
    null,
    null,
    null,
    null
  ]
}
```

Do not place symbols outside the branch span. For `start: 0` and `end: 1`, only `cells[0]` may contain a symbol.

---

## Pre-return checklist

Before returning generated JSON, verify:

```text
[ ] JSON parses cleanly.
[ ] Top-level name, scan_ms, and rungs exist.
[ ] Every ladder rung has kind = "ladder".
[ ] Every script rung has kind = "script".
[ ] Every ladder rung has exactly 8 main slots.
[ ] Every branch has exactly 8 cells.
[ ] Every branch has end > start.
[ ] Branch symbols are only in cells[start] through cells[end - 1].
[ ] All tags are valid identifiers.
[ ] IDs are unique enough for editor use.
[ ] TON/TOF/CTU/CTD include positive preset.
[ ] CTU/CTD use resetTag for reset/load behavior.
[ ] No ordinary NO/NC contact uses a function-block instance tag.
[ ] Script rungs stay inside the simulator subset unless the request is AngelScript-only.
```

---

## Minimal valid project

```json
{
  "schema": "pilab.ladder.project",
  "schema_version": 1,
  "name": "Minimal Motor Latch",
  "scan_ms": 5,
  "rungs": [
    {
      "id": "r_motor",
      "kind": "ladder",
      "comment": "Start/stop motor latch",
      "main": [
        { "id": "no_start", "type": "NO", "tag": "I0_Start" },
        null,
        { "id": "nc_stop", "type": "NC", "tag": "I1_Stop" },
        null,
        null,
        null,
        null,
        { "id": "out_motor", "type": "OUT", "tag": "Q0_Motor" }
      ],
      "branches": [
        {
          "id": "b_seal",
          "start": 0,
          "end": 1,
          "cells": [
            { "id": "no_motor", "type": "NO", "tag": "Q0_Motor" },
            null,
            null,
            null,
            null,
            null,
            null,
            null
          ]
        }
      ]
    }
  ]
}
```


---

## Source-code validation note

This guide was checked against the current Ladder Editor source layout containing `src/ladder/ladderModel.js`, `src/ladder/ladderSchema.js`, `src/ladder/ladderValidation.js`, `src/ladder/ladderSimulator.js`, and `src/ladder/ladderTranspiler.js`.
