# PiLab Ladder Project JSON Schema

This document describes the formal saved-file format for the PiLab Ladder Editor.

The editor stores projects as JSON. The JSON is the interchange format between:

- the browser ladder editor
- exported/imported project files
- future PiLab firmware upload paths
- the ladder-to-AngelScript transpiler
- future mixed ladder / AngelScript / ST-style rung support

The canonical schema file is:

```text
schema/pilab-ladder-project.schema.json
```

Current schema version: **1**

---

## Top-level project object

A project is a JSON object with this required shape:

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
| `scan_ms` | number | yes | Nominal scan time in milliseconds. Used by generated timer code and the browser simulator. |
| `rungs` | array | yes | Ordered list of ladder and script rungs. Scan order follows this array order. |

Older editor exports may not include `schema` or `schema_version`. The editor still accepts those files and normalizes them on import.

---

## Wire-node ladder model

PiLab ladder rungs use a fixed wire-node model:

```text
node 0   slot 0   node 1   slot 1   node 2 ... slot 7   node 8
  |------[   ]------|------[   ]------| ... ------(   )------|
```

Important rules:

- Each ladder rung has **9 wire nodes**, numbered `0` through `8`.
- Each ladder rung has **8 instruction slots**, numbered `0` through `7`.
- A main-path symbol in slot `n` connects wire node `n` to wire node `n + 1`.
- A branch connects from `start` wire node to `end` wire node.
- A branch may contain symbols in branch `cells[start]` through `cells[end - 1]`.
- Branches connect wire-node to wire-node, not symbol-center to symbol-center.

This is the most important rule in the format. It makes the saved project behave like an electrical/PLC power-flow graph instead of a drawing-only representation.

---

## Ladder rung

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

## Script rung

Script rungs allow raw AngelScript to live in the same ordered scan list as ladder rungs.

```json
{
  "id": "s1",
  "kind": "script",
  "comment": "Custom AngelScript rung",
  "code": "Q_Debug = I0_Auto && !I1_Stop;"
}
```

| Field | Type | Required | Description |
|---|---:|---:|---|
| `id` | string | yes | Unique editor/runtime identifier for this rung. |
| `kind` | string | yes | Must be `script`. |
| `comment` | string | yes | Human-readable rung comment. May be empty. |
| `code` | string | yes | AngelScript body emitted inside `scan()`. |

---

## Branch object

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

The JSON Schema checks the basic branch shape. The editor's semantic validation also checks that `end > start` and that symbols are not placed outside the branch span.

---

## Symbol object

All ladder symbols share a common shape:

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
| `type` | string | yes | One of `NO`, `NC`, `OUT`, `TON`, `TOF`, `CTU`, `CTD`. |
| `tag` | string | yes | AngelScript-compatible identifier. |
| `preset` | number | required for timers/counters | Timer preset in ms or counter preset count. |
| `resetTag` | string | optional | Counter control Boolean expression. For `CTU` it resets `CV` to `0`; for `CTD` it reloads `CV` from `preset`. |

Valid tag names must match:

```text
^[A-Za-z_][A-Za-z0-9_]*$
```

Examples:

```text
I0
Q0_Motor
T_OverTemp
C_Parts
```

Invalid examples:

```text
0_Start
Motor Run
Q-Alarm
```

---

## Symbol semantics

| Type | Meaning | Transpiler behavior |
|---|---|---|
| `NO` | Normally-open contact | Passes power when `tag` is true. |
| `NC` | Normally-closed contact | Passes power when `tag` is false. |
| `OUT` | Output coil | Does not block power flow. Rung result writes to `tag`. |
| `TON` | On-delay timer | Updates from power reaching timer input. Contact condition is `tag.Q()`. |
| `TOF` | Off-delay timer | Updates from power reaching timer input. Contact condition is `tag.Q()`. |
| `CTU` | Count-up counter | Counts rising edges of input power, clamps `CV` at `preset`, resets to `0` when `resetTag` is true. Contact condition is `tag.Q()`. |
| `CTD` | Count-down counter | Counts down on rising edges of input power, initializes/reloads `CV` from `preset` when `resetTag` is true. Contact condition is `tag.Q()`. |

---

## Counter reset expressions

Counter reset expressions are intentionally limited to a safe Boolean subset:

- identifiers
- `true`
- `false`
- `!`
- `&&`
- `||`
- parentheses
- whitespace

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

---

## Example project

```json
{
  "schema": "pilab.ladder.project",
  "schema_version": 1,
  "name": "Example PiLab Ladder Project",
  "scan_ms": 5,
  "rungs": [
    {
      "id": "r1",
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
          "id": "b1",
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
      "id": "s1",
      "kind": "script",
      "comment": "Custom debug logic",
      "code": "Q_Debug = I0_Start && !I1_Stop;"
    }
  ]
}
```

---

## Validation layers

The project now has two validation layers:

1. **Schema validation** checks the file shape: required fields, rung kinds, slot counts, symbol types, and tag identifier format.
2. **Editor semantic validation** checks PLC/editor-specific rules: duplicate coils, invalid branch spans, symbols outside branch span, invalid presets, suspicious empty branches, and complex overlapping branches.

The schema protects the file format. The semantic validator protects the ladder logic meaning.

---

## Test commands

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
