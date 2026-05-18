# PiLab Ladder Editor

PiLab Ladder Editor is a browser-based ladder logic editor and simulator for the PiLab PLC / HMI / edge-controller project.

It is built around a simple idea: a small controller should be able to carry its own programming environment. A user can open a browser, create PLC-style logic, simulate it immediately, watch tags change, mix ladder with script logic where appropriate, and export or deploy the result to a PiLab runtime.

This project is not a certified industrial PLC environment. It is an open-source automation learning, prototyping, simulation, and experimentation tool for students, hobbyists, researchers, test engineers, and small-machine experimentation where a lightweight zero-install workflow is more important than vendor certification.

## What this project does

Current capabilities include:

- Browser-based ladder editing
- Live ladder simulation
- Boolean, numeric, and string simulator tags
- Editable simulator watch table
- Tag filtering and starred watch mode
- Ladder power-flow visualization
- Mixed ladder and script rungs in the same scan sequence
- Export to PiLab project JSON
- Export to AngelScript for the PiLab runtime
- Export to self-contained JavaScript for browser or Node.js simulation
- Automated tests for core ladder simulation, schema validation, and transpiler behavior

The editor currently uses these project symbol types:

```text
NO      normally open contact
NC      normally closed contact
OUT     output coil
SET     latched set coil
RST     reset/unlatch coil
ONS     one-shot rising edge
TON     on-delay timer
TOF     off-delay timer
CTU     count-up counter
CTD     count-down counter
```

Script rungs use AngelScript-style syntax and are emitted into the generated AngelScript `scan()` function. The JavaScript simulator supports a practical subset of script-rung behavior including assignment, math, comparisons, simple `if` blocks, numeric tags, and common math helpers.

## Why this exists

Traditional PLC development often requires vendor software, licensing, specific hardware, and a large installation before a beginner can experiment. PiLab takes a lighter path:

```text
Open browser
Draw ladder
Simulate logic
Watch tags
Edit numeric values live
Add script where ladder is awkward
Export code
Run on a PiLab device
```

That makes the project useful as a low-cost automation learning and prototyping environment. It also makes the project interesting for AI-assisted logic generation because the native project file is compact JSON instead of a proprietary binary format.

## Current project status

PiLab Ladder Editor is an active experimental project. The core editor and simulator are useful now, but the project should still be treated as an alpha / research / learning tool.

Good current uses:

- PLC education
- Ladder logic experimentation
- Browser-based simulation
- Small automation prototypes
- Test fixture logic
- Edge-controller logic experiments
- AI-assisted logic generation experiments
- PiLab runtime program generation
- Node.js simulation of exported programs

Not recommended yet for:

- Safety-critical control
- Certified industrial machinery
- Emergency-stop safety logic
- Applications requiring SIL, PL, UL, CSA, CE, or vendor support guarantees
- Motion-control systems where deterministic hard real-time behavior is required

## Core concepts

### Ladder rungs

Ladder rungs are visual power-flow logic. Contacts and function blocks evaluate left-to-right and coils write to tags. Typical ladder use cases include start/stop latches, motor interlocks, alarm latches, fan off-delays, part counters, and one-shot event pulses.

### Script rungs

Script rungs are text-based logic rungs that run in the same scan order as ladder rungs. They are useful for math, scaling, clamping, numeric comparisons, recipe calculations, compact state machines, and edge-controller glue logic.

Example script rung:

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

### Tags

Tags are named values used by both ladder and script rungs. Simulator tags may be Boolean, numeric, or string values. Boolean tags are useful for inputs, outputs, memory bits, interlocks, and alarms. Numeric tags are useful for setpoints, analog inputs, scaled values, counter values, recipe parameters, and script logic.

### JSON project model

The saved project format is JSON. This makes logic easy to save, diff, generate, inspect, test, and transform.

```json
{
  "schema": "pilab.ladder.project",
  "schema_version": 1,
  "name": "Example Project",
  "scan_ms": 5,
  "rungs": []
}
```

The scan order is the order of the `rungs` array.

### Export targets

The editor can export logic to:

- **JSON** — native project format
- **AngelScript** — intended for PiLab runtime execution
- **JavaScript** — self-contained simulator/runtime target for browser or Node.js use

The JavaScript export is useful for automated testing, demos, and simulation outside the editor.

## Documentation

- [Project JSON Schema documentation](docs/LADDER_PROJECT_SCHEMA.md)
- [AI project generation guide](docs/PILAB_AI_GENERATION_GUIDE.md)
- [Machine-readable JSON Schema](schema/pilab-ladder-project.schema.json)
- [Mixed ladder/script example project](examples/mixed_ladder_script_example.json)

## JavaScript export example

The generated JavaScript file is self-contained and can be imported into Node.js.

```js
import {
  createPiLabLadderProgram,
  PiLabSimContext,
  runPiLabLadderScans
} from "./pilab_ladder_generated.mjs";

const ctx = new PiLabSimContext({
  HmiSpeedSetpoint: 250,
  SpeedTrim: 0.5,
  MinSpeed: 20,
  MaxSpeed: 100,
  RawAI0: 2048,
  RawMin: 0,
  RawMax: 4096
});

const program = createPiLabLadderProgram(ctx);
runPiLabLadderScans(program, 1);

console.log(program.ctx.tags);
```

## Script rung simulator subset

The JavaScript simulator supports a practical deterministic subset of AngelScript-like statements:

```text
Assignments:            A = B;
Compound assignments:   A += 1; A -= 1; A *= 2; A /= 2;
Increment/decrement:    A++; A--;
if blocks:              if (...) { ... } else if (...) { ... } else { ... }
Local variables:        bool/int/uint/float/double/string/auto name = expression;
Math:                   + - * / %
Comparisons:            > >= < <= == !=
Boolean logic:          && || !
Math helpers:           min max abs sqrt sin cos tan atan2 floor ceil round pow exp log
Function-block reads:   Timer.Q() Timer.ET() Counter.CV()
```

Current simulator limitations:

```text
No full AngelScript parser
No user-defined functions in script rungs
No classes
No arrays
No loops
No switch/case
No multi-file script project model
Limited compile-time diagnostics
```

The goal is not to recreate the entire AngelScript language in the browser. The goal is to simulate the practical subset needed for PLC-style math, comparison, scaling, and small state logic.

## Development setup

Install Node.js 20 LTS or newer.

```bash
npm install
npm run dev
```

Run tests:

```bash
npm run test
```

Build production files:

```bash
npm run build
```

## Suggested project structure

```text
src/
  App.vue
  main.js
  style.css
  components/
    SymbolRender.vue
  ladder/
    ladderModel.js
    ladderValidation.js
    ladderTranspiler.js
    ladderSimulator.js

test/
  ladderSchema.test.js
  ladderSimulator.test.js
  ladderTranspiler.test.js

docs/
  LADDER_PROJECT_SCHEMA.md
  PILAB_AI_GENERATION_GUIDE.md

schema/
  pilab-ladder-project.schema.json

examples/
  mixed_ladder_script_example.json
```

## Safety note

PiLab Ladder Editor is not a safety system. Do not use it as the only layer of protection for machinery or systems that can injure people or damage property.

Physical systems should use appropriate independent safety hardware, emergency stops, fusing, interlocks, watchdogs, and fail-safe design. Treat PiLab as an automation logic and experimentation platform, not as a certified safety controller.

## Project philosophy

PiLab is not trying to imitate a large vendor PLC environment one-to-one. It explores a lighter model:

```text
PLC-style logic
+ browser-based tools
+ live simulation
+ script extensibility
+ web HMI
+ open JSON project format
+ low-cost embedded runtime
```

The long-term goal is to make control logic easier to create, inspect, simulate, explain, and run.

## License

MIT Licensed
