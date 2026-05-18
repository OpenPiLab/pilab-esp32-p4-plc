# PiLab SFC Type Editor Mockup

A Vite + Vue + Tailwind mockup for a PiLab Sequential Function Chart type editor.

## What it includes

- Industrial dark PiLab-style layout
- SFC type and default instance settings
- Multiple sequencers with priorities and start conditions
- Siemens-inspired step phases: initialization, cyclic processing, termination
- Entry guards, active step monitors, min/max runtime, timeout-oriented fields
- Transitions with source/target steps, conditions, priority, transition actions, and OS comments
- Characteristics model: control strategies, setpoints, process values, parameters, bit memory, timers, note texts, position texts, block contacts
- Validation panel for common authoring hazards
- JSON export/import/copy
- Future AngelScript callback skeleton preview

## Run

```bash
npm install
npm run dev
```

## Build

```bash
npm run build
```

## Design intent

This is not a finished SFC engine. It is a front-end JSON IR mockup intended to feed a future PiLab transpiler or C++ SFC runtime that manages sequencing while AngelScript supplies step/transition callbacks.
