# PiLab Ladder Project JSON Schema

This document describes the saved-file format for the PiLab Ladder Editor and gives practical guidance for humans and AI systems that generate PiLab ladder projects.

PiLab project JSON is used by the browser ladder editor, the live simulator, project import/export, the AngelScript transpiler, the JavaScript transpiler, and AI-generated example projects.

Current schema version: **1**

---

## The core model

PiLab is not a drawing format. It is a compact logic model.

Each ladder rung has:

```text
9 wire nodes: 0 through 8
8 instruction slots: 0 through 7
```

A symbol in slot `n` connects wire node `n` to wire node `n + 1`.

```text
node 0 -- slot 0 -- node 1 -- slot 1 -- node 2 ... slot 7 -- node 8
```

A branch connects from one wire node to another wire node. A branch from `start: 0` to `end: 3` provides an alternate path from node 0 to node 3.

---

## Top-level project

```json
{
  "schema": "pilab.ladder.project",
  "schema_version": 1,
  "name": "PiLab Ladder Project",
  "scan_ms": 5,
  "rungs": []
}
```

| Field | Required | Description |
|---|---:|---|
| `schema` | optional | Recommended value: `pilab.ladder.project`. |
| `schema_version` | optional | Recommended value: `1`. |
| `name` | yes | Human-readable project name. |
| `scan_ms` | yes | Nominal scan time in milliseconds. |
| `rungs` | yes | Ordered list of ladder and script rungs. Scan order follows this array. |

---

## Rung kinds

PiLab supports:

```text
ladder
script
```

Use ladder for visible discrete logic: interlocks, permissives, latches, timers, counters, and outputs.

Use script for dense numeric logic: math, scaling, clamp/limit, comparisons, small state machines, and analog calculations.

---

## Ladder rung shape

```json
{
  "id": "r1",
  "kind": "ladder",
  "comment": "Start/stop seal-in motor latch",
  "main": [null, null, null, null, null, null, null, null],
  "branches": []
}
```

Every ladder rung must have exactly 8 `main` entries.

---

## Script rung shape

```json
{
  "id": "s1",
  "kind": "script",
  "comment": "Scale analog input",
  "code": "TankPct = (RawAI0 * 100.0f) / 4095.0f;"
}
```

Script rungs run once per scan in the same order as the `rungs` array.

The AngelScript export passes script rungs through inside `scan()`.

The JavaScript simulator supports a practical subset:

```text
Tag = expression;
Tag += expression;
Tag++;
Tag--;
float local = expression;
int local = expression;
uint local = expression;
bool local = expression;
if (condition) { ... }
else if (condition) { ... }
else { ... }
+ - * / %
> >= < <= == !=
&& || !
min max abs sqrt sin cos tan atan2 floor ceil round pow exp log
T1.Q()
T1.ET()
C1.CV()
```

Avoid loops, arrays, classes, functions, switch/case, objects, and complex multiline expressions when targeting browser simulation.

---

## Branch shape

```json
{
  "id": "b1",
  "start": 0,
  "end": 1,
  "cells": [null, null, null, null, null, null, null, null]
}
```

Every branch must have exactly 8 `cells`.

Symbols should only appear in `cells[start]` through `cells[end - 1]`.

---

## Supported symbols

```text
NO
NC
OUT
SET
RST
ONS
TON
TOF
CTU
CTD
```

| Type | Meaning |
|---|---|
| `NO` | Normally-open contact. Passes when tag is true/nonzero. |
| `NC` | Normally-closed contact. Passes when tag is false/zero. |
| `OUT` | Output coil. Rung result writes to tag. |
| `SET` | Latches tag true when powered. |
| `RST` | Resets tag false when powered. |
| `ONS` | Rising-edge one-shot. |
| `TON` | On-delay timer. |
| `TOF` | Off-delay timer. |
| `CTU` | Count-up counter. |
| `CTD` | Count-down counter. |

---

## Critical function block rule

These symbols are stateful function block instances:

```text
ONS
TON
TOF
CTU
CTD
```

For those types, the `tag` is the **instance name**, not a normal Boolean tag.

### Correct: use the function block inline

```json
"main": [
  { "id": "no_cycle", "type": "NO", "tag": "CycleStart" },
  { "id": "ton_dwell", "type": "TON", "tag": "T_Dwell", "preset": 500 },
  { "id": "ctu_batch", "type": "CTU", "tag": "C_Batch", "preset": 10, "resetTag": "ResetBatch" },
  null,
  null,
  null,
  null,
  { "id": "out_done", "type": "OUT", "tag": "BatchDone" }
]
```

Meaning:

```text
CycleStart powers T_Dwell.
T_Dwell.Q() powers C_Batch.
C_Batch.Q() powers BatchDone.
```

### Wrong: do not add an NO contact with the timer name

```json
"main": [
  { "id": "no_cycle", "type": "NO", "tag": "CycleStart" },
  { "id": "ton_dwell", "type": "TON", "tag": "T_Dwell", "preset": 500 },
  { "id": "no_t_dwell", "type": "NO", "tag": "T_Dwell" },
  { "id": "ctu_batch", "type": "CTU", "tag": "C_Batch", "preset": 10, "resetTag": "ResetBatch" },
  null,
  null,
  null,
  { "id": "out_done", "type": "OUT", "tag": "BatchDone" }
]
```

That incorrectly treats `T_Dwell` as an ordinary Boolean tag.

### Correct script access to function blocks

```cpp
Q_TimerDone = T_Dwell.Q();
TimerElapsedMs = T_Dwell.ET();
BatchCount = C_Batch.CV();
Q_BatchDone = C_Batch.Q();
```

---

## Stored-output blocks

These blocks can continue to drive downstream ladder logic from stored internal state:

```text
TOF
CTU
CTD
```

Examples:

```text
TOF: output stays true during the off-delay after input falls.
CTU: done stays true after CV >= preset until reset.
CTD: done stays true after CV == 0 until reload/reset.
```

---

## Counter reset/load expressions

Use `resetTag` for CTU reset and CTD reload.

Allowed syntax:

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

For CTU:

```text
resetTag true -> CV = 0, Q = false
```

For CTD:

```text
resetTag true -> CV = preset, Q = false
```

Using the same counter in its reset expression is allowed:

```json
"resetTag": "ResetBatch || C_Batch"
```

That means reset when `ResetBatch` is true or when `C_Batch.Q()` is true.

---

## AI generation checklist

Before returning generated JSON, verify:

```text
[ ] Top-level name, scan_ms, rungs exist.
[ ] Every ladder rung has kind = "ladder".
[ ] Every script rung has kind = "script".
[ ] Every ladder rung has exactly 8 main slots.
[ ] Every branch has exactly 8 cells.
[ ] Every branch has end > start.
[ ] Branch symbols are only in cells[start] through cells[end - 1].
[ ] Tags are valid identifiers.
[ ] TON/TOF/CTU/CTD include positive preset.
[ ] CTU/CTD use resetTag for reset/load behavior.
[ ] No ordinary NO/NC contact uses a function block instance tag.
[ ] Script examples avoid unsupported simulator features unless explicitly targeting AngelScript only.
```
