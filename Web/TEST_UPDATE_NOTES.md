# Metadata Regression Test Update

Added regression coverage for the PiLabParam/PiLabMonitor web workflow:

- AngelScript generation emits PiLabParam/PiLabMonitor for exposed TON and CTU block parameters.
- AngelScript generation applies exposed params through SetPreset(...).
- JavaScript simulator output stays sandboxed and metadata-free.
- Browser simulator updates timer internals from its own sim context even when metadata params are enabled.
- Tag registry discovers writable parameter tags and read-only monitor tags for HMI/live PLC mode.

Validation performed:

- npm test: 7 test files passed, 65 tests passed.
- npm run build: succeeded. The existing Vite chunk-size warning still appears and is not a build failure.
