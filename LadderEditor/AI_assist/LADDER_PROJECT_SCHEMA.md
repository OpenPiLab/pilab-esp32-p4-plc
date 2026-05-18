# PiLab Ladder Project JSON Schema

This document describes the saved-file format used by the **PiLab Ladder Editor**.

The project file is JSON. It is meant to be small enough for people, tools, and AI systems to read and generate directly.

The JSON project is the interchange format between:

- the browser ladder editor
- the browser simulator and live tag table
- imported/exported project files
- the ladder-to-AngelScript transpiler
- the ladder-to-JavaScript transpiler
- mixed ladder/script examples
- future PiLab firmware upload paths

Canonical schema file:

```text
schema/pilab-ladder-project.schema.json
```

Current schema version: **1**

The editor also contains a JavaScript shape validator and a semantic validator. The schema protects the file shape. The semantic validator protects editor/PLC meaning.

---

## 1. Top-level project object

A project is a JSON object with this basic shape:

```json
{
  "schema": "pilab.ladder.project",
  "schema_version": 1,
  "name": "PiLab Ladder Project",
  "scan_ms": 5,
  "rungs": []
}
```

| Field | Type | Required | Description |
|---|---:|---:|---|
| `schema` | string | optional | Recommended identifier. Current value: `pilab.ladder.project`. |
| `schema_version` | integer | optional | Recommended version number. Current formal version is `1`. |
| `name` | string | yes | Human-readable project name. |
| `scan_ms` | number | yes | Nominal scan time in milliseconds. Used by timers, counters, generated JavaScript, and the browser simulator. |
| `rungs` | array | yes | Ordered list of ladder and script rungs. Scan order follows array order. |

Older editor exports may not include `schema` or `schema_version`. The editor accepts those files and normalizes them on import.

---

## 2. Scan-order model

PiLab executes rungs in array order:

```text
rungs[0]
rungs[1]
rungs[2]
...
```

This matters. A later rung sees tag values written by earlier rungs in the same scan.

A project may freely mix:

```json
{ "kind": "ladder" }
```

and:

```json
{ "kind": "script" }
```

Script rungs are emitted into the same generated `scan()` function as ladder rungs.

---

## 3. Wire-node ladder model

PiLab ladder rungs use a fixed **8-slot / 9-wire-node** model:

```text
node 0   slot 0   node 1   slot 1   node 2 ... slot 7   node 8
  |------[   ]------|------[   ]------| ... ------(   )------|
```

Rules:

- Each ladder rung has **9 wire nodes**, numbered `0` through `8`.
- Each ladder rung has **8 instruction slots**, numbered `0` through `7`.
- A main-path symbol in slot `n` connects wire node `n` to wire node `n + 1`.
- A branch connects from `start` wire node to `end` wire node.
- A branch may contain symbols in `cells[start]` through `cells[end - 1]`.
- Branches connect wire-node to wire-node, not symbol-center to symbol-center.
- Branch `end` must be greater than branch `start`.

This is the most important rule in the format. It makes the saved project behave like a small power-flow graph instead of a drawing-only representation.

---

## 4. Ladder rung

A ladder rung has this shape:

```json
{
  "id": "r1",
  "kind": "ladder",
  "comment": "Start/stop seal-in motor latch",
  "main": [null, null, null, null, null, null, null, null],
  "branches": []
}
```

| Field | Type | Required | Description |
|---|---:|---:|---|
| `id` | string | yes | Unique editor/runtime identifier for this rung. |
| `kind` | string | yes | Must be `ladder`. |
| `comment` | string | yes | Human-readable rung comment. May be empty. |
| `main` | array | yes | Exactly 8 entries. Each entry is either `null` or a symbol. |
| `branches` | array | yes | Branch objects for this rung. May be empty. |

---

## 5. Script rung

Script rungs allow AngelScript-like text logic to live in the same ordered scan list as visual ladder rungs.

```json
{
  "id": "s1",
  "kind": "script",
  "comment": "Custom script rung",
  "code": "Q_Debug = I0_Auto && !I1_Stop;"
}
```

| Field | Type | Required | Description |
|---|---:|---:|---|
| `id` | string | yes | Unique editor/runtime identifier for this rung. |
| `kind` | string | yes | Must be `script`. |
| `comment` | string | yes | Human-readable rung comment. May be empty. |
| `code` | string | yes | Script body emitted inside generated `scan()`. |

### AngelScript export behavior

For AngelScript export, script rung code is passed through directly inside `scan()`.

That means this:

```json
{
  "kind": "script",
  "code": "SpeedCmd = HmiSpeedSetpoint * SpeedTrim;"
}
```

is emitted as code inside:

```cpp
void scan()
{
    SpeedCmd = HmiSpeedSetpoint * SpeedTrim;
}
```

The generated AngelScript assumes referenced tags/globals exist in the PiLab runtime or in surrounding declarations.

### JavaScript simulator subset

The browser/Node JavaScript simulator supports a practical deterministic subset of AngelScript-like statements. It is **not** a full AngelScript interpreter.

Supported in script rungs by the JavaScript simulator:

```text
Assignments:            Tag = expression;
Compound assignments:   Tag += expression;  Tag -= expression;  Tag *= expression;  Tag /= expression;
Increment/decrement:    Tag++;  Tag--;
if blocks:              if (...) { ... } else if (...) { ... } else { ... }
Local variables:        bool/int/uint/float/double/string/auto name = expression;
Math:                   + - * / %
Comparisons:            > >= < <= == !=
Boolean logic:          && || !
Block reads:            Timer.Q() Timer.ET() Counter.CV()
Math helpers:           min max abs sqrt sin cos tan floor ceil round pow exp log etc.
```

Limitations of the JavaScript simulator subset:

```text
No loops
No user-defined functions
No arrays/classes/objects
No switch/case
No multiline expressions
No arbitrary AngelScript library calls
```

Persistent state should be stored in tags, not local variables. A local variable is recreated every scan.

Good persistent-state example:

```cpp
Q_RisingEdge = I0_Input && !I0_Last;
I0_Last = I0_Input;
```

`I0_Last` is a tag, so it persists between scans.

---

## 6. Branch object

A branch is a parallel path between two wire nodes.

```json
{
  "id": "b1",
  "start": 0,
  "end": 1,
  "cells": [null, null, null, null, null, null, null, null]
}
```

| Field | Type | Required | Description |
|---|---:|---:|---|
| `id` | string | yes | Unique branch identifier. |
| `start` | integer | yes | Start wire node, `0` through `8`. |
| `end` | integer | yes | End wire node, `0` through `8`. Must be greater than `start`. |
| `cells` | array | yes | Exactly 8 entries. Symbols should only appear from `start` through `end - 1`. |

Example branch from node 0 to node 1:

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

The schema now enumerates valid `start`/`end` combinations, so `end <= start` is rejected by schema validation.

---

## 7. Symbol object

All ladder symbols share this shape:

```json
{
  "id": "no_I0",
  "type": "NO",
  "tag": "I0"
}
```

| Field | Type | Required | Description |
|---|---:|---:|---|
| `id` | string | yes | Unique symbol identifier. |
| `type` | string | yes | One of `NO`, `NC`, `OUT`, `SET`, `RST`, `ONS`, `TON`, `TOF`, `CTU`, `CTD`. |
| `tag` | string | yes | AngelScript-compatible identifier. |
| `preset` | number | required for `TON`, `TOF`, `CTU`, `CTD` | Timer preset in milliseconds or counter preset count. |
| `resetTag` | string | optional | Canonical counter control Boolean expression. For `CTU` this resets `CV` to `0`; for `CTD` this reloads `CV` from `preset`. |

The current source also accepts legacy/alias counter control fields:

```text
resetExpr
loadTag
loadExpr
```

For new generated files, prefer `resetTag`.

Valid tag names must match:

```text
^[A-Za-z_][A-Za-z0-9_]*$
```

Valid examples:

```text
I0
Q0_Motor
T_OverTemp
C_Parts
RawAI0
TankLevelPct
```

Invalid examples:

```text
0_Start
Motor Run
Q-Alarm
```

---

## 8. Symbol semantics

| Type | Meaning | Power-flow / transpiler behavior |
|---|---|---|
| `NO` | Normally-open contact | Passes power when `tag` is true. |
| `NC` | Normally-closed contact | Passes power when `tag` is false. |
| `OUT` | Output coil | Does not block power flow. Rung result writes to `tag`. |
| `SET` | Set/latch coil | Does not block power flow. When power reaches the coil, `tag` is written true and remains true until reset elsewhere. |
| `RST` | Reset/unlatch coil | Does not block power flow. When power reaches the coil, `tag` is written false. |
| `ONS` | One-shot rising edge | Stateful inline block. Its `.Q()` is true for one scan when input power changes from false to true. |
| `TON` | On-delay timer | Updates from power reaching timer input. `.Q()` turns true after elapsed time reaches preset, and false immediately when input goes false. |
| `TOF` | Off-delay timer | Updates from power reaching timer input. `.Q()` remains true during the off-delay after input goes false. |
| `CTU` | Count-up counter | Counts rising edges of input power, clamps `CV` at `preset`, sets `.Q()` when `CV >= preset`, and resets to `0` when `resetTag` is true. |
| `CTD` | Count-down counter | Counts down on rising edges of input power, initializes/reloads `CV` from `preset` when `resetTag` is true, and sets `.Q()` when `CV == 0`. |

### Stored-output blocks

The current simulator/transpiler treats these as stored-output blocks for downstream power flow:

```text
TOF
CTU
CTD
```

That means their `.Q()` output can continue powering symbols to the right even after the left-side input power has dropped.

This is important for rungs like:

```text
--[ PartSensor ]--[ CTU C_Parts PV 3 ]----------------( BatchDone )
```

After `C_Parts.CV >= 3`, `BatchDone` remains on even after `PartSensor` goes false, until the counter is reset.

---

## 9. Counter reset/load expressions

Counter control expressions are intentionally limited to a small Boolean subset:

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
{
  "id": "ctu_parts",
  "type": "CTU",
  "tag": "C_Parts",
  "preset": 3,
  "resetTag": "ResetPB || !AutoMode"
}
```

For `CTU`, the expression resets the count to zero.

For `CTD`, the expression reloads the count from preset.

---

## 10. Simulator tag behavior

The simulator now supports Boolean, numeric, and string tag values.

The tag table can infer and edit tags discovered from:

- ladder symbol `tag` fields
- counter reset/load expressions
- script rung identifiers
- existing simulator runtime tags

Ladder contact truth rules:

```text
boolean true       -> true
boolean false      -> false
number 0           -> false
nonzero number     -> true
empty string       -> false
"false"            -> false
"0"                -> false
other string       -> true
```

Numeric tags are useful for script rungs:

```cpp
ScaledSpeed = HmiSpeedSetpoint * SpeedTrim;
Q_SpeedHigh = ScaledSpeed >= MaxSpeed;
```

The simulator UI supports filtering and starred/watch tags so a user can monitor a subset of a large tag list.

---

## 11. Practical script examples

### Compare / move / math / clamp

```cpp
ScaledSpeed = HmiSpeedSetpoint * SpeedTrim;
Q_SpeedHigh = ScaledSpeed >= MaxSpeed;
SpeedCmd = ScaledSpeed;

if (SpeedCmd < MinSpeed) {
    SpeedCmd = MinSpeed;
}
if (SpeedCmd > MaxSpeed) {
    SpeedCmd = MaxSpeed;
}
```

### Analog scaling with divide protection

```cpp
float span = RawMax - RawMin;

if (span > 0.0f) {
    TankLevelPct = ((RawAI0 - RawMin) * 100.0f) / span;
} else {
    TankLevelPct = 0.0f;
}

TankLevelPct = max(0.0f, min(100.0f, TankLevelPct));
```

### Rising and falling edge tags

```cpp
Q_RisingEdge = I0_Input && !I0_Last;
Q_FallingEdge = !I0_Input && I0_Last;
I0_Last = I0_Input;
```

### Script-based CTUD-style counter

```cpp
if (CUD_Reset) {
    CUD_Count = 0;
} else {
    if (CountUp && !CUD_UpLast) {
        CUD_Count++;
    }
    if (CountDown && !CUD_DownLast && CUD_Count > 0) {
        CUD_Count--;
    }
}

CUD_UpLast = CountUp;
CUD_DownLast = CountDown;
CUD_Done = CUD_Count >= CUD_Preset;
CUD_Empty = CUD_Count == 0;
```

### Script-based TP pulse timer

```cpp
if (Trigger && !TrigLast) {
    PulseActive = true;
    PulseET_ms = 0;
}

TrigLast = Trigger;

if (PulseActive) {
    Q_Pulse = true;
    PulseET_ms += 5;

    if (PulseET_ms >= 250) {
        PulseActive = false;
        Q_Pulse = false;
    }
} else {
    Q_Pulse = false;
}
```

The `5` in `PulseET_ms += 5;` should match the project `scan_ms` for a 5 ms scan project.

---

## 12. Complete example project

```json
{
  "schema": "pilab.ladder.project",
  "schema_version": 1,
  "name": "Mixed Ladder Script Example",
  "scan_ms": 5,
  "rungs": [
    {
      "id": "r_motor_latch",
      "kind": "ladder",
      "comment": "Start/stop seal-in motor latch",
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
    },
    {
      "id": "r_batch_counter",
      "kind": "ladder",
      "comment": "Count three part-sensor pulses and latch BatchDone through the CTU done output",
      "main": [
        { "id": "no_part", "type": "NO", "tag": "PartSensor" },
        { "id": "ctu_parts", "type": "CTU", "tag": "C_Parts", "preset": 3, "resetTag": "ResetBatch" },
        null,
        null,
        null,
        null,
        null,
        { "id": "out_batch", "type": "OUT", "tag": "BatchDone" }
      ],
      "branches": []
    },
    {
      "id": "s_scale_speed",
      "kind": "script",
      "comment": "Scale and clamp speed command",
      "code": "ScaledSpeed = HmiSpeedSetpoint * SpeedTrim;\nSpeedCmd = ScaledSpeed;\nif (SpeedCmd < MinSpeed) {\n    SpeedCmd = MinSpeed;\n}\nif (SpeedCmd > MaxSpeed) {\n    SpeedCmd = MaxSpeed;\n}"
    }
  ]
}
```

---

## 13. AI generation guidelines

When generating PiLab ladder project JSON:

1. Always produce valid JSON, not JavaScript object literal syntax.
2. Use exactly 8 `main` slots for every ladder rung.
3. Use exactly 8 `cells` entries for every branch.
4. Use `null` for empty slots.
5. Use unique `id` values for rungs, branches, and symbols.
6. Use valid tag names matching `^[A-Za-z_][A-Za-z0-9_]*$`.
7. Put normal coils near slot 7 unless there is a specific reason not to.
8. Prefer `resetTag` for counter reset/load control.
9. Use script rungs for math, scaling, clamping, compact state machines, and logic that would be awkward in visual ladder.
10. Keep JavaScript-simulator-compatible script rungs inside the documented subset if the user needs browser simulation.
11. Store persistent script state in tags, not local variables.
12. Set `scan_ms` deliberately; timer presets and script pulse examples assume this scan time.

---

## 14. Validation layers

The project has two validation layers:

1. **JSON Schema validation** checks saved-file shape: required fields, rung kinds, slot counts, symbol types, branch start/end shape, and tag identifier format.
2. **Editor semantic validation** checks PLC/editor-specific concerns: duplicate coils, duplicate function-block instances, invalid reset expressions, symbols outside branch spans, suspicious empty branches, complex overlapping branch structures, and outputless rungs.

The schema protects the file format. The semantic validator protects ladder meaning.

---

## 15. Test commands

Run all tests:

```bash
npm run test
```

Run tests continuously while editing:

```bash
npm run test:watch
```

Build the editor:

```bash
npm run build
```
