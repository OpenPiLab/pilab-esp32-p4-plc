# PiLab Ladder Docs Source-Code Validation Report

Validated against the uploaded current Ladder Editor source zip.

## Source files inspected

```text
src/ladder/ladderModel.js
src/ladder/ladderSchema.js
src/ladder/ladderValidation.js
src/ladder/ladderSimulator.js
src/ladder/ladderTranspiler.js
test/ladderSchema.test.js
test/ladderSimulator.test.js
test/ladderTranspiler.test.js
package.json
README.md
docs/LADDER_PROJECT_SCHEMA.md
schema/pilab-ladder-project.schema.json
```

## Confirmed against implementation

- Project shape uses `name`, `scan_ms`, and ordered `rungs`.
- Formal metadata is `schema: "pilab.ladder.project"` and `schema_version: 1`.
- Ladder rungs use `kind: "ladder"`, exactly 8 `main` slots, and a `branches` array.
- Script rungs use `kind: "script"` and `code`.
- Branches use wire-node `start` and `end`, plus exactly 8 `cells`.
- Branch symbols outside `cells[start]` through `cells[end - 1]` are rejected by the JavaScript shape validator.
- Supported JSON symbol types are `NO`, `NC`, `OUT`, `SET`, `RST`, `ONS`, `TON`, `TOF`, `CTU`, and `CTD`.
- The source does not use `COIL` or `RESET` as project JSON symbol types.
- `TON`, `TOF`, `CTU`, and `CTD` require positive `preset`.
- `ONS` is stateful but does not require a preset.
- `CTU` reset and `CTD` reload use `resetTag`; the simulator/transpiler also accept legacy aliases `resetExpr`, `loadTag`, and `loadExpr`.
- Generated JavaScript exports `createPiLabLadderProgram`, `PiLabSimContext`, `runPiLabLadderScans`, and `createAndRunPiLabLadder`.
- Script rungs are passed through directly into AngelScript export and mini-transpiled for JavaScript/browser simulation.

## Validation performed

- Parsed the uploaded source tree.
- Imported `validateLadderProjectShape()` from the actual source and validated `examples/mixed_ladder_script_example.json`: no shape errors.
- Ran the actual semantic validator mixin against the example: no semantic issues.
- Ran the actual JavaScript transpiler against the example and confirmed the generated module imports and executes in Node.
- Validated `schema/pilab-ladder-project.schema.json` with JSON Schema Draft 2020-12.
- Validated `examples/mixed_ladder_script_example.json` against the JSON Schema.

## Notes

The uploaded source README still used older user-facing words `COIL` and `RESET`, but the actual implementation and schema use `OUT` and `RST`. The corrected README in this package uses `OUT` and `RST`.

The implementation's semantic validator checks duplicate `TON`, `TOF`, `CTU`, and `CTD` instances. AI-generated projects should also keep `ONS` instance tags unique, even though `ONS` does not require a preset and duplicate `ONS` instances are not currently rejected by the validator.

The full Vitest suite could not be executed in this sandbox because the uploaded zip did not include `node_modules`, and `vitest` was not installed. The source `package.json` defines the expected commands:
`npm run test`, `npm run test:watch`, `npm run build`, and `npm run preview`.
