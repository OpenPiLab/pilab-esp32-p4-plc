# PiLab Philosophy

PiLab is a browser-first industrial automation runtime that combines a soft PLC, edge controller, scripting engine, HMI server, and development environment into a single embedded device.

The simplest way to describe PiLab is:

```text
Imagine if a PLC, a lightweight SCADA system,
a scripting runtime, and a web IDE all lived together
inside one small controller with no installation required.
```

---

# Why PiLab Exists

Traditional industrial automation systems are powerful, but they are often:

- expensive
- fragmented
- proprietary
- configuration-heavy
- slow to iterate
- difficult to experiment with

PiLab was created to explore a different approach:

```text
open browser
write logic
upload instantly
observe live behavior
modify in realtime
```

The controller itself hosts the entire experience.

No IDE installation.
No runtime licensing.
No Windows dependency.
No separate HMI package.

---

# What Makes PiLab Different

PiLab intentionally combines several worlds that are usually separated:

| Capability | Traditional Systems |
|---|---|
| PLC Runtime | Separate |
| HMI/SCADA | Separate |
| Scripting | Separate |
| Web Interface | Separate |
| File Storage | Limited |
| Runtime Diagnostics | Often hidden |
| Edge Compute | Separate device |
| Development Environment | External PC software |

PiLab integrates all of these into one live runtime environment.

---

# Browser-First Automation

With PiLab you can:

- Open a browser
- Connect directly to the controller
- Write machine logic
- Upload and run scripts live
- Monitor tags in realtime
- Build HMIs directly on the device
- Store multiple scripts and assets locally
- Observe timing and diagnostics live

The workflow is intentionally immediate and interactive.

---

# Deterministic Control + Edge Compute

Traditional PLCs focus almost entirely on deterministic machine control.

Modern industrial systems increasingly need to combine:

- machine control
- networking
- vision systems
- analytics
- uploads
- cloud APIs
- diagnostics
- scripting

PiLab intentionally supports both:

## Deterministic PLC Model

```text
Execution must complete within the configured scan period.
```

## Edge Runtime Model

```text
The scheduler cadence is fixed,
but execution completion may legally exceed the cadence.
```

This allows PiLab to support:

- realtime control
- browser-based HMIs
- image processing
- analytics
- uploads
- AI/edge workloads

inside one controller.

---

# Runtime Transparency

Most automation systems hide the runtime internals.

PiLab intentionally exposes them.

Examples:

- script execution time
- scan load percentage
- overrun conditions
- coalesced scans
- runtime fault states
- elapsed scan timing

The runtime behavior is:

```text
observable
configurable
script-visible
HMI-visible
```

---

# Low Friction Experimentation

One of the primary goals of PiLab is reducing friction between:

```text
idea
→
experiment
→
observable result
```

The feedback loop is intentionally extremely fast:

```text
write logic
upload
observe
modify
repeat
```

This encourages experimentation instead of discouraging it.

---

# Filesystem-Centric Workflow

PiLab treats the onboard filesystem as part of the runtime experience.

Users can:

- store multiple scripts
- swap runtime behavior instantly
- keep script versions directly on the controller
- store HMI assets
- maintain local automation projects

without constantly rebuilding firmware images.

---

# Runtime Policies

PiLab intentionally supports multiple execution policies.

Examples:

- strict realtime PLC behavior
- graceful overload handling
- coalesced execution
- edge-task operation
- runtime fault policies

This flexibility allows the same hardware platform to behave as:

- a soft PLC
- an edge controller
- a test executive
- a programmable automation appliance

depending on the workload.

---

# Philosophy Summary

PiLab is built around the idea that modern automation systems should be:

- programmable
- observable
- interactive
- scriptable
- browser-accessible
- experimentation-friendly
- realtime capable
- edge-aware

while remaining lightweight and deployable on inexpensive embedded hardware.

PiLab is not trying to recreate traditional industrial automation exactly as it already exists.

It is exploring what industrial automation might look like if it were designed today using modern runtime, web, and software development ideas.
