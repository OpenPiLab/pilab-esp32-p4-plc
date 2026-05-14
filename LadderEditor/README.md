# PiLab Ladder Editor

Current UI test version: `v0.1.11-reset-visible` — Vite/Vue Step 3 Transpiler Tests

This is the Step 3 baseline of the PiLab ladder editor. It keeps the UI and behavior close to the original standalone HTML prototype, keeps the Step 2 module split, and adds a real automated test harness for the ladder-to-AngelScript transpiler.

## Current structure

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
  ladderTranspiler.test.js
```

## What changed in this pass

- Added Vitest.
- Added `npm run test` and `npm run test:watch`.
- Added transpiler coverage for:
  - simple series NO/NC contacts
  - output coil assignment
  - classic start/stop seal-in circuit
  - same-span parallel branches
  - crossing branch graph fallback
  - TON update conditions
  - TOF update conditions
  - CTU reset expressions
  - CTD reset expressions
  - AngelScript rung passthrough
- Verified the app still builds with `npm run build`.

The logic modules are still exported as Vue method mixins for compatibility. The tests use a small harness object that combines the model and transpiler methods. This keeps the test pass low-risk while giving us protection before deeper refactoring.

## Requirements

Install Node.js 20 LTS or newer.

## Install

```bash
npm install
```

## Run locally

```bash
npm run dev
```

Then open the local URL printed by Vite, usually:

```text
http://localhost:5173/
```

## Run tests

```bash
npm run test
```

For watch mode:

```bash
npm run test:watch
```

## Build

```bash
npm run build
```

The compiled app will be in:

```text
dist/
```

## Preview the production build

```bash
npm run preview
```

## Suggested next step

Step 4 should make the JSON project schema official. The schema should describe:

- project root fields
- ladder rung fields
- script rung fields
- symbol fields
- branch wire-node fields
- timer/counter fields
- versioning/migration rules

After that, the next cleanup should convert the transpiler from Vue method mixins into pure functions while keeping these tests green.
