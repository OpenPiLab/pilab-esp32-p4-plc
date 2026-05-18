# PiLab Ladder Editor

PiLab Ladder Editor is a browser-based ladder logic editor and simulator for the PiLab PLC / HMI / edge-controller project.

It is designed around a simple idea: a small controller should be able to carry its own programming environment. A user should be able to open a browser, create PLC-style logic, simulate it immediately, watch tags change, mix ladder with script logic where appropriate, and then export or deploy the result to a PiLab runtime.

This project is not intended to be a certified industrial PLC environment. It is an open-source automation learning, prototyping, simulation, and experimentation tool. It is especially useful for students, hobbyists, researchers, test engineers, and small-machine experimentation where a lightweight zero-install workflow is more important than vendor certification.

## What this project does

The current editor supports:

- Browser-based ladder editing
- Live ladder simulation
- Boolean and numeric simulator tags
- Editable simulator watch table
- Tag filtering and starred watch mode
- Ladder power-flow visualization
- Mixed ladder and script rungs in the same scan sequence
- Export to PiLab-style JSON
- Export to AngelScript for the PiLab runtime
- Export to self-contained JavaScript for browser or Node.js simulation
- Automated tests for core ladder simulation and transpiler behavior

The editor currently includes these ladder instructions:

```text
NO      normally open contact
NC      normally closed contact
COIL    output coil
SET     latched set coil
RESET   reset coil
ONS     one-shot rising edge
TON     on-delay timer
TOF     off-delay timer
CTU     count-up counter
CTD     count-down counter
```

In addition to ladder rungs, the editor supports **script rungs**. Script rungs use AngelScript-style syntax and are emitted into the generated AngelScript `scan()` function. The JavaScript simulator also supports a practical subset of script-rung behavior, including assignment, math, comparison, simple `if` blocks, numeric tags, and several common math helpers.

## Why this exists

Traditional PLC development often requires vendor software, licensing, specific hardware, and a large installation before a beginner can even experiment. PiLab takes a different approach:

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

That makes the project useful as a low-cost automation learning and prototyping environment.

A student can learn PLC concepts without buying a commercial PLC. A hobbyist can build simple control logic without first becoming an embedded C++ developer. A researcher can use the JSON model, simulator, JavaScript export, and physical runtime as a small cyber-physical experimentation platform.

## Current project status

This is an active experimental project. The core editor and simulator are useful now, but the project should still be treated as an alpha / research / learning tool.

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

### 1. Ladder rungs

Ladder rungs are visual power-flow logic. Contacts and function blocks evaluate left-to-right and coils write to tags.

Example applications:

- Start/stop latch
- Motor interlock
- Alarm latch/reset
- Fan off-delay
- Part counter
- Batch complete signal
- One-shot event pulse

### 2. Script rungs

Script rungs are text-based logic rungs that run in the same scan order as ladder rungs.

They are useful for logic that is awkward to express visually, such as:

- Math
- Scaling
- Clamping
- Numeric comparisons
- Small state machines
- Recipe calculations
- Edge-controller glue logic
- Data movement

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

### 3. Tags

Tags are named values used by both ladder and script rungs. Tags may be boolean, numeric, or string values in the simulator.

Boolean tags are useful for:

- Inputs
- Outputs
- Memory bits
- Interlocks
- Alarms

Numeric tags are useful for:

- Setpoints
- Analog inputs
- Scaled values
- Counter values
- Recipe parameters
- Timer/counter presets in script logic

The simulator tag panel allows values to be edited while the simulator is running. This makes it possible to change setpoints and raw inputs live while watching the logic respond.

### 4. JSON project model

The saved project format is JSON. This is important because it makes the logic easy to save, diff, generate, inspect, and transform.

A project contains an ordered list of rungs:

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

### 5. Export targets

The editor can export logic to:

- **JSON** — native project format
- **AngelScript** — intended for PiLab runtime execution
- **JavaScript** — self-contained simulator/runtime target for browser or Node.js use

The JavaScript export is useful for automated testing, demos, and simulation outside the editor.

## Practical examples

### Example 1: Start/stop motor latch

A typical motor latch uses a start button, stop button, and output coil.

Conceptually:

```text
Start pressed OR Motor already running
AND Stop not pressed
THEN MotorRun = true
```

This is a classic ladder learning example because it teaches scan order, latching, normally closed contacts, and output memory.

Practical uses:

- Small pump control
- Fan enable
- Conveyor enable
- Test fixture cycle enable

### Example 2: Fan off-delay using TOF

Use a TOF block when an output should remain on briefly after the command turns off.

Example behavior:

```text
RunCmd turns on  -> FanOutput turns on
RunCmd turns off -> FanOutput remains on for preset time
Delay expires    -> FanOutput turns off
```

Practical uses:

- Cooling fan after motor stop
- Ventilation purge delay
- Light delay-off
- Conveyor runout delay

### Example 3: Batch done using CTU

A CTU counter counts rising edges. When the count reaches the preset, its done output stays true until reset.

Example behavior:

```text
PartSensor pulses 3 times
C_Parts reaches preset
BatchDone turns on
BatchDone stays on until ResetParts is true
```

Practical uses:

- Part counting
- Batch completion
- Reject counting
- Cycle counting

### Example 4: Numeric scaling using a script rung

Script rungs are useful for analog-style math.

```cpp
float span = RawMax - RawMin;

if (span > 0.0f) {
    TankLevelPct = ((RawAI0 - RawMin) * 100.0f) / span;
} else {
    TankLevelPct = 0.0f;
}

TankLevelPct = max(0.0f, min(100.0f, TankLevelPct));
```

Example simulator values:

```text
RawAI0 = 2048
RawMin = 0
RawMax = 4096
```

Expected result:

```text
TankLevelPct = 50
```

Practical uses:

- Analog input scaling
- Tank level calculation
- Temperature conversion
- Pressure scaling
- Sensor normalization

### Example 5: Mixed ladder and script logic

A practical program might use ladder for the visible machine interlocks and script for numeric calculations.

Ladder:

```text
AutoMode AND GuardClosed AND EStopOK -> MachineEnable
```

Script:

```cpp
TargetSpeed = HmiSpeedSetpoint * SpeedTrim;

if (!MachineEnable) {
    TargetSpeed = 0.0f;
}
```

This mixed approach keeps discrete control readable while avoiding awkward ladder networks for simple math.

### Example 6: Simple step sequence using script

Small state machines can be clearer in script than in pure ladder.

```cpp
if (ResetCycle) {
    Step = 0;
}

if (StartCycle && Step == 0) {
    Step = 10;
}

if (Step == 10) {
    ClampOutput = true;
    if (ClampMade) {
        Step = 20;
    }
}

if (Step == 20) {
    DrillOutput = true;
    if (DrillDone) {
        Step = 30;
    }
}

if (Step == 30) {
    ClampOutput = false;
    DrillOutput = false;
    CycleDone = true;
}
```

Practical uses:

- Small fixture sequence
- Test stand cycle
- Clamp/drill/release demo
- Educational state-machine example

## JavaScript export example

The generated JavaScript file is self-contained and can be imported into Node.js.

Example usage:

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

This makes it possible to test ladder/script behavior outside the editor.

## AngelScript export notes

The generated AngelScript is intended to run inside the PiLab runtime environment.

Important notes:

- Script rungs are emitted into the generated `scan()` function.
- Tags referenced in script rungs must exist in the PiLab runtime or be provided by the runtime binding layer.
- The editor does not currently perform full AngelScript syntax validation.
- The browser simulator supports a practical subset of script-rung syntax, not the full AngelScript language.

## Script rung simulator subset

The JavaScript simulator currently supports practical script-rung behavior such as:

- Assignment: `A = B;`
- Math: `+`, `-`, `*`, `/`, `%`
- Comparisons: `>`, `>=`, `<`, `<=`, `==`, `!=`
- Boolean logic: `&&`, `||`, `!`
- Compound assignment: `A += 1;`
- Increment/decrement: `A++;`, `A--;`
- Simple local variables: `float span = RawMax - RawMin;`
- `if`, `else if`, and `else` blocks with braces
- Common math helpers such as `min`, `max`, `abs`, `sqrt`, `sin`, and `cos`
- Function block reads such as `Timer.Q()`, `Timer.ET()`, and `Counter.CV()`

Current limitations:

- No full AngelScript parser
- No user-defined functions in script rungs
- No classes
- No arrays
- No loops
- No `switch`
- No multi-file script project model
- Limited compile-time diagnostics

The goal is not to recreate the entire AngelScript language in the browser. The goal is to simulate the practical subset needed for PLC-style math, comparison, scaling, and small state logic.

## Simulator workflow

1. Create ladder rungs and/or script rungs.
2. Open the simulator panel.
3. Use the tag list to edit boolean and numeric values.
4. Use the filter box to find important tags.
5. Star tags you want to monitor.
6. Enable watch mode to show only starred tags.
7. Run the simulator.
8. Edit inputs and numeric setpoints live while watching outputs and internal tags.

This is especially useful for testing logic before deploying it to hardware.

## Suggested learning path

For someone new to PLC programming:

1. Create a single NO contact driving a coil.
2. Add an NC stop contact.
3. Build a start/stop latch.
4. Add a TON timer.
5. Add a TOF timer.
6. Add a CTU counter.
7. Add a reset rung.
8. Add a script rung for numeric scaling.
9. Use the watch table to monitor internal tags.
10. Export to JavaScript and run the logic in Node.js.
11. Export to AngelScript and run it on a PiLab device.

## Development setup

Install Node.js 20 LTS or newer.

Install dependencies:

```bash
npm install
```

Run the development server:

```bash
npm run dev
```

Then open the local URL printed by Vite, usually:

```text
http://localhost:5173/
```

Run tests:

```bash
npm run test
```

Run tests in watch mode:

```bash
npm run test:watch
```

Build production files:

```bash
npm run build
```

Preview production build:

```bash
npm run preview
```

## Project structure

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

schema/
  pilab-ladder-project.schema.json
```

## Current limitations and future work

The current version is useful, but still early.

Important future improvements include:

- Explicit tag declaration table
- Better tag type management
- Better script-rung error reporting
- More complete validation before export
- More simulator diagnostics
- Scan stepping and breakpoints
- Better branch editing tools
- Project version migration tools
- Import/export compatibility with other PLC formats
- Expanded HMI integration
- Deployment workflow directly to PiLab runtime
- Optional documentation generator for projects
- More test coverage for complex mixed ladder/script projects

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

The result is a small, accessible automation platform that can be used for learning, prototyping, research, and practical control experiments.

The long-term goal is to make control logic easier to create, inspect, simulate, explain, and run.

## License

MIT Licensed
