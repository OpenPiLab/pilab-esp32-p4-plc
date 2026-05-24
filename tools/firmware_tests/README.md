# PiLab Firmware Host Metadata Tests

These tests run on Windows using the Visual Studio Developer Command Prompt. They do not flash the ESP32-P4 and do not require ESP-IDF.

Run from the firmware project root:

```bat
run_firmware_metadata_tests.bat
```

The tests currently cover the host-testable part of the firmware metadata system:

- `PiLabParam` parsing
- `PiLabMonitor` parsing
- metadata stripping before AngelScript compilation
- generated monitor wrapper output
- `scan()` and `Scan(float dt)` wrapper behavior
- invalid metadata rejection
- class-level monitor metadata being documentation-only for now

These tests intentionally focus on `PiLabScriptBuilder`, because it is ordinary C++ string parsing/code generation and is easy to test on Windows. Runtime tag table tests should be added later either as ESP-IDF Unity device tests or by splitting the tag table core into a host-testable module.
