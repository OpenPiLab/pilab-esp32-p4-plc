# PiLab PLC Runtime Overrun Policies

PiLab supports multiple runtime execution policies that control what happens when an AngelScript scan exceeds its configured scan period.

This allows PiLab to operate as either:

- A more traditional deterministic soft PLC
- A flexible edge controller capable of handling heavier compute workloads

---

# Why This Exists

PiLab executes user scripts at a fixed interval.

Example:

```text
Configured script scan = 5 ms
```

If the script takes longer than 5 ms to execute, the runtime enters an overrun condition.

Traditional PLC systems usually treat this as a fault.

PiLab optionally supports more flexible behaviors for workloads such as:

- image processing
- analytics
- uploads
- protocol translation
- cloud communication
- edge AI workloads

---

# Important Concept: Coalescing

When the runtime is overloaded, multiple pending script scan triggers can accumulate.

Instead of allowing an infinite backlog of delayed scans, PiLab can optionally:

```text
discard stale pending scans
continue immediately with the newest scan
```

This behavior is called:

```text
coalescing
```

This prevents:

- watchdog starvation
- runaway scan backlogs
- system lockups
- unstable timing collapse

---

# Runtime Policies

## Coalesce and continue

Discard stale pending scans and continue running.

## Warn and continue

Only raise warnings and diagnostic counters. Runtime continues regardless of overload.

## Fault after limit

Allow temporary overruns. Fault only after sustained overload.

## Stop immediately

First detected overrun stops the PLC runtime immediately.

## Edge task / coalesce indefinitely

Allow indefinite coalescing without faulting.

---

# Runtime Diagnostic Tags

## PLC_ScanExecutionTimeUs

Actual script execution time in microseconds.

## PLC_ScanLoadPercent

Approximate runtime utilization.

## PLC_ScanOverrunActive

Indicates the current scan exceeded its configured time budget.

## PLC_ScanFaultActive

Indicates the runtime entered a fault state due to sustained overload.

## PLC_ScanCoalescedCount

Counts how many stale script scans were discarded.

## PLC_DeltaTimeUs / PLC_DeltaTimeMs

Actual elapsed time between script executions.

---

# Timing Best Practices

Avoid scan-count-based timers.

Prefer elapsed-time-based logic using PLC_DeltaTimeMs.

---

# Important Design Philosophy

PiLab intentionally supports multiple runtime execution models.

## Deterministic PLC Model

Execution must complete within the fixed scan period.

## Edge Runtime Model

The scheduler cadence is fixed, but execution completion may legally exceed the cadence.
