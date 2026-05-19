# PiLab Web + Ladder Editor integration preview

This zip imports the Ladder Editor into the PiLab Web Interface as a new routed page.

## What changed

- Added `src/pages/LadderEditor.vue`, based on the Ladder Editor `App.vue`.
- Added Ladder Editor support files:
  - `src/components/ladder/SymbolRender.vue`
  - `src/ladder/ladderModel.js`
  - `src/ladder/ladderSchema.js`
  - `src/ladder/ladderSimulator.js`
  - `src/ladder/ladderTagRegistry.js`
  - `src/ladder/ladderTranspiler.js`
  - `src/ladder/ladderValidation.js`
- Added `/ladder` route in `src/router.js`.
- Added `Ladder` link to the main app navigation in `src/components/AppShell.vue`.
- Removed the standalone full app header from the embedded Ladder page and replaced it with a smaller embedded toolbar.
- Scoped the Ladder Editor custom CSS so its `.panel`, `.btn`, `.mono`, `.glass`, and scrollbar styles do not accidentally override the rest of the Web Interface.

## Current state

This is intentionally a first integration pass. The Ladder page should load inside the Web Interface and keep its local save/load, simulation, JSON, AngelScript, JavaScript, and tag registry export features.

It does not yet sync tags to `/api/tags` and does not yet upload generated AngelScript through the Web Interface script API. Those should be the next two integration steps after confirming the page behaves correctly inside the host app.

## Build check

`npm run build` completed successfully with this integrated version.

## 2026-05-18 Tag Registry replacement preview

This preview replaces the original Web Interface `src/pages/TagRegistry.vue` with a page built around the Ladder Editor tag registry methods from `src/ladder/ladderTagRegistry.js`.

What changed:

- `/tags` now uses the Ladder Editor tag registry model for normalization, filtering, editable metadata, status badges, import/export, and delete/reset behavior.
- The page still loads from and saves to the existing Web Interface `/api/tags` endpoint through `src/api/tagApi.js`.
- Manual tag creation was added directly to the page so it can still act as a normal PLC tag registry even when no Ladder project is open.
- The page subscribes to `plcStore.usePlcData()` and shows live `/api/plc_data` runtime points for comparison with the saved registry.
- Save is disabled when `flashWritesAllowed` is false, matching the existing Web Interface flash-write policy.

What is intentionally not done yet:

- The Ladder page and Tag page do not yet share a live global tag store.
- Ladder-discovered tags are not automatically pushed to `/api/tags` yet.
- Generated ladder AngelScript upload is still not wired to the Web app script upload API.

Recommended next step:

- Add a shared tag store/composable so the Ladder page can load the current `/api/tags` registry and merge ladder-discovered tags into it before saving/uploading.
