# PiLab Timing Best Practices

PiLab supports both deterministic PLC-style execution and more flexible edge-runtime execution modes.

Because PiLab supports scan coalescing and overloaded runtime behavior, it is important to understand the difference between:

- Scan-count-based timing
- Elapsed-time-based timing

---

# Traditional PLC / Scan Count Timer

This is the classic PLC timing approach.

Assumption:

```text
Every scan occurs exactly every 5 ms forever.
```

Example:

```cpp
// Blink output every 1 second assuming 5 ms scan

int counter = 0;

void scan()
{
    counter++;

    // 200 scans * 5 ms = 1000 ms
    if(counter >= 200)
    {
        counter = 0;

        Q0 = !Q0;
    }
}
```

This works correctly if:

- scan timing is deterministic
- no scans are skipped
- no coalescing occurs
- execution time always remains below the configured scan period

---

# Problem With Coalescing

Suppose:

```text
Configured scan = 5 ms
Actual overloaded runtime = 10 ms effective scan period
```

Now:

```text
200 scans * 10 ms = 2 seconds
```

The timer still works logically, but the timing is no longer accurate.

This is why scan-count timing becomes invalid when:

- coalescing is enabled
- scans are skipped
- runtime overload occurs
- edge-task behavior is allowed

---

# Elapsed-Time-Based Timing (Recommended)

Instead of assuming how much time passed, measure the actual elapsed time between scans.

Example:

```cpp
// Blink output every 1 second using actual elapsed time

float elapsed_ms = 0.0f;

void scan()
{
    // Add actual elapsed runtime delta
    elapsed_ms += PLC_DeltaTimeMs;

    if(elapsed_ms >= 1000.0f)
    {
        elapsed_ms -= 1000.0f;

        Q0 = !Q0;
    }
}
```

This timing remains accurate even when:

- scans are coalesced
- runtime is overloaded
- execution time varies
- scan periods change dynamically

---

# Why This Is Better

Instead of asking:

```text
"How many scans occurred?"
```

Elapsed-time timing asks:

```text
"How much real time actually passed?"
```

That distinction becomes important once the runtime allows:

- coalescing
- hot swapping
- edge workloads
- intentional degraded timing modes

---

# Comparison

## Scan Count Method

Characteristics:

```text
simple
traditional PLC style
fast
works only with deterministic scan timing
```

---

## Elapsed Time Method

Characteristics:

```text
accurate under overload
accurate under coalescing
accurate under jitter
compatible with edge-runtime behavior
```

---

# Recommended Usage

## Deterministic PLC Workloads

The scan-count method is acceptable for:

- simple machine sequencing
- relay logic
- short deterministic control loops
- systems where overload is treated as a fault

---

## Edge / Compute / Flexible Workloads

Elapsed-time timing is strongly recommended for:

- image processing
- uploads
- analytics
- AI inference
- cloud-connected automation
- mixed PLC + edge workloads

Use:

```cpp
PLC_DeltaTimeMs
PLC_DeltaTimeUs
```

for timing calculations.

---

# Important Design Insight

PiLab intentionally supports two execution philosophies:

## Deterministic PLC Model

```text
Execution must complete within the fixed scan period.
```

## Edge Runtime Model

```text
The scheduler cadence is fixed,
but execution completion may legally exceed the cadence.
```

This is conceptually similar to the distinction used in game engines and simulation runtimes:

```text
fixed timestep logic
vs
delta-time-based logic
```

Traditional PLCs historically only required fixed timestep logic because overload conditions were usually considered fatal errors.

PiLab intentionally supports both execution models.
