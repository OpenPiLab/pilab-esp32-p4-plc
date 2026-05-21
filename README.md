# pilab-esp32-p4-plc
PiLab P4 is an experimental browser-first PLC/HMI platform running directly on the ESP32-P4.

> 📖 New here? Start with the [PiLab Philosophy](docs/PiLab_Philosophy.md)

# Web App
![PiLab PLC Command Center](docs/images/command-center.png)

---
## Browser-Based Ladder Logic Editor (Experimental)

PiLab now includes an experimental browser-based ladder logic editor with:

- Wire-node branch modeling
- Live ladder simulation
- TON / TOF / CTU / CTD support
- JSON ladder project model
- Readable AngelScript transpilation
- Mixed ladder + AngelScript rungs
- Real-time wire/contact highlighting

### Live Demo

👉 Try it directly in your browser:

https://openpilab.github.io/pilab-esp32-p4-plc/experimental/ladder/pilab_ladder_editor.html

No installation required.

---
###  Web flasher for ESP32-P4

Only this specific Waveshare board has been tested
https://www.amazon.ca/dp/B0F2MP483W

https://openpilab.github.io/pilab-esp32-p4-plc/webflasher/

See how easy it is to go from a New Board to a PLC in 2 minutes
https://www.youtube.com/watch?v=g5mR0mprYgU

### Current Status

This editor is still experimental but already supports:
- Ladder drawing
- Branch editing
- Validation
- Simulation
- JSON import/export
- AngelScript generation


👉 Try the P4 root pages directly in your browser here (not fully functional without hardware):

https://openpilab.github.io/pilab-esp32-p4-plc/experimental/web/index.html

---
# System Architecture

```mermaid
flowchart TD
    A[Browser<br/>Vue.js SPA] --> B[ESP32-P4 Embedded Web Server]

    A1[Command Center] --> A
    A2[Script Editor] --> A
    A3[HMI Builder / Runtime] --> A
    A4[Tag Editor] --> A
    A5[File Manager] --> A
    A6[Settings] --> A

    B --> C[REST API]
    B --> D[Static Web App Hosting]

    C --> E[Shared Runtime Services]

    E --> F[Tag Database]
    E --> G[Script Manager]
    E --> H[LittleFS File Manager]
    E --> I[PLC State / Run-Stop Control]

    F --> J[PLC Runtime]
    G --> J
    I --> J

    J --> K[5 ms PLC Scan Loop]
    K --> L[AngelScript VM]
    K --> M[I/O Update Layer]

    M --> N[ESP32-P4 GPIO]
    M --> O[Future I/O Expansion]
    M --> P[Field Devices]
```

# Video Demo PLC Interface
https://www.youtube.com/watch?v=MBs3DlIrDFY

# Video Demo Ladder Logic Editor/Simulator
https://youtu.be/OfzHz8rGLJE?si=5LT6dlCu50iBqVzB

# PiLab ESP32-P4 PLC (Alpha)

Experimental ESP32-P4 PLC/HMI platform with an embedded Vue.js SPA, AngelScript runtime, browser-based programming, file manager, tag registry, and web HMI.

---

# What is this?

PiLab is an experimental PLC-style automation platform running directly on the ESP32-P4.

The system combines:

- Real-time PLC scan loop
- AngelScript runtime for machine logic
- Browser-based code editor
- Embedded Vue.js single-page web application
- HMI designer/runtime
- Tag registry
- File manager using LittleFS
- REST API for live PLC data
- Fully self-hosted web interface on the ESP32-P4

The goal is to create a lightweight, modern, browser-first automation platform without requiring heavy desktop software or proprietary tooling.

---

# Origins of the Project

PiLab did not originate as a hobby PLC project.

The architectural ideas behind the system trace back to earlier work on distributed real-time engine test automation systems developed for industrial dynamometer test cells.

The original concepts were explored in the 2012 University of Windsor thesis:

**“Design of an Engine Test Cell Control System”**  
by Anthony Joseph Fountaine

- University of Windsor Thesis Archive  
  https://uwindsor.scholaris.ca/items/46d8b135-747a-49b3-b691-0cdcc43894aa

- Direct Thesis URI  
  https://hdl.handle.net/20.500.14776/5353

## Thesis Summary

The thesis explored:

- Distributed data acquisition architectures
- Real-time machine sequencing
- Automated engine test execution
- Fault detection and alarming
- Long-term test data analysis
- Integration of real-time systems with graphical user interfaces

The system used a distributed architecture that moved acquisition hardware closer to the sensors while coordinating testing through a real-time operating system and Windows-based operator interface.

A custom sequencer was developed to create and execute complex automated engine tests.

The resulting system was deployed in multiple dynamometer test cells operating continuously in industrial environments.

## Relationship to PiLab

PiLab can be viewed as a modern continuation of many of those ideas using:

- Embedded edge hardware
- Browser-first engineering tools
- Embedded web technologies
- Runtime scripting
- Lightweight deployment
- Self-hosted interfaces
- AI-assisted engineering workflows

Rather than attempting to recreate traditional PLC environments, the project explores what modern machine orchestration and automation systems might look like if designed using contemporary software and web development concepts.

The project intentionally combines concepts from:

- PLC scan-based execution
- Embedded systems
- Industrial sequencing
- Browser-native tooling
- Runtime scripting environments
- Modern web application architecture

The goal is not simply to build another PLC/HMI, but to explore a more flexible and software-defined approach to industrial control and machine orchestration.

---

# Example PLC / Edge Scripts

## Analog Signal Processing + Heartbeat

Example script showing:

- cyclic PLC scan logic
- array usage
- floating point math
- string handling
- edge-style sensor processing

```cpp
array<float> samples;
uint counter = 0;

void scan()
{
    samples.resize(32);

    float average = 0.0f;

    for (uint i = 0; i < samples.length(); i++)
    {
        float phase = float((counter + i) % 628) * 0.01f;

        samples[i] = 50.0f + sin(phase) * 10.0f;

        average += samples[i];
    }

    average /= samples.length();

    string status = "RUNNING";

    if (average > 55.0f)
        status = "HIGH";

    if (average < 45.0f)
        status = "LOW";

    // Example PLC outputs
    Q0 = average > 52.0f;
    Q1 = average < 48.0f;

    counter++;
}
```

## Simple Edge Vibration Monitor

Example script simulating vibration monitoring and alarm detection.

```cpp
array<float> vibration;
float rms = 0.0f;
bool alarm = false;

void scan()
{
    vibration.resize(64);

    float acc = 0.0f;

    for (uint i = 0; i < vibration.length(); i++)
    {
        float noise =
            sin(float(i) * 0.21f) * 2.0f +
            cos(float(i) * 0.07f) * 1.5f;

        vibration[i] = noise;

        acc += vibration[i] * vibration[i];
    }

    rms = sqrt(acc / vibration.length());

    // Alarm threshold
    alarm = rms > 2.2f;

    // PLC outputs
    Q0 = alarm;
    Q1 = !alarm;
}
```


## PLC-Style Timer Logic (TON)

Example showing IEC-style PLC timer logic implemented directly in AngelScript.

```cpp
class TON
{
    uint preset_ms;
    uint elapsed_ms;

    bool input;
    bool output;

    TON(uint preset)
    {
        preset_ms = preset;
        elapsed_ms = 0;
        input = false;
        output = false;
    }

    void update(bool in, uint scan_ms)
    {
        input = in;

        if (input)
        {
            if (elapsed_ms < preset_ms)
                elapsed_ms += scan_ms;

            if (elapsed_ms >= preset_ms)
                output = true;
        }
        else
        {
            elapsed_ms = 0;
            output = false;
        }
    }

    bool Q()
    {
        return output;
    }

    uint ET()
    {
        return elapsed_ms;
    }
};

TON motorStartDelay(2000);

void scan()
{
    // Start motor 2 seconds after input turns on
    motorStartDelay.update(I0, 5);

    Q0 = motorStartDelay.Q();
}
```

## Rising Edge Trigger + Alarm Latch

Example showing PLC-style edge detection and alarm latching.

```cpp
class RisingEdge
{
    bool last = false;

    bool update(bool input)
    {
        bool edge = input && !last;
        last = input;
        return edge;
    }
};

class AlarmLatch
{
    bool latched = false;

    void trigger()
    {
        latched = true;
    }

    void reset()
    {
        latched = false;
    }

    bool active()
    {
        return latched;
    }
};

RisingEdge startEdge;
AlarmLatch alarm;

void scan()
{
    // Detect rising edge
    if (startEdge.update(I0))
    {
        Q0 = true;
    }

    // Example alarm condition
    if (AI0 > 85.0f)
    {
        alarm.trigger();
    }

    // Reset alarm
    if (I1)
    {
        alarm.reset();
    }

    Q1 = alarm.active();
}
```


---



# Development Approach

This project was developed using a combination of:

- Traditional engineering and debugging
- Iterative prototyping
- AI-assisted development workflows

AI tools were used as engineering copilots to accelerate:
- boilerplate generation
- refactoring
- debugging
- UI migration
- documentation
- architecture exploration

All integration, testing, hardware validation, timing analysis, and final design decisions were performed manually during development.

The goal of the project is not only to explore embedded PLC/HMI architecture, but also modern AI-assisted engineering workflows.

One of the specific interests for this project is using LLM's to generate and debug machine logic.  This is much more difficult with vendor PLC's and HMI because of the closed binary systems.

---

# Current Features

## Web Interface

- Vue.js SPA embedded directly into firmware
- Hard-refresh-safe routing
- Browser-based navigation
- No external web server required

Routes:

```text
/
 /hmi
 /tags
 /files
 /script
 /editor
```

---

## PLC Runtime

- Cyclic PLC scan task
- Digital I/O support
- PLC run/stop control
- Shared tag database
- Live polling APIs

---

## AngelScript Integration

- Browser-based script editor
- Script upload/build/run
- Compiler error reporting
- Live scan timing display
- Runtime execution environment

---

## HMI

- Browser-based HMI
- Retains state across route changes
- Live PLC data updates
- Tag binding support

---

## File System

- LittleFS support
- Upload/download/delete files
- Directory creation
- PLC-safe upload restrictions

---

# Hardware

Currently tested on:

- ESP32-P4
- Ethernet-enabled ESP32-P4 configurations
- https://www.amazon.ca/Waveshare-ESP32-P4-Module-High-Performance-Development-ESP32-P4/dp/B0F2MP483W


Waveshare PiLab Digital Input Output Pinout
https://openpilab.github.io/pilab-esp32-p4-plc/schematic/PiLabPLC_Pinout_Mapping.png

Main software stack:

- ESP-IDF v6.x
- Vue 3
- Vite
- TailwindCSS
- AngelScript

---

# Repository Layout

```text
pilab-esp32-p4-plc/
├── firmware/
│   └── P4_PLC_Base/
├── web/
│   └── WebProject2/
├── docs/
│   └── images/
├── examples/
└── README.md
```

---

# Building the Vue App

From the Vue project folder:

```bash
cd web/WebProject2
npm install
npm run build
```

The production output will be generated in:

```text
dist/
```

Current build output:

```text
dist/index.html
dist/assets/index.css
dist/assets/app.js
```

---

# Embedding the Vue App Into Firmware

Copy the built Vue files into the ESP-IDF project:

```text
firmware/P4_PLC_Base/main/web/
```

Result:

```text
main/web/index.html
main/web/assets/index.css
main/web/assets/app.js
```

The firmware embeds these files directly using:

```cmake
EMBED_FILES
```

inside:

```text
main/CMakeLists.txt
```

---

# Building the ESP-IDF Firmware

## Requirements

- ESP-IDF v6.x
- Python environment configured for ESP-IDF
- Node.js + npm

---

## Build Firmware

```bash
cd firmware/P4_PLC_Base
idf.py build
```

---

## Flash Firmware

```bash
idf.py flash
```

---

## Flash + Monitor

```bash
idf.py flash monitor
```

---

# Development Workflow

## Run Vue App Against Live ESP32-P4

Set target IP:

### PowerShell

```powershell
$env:P4_TARGET="http://192.168.5.210"
npm run dev
```

### cmd.exe

```cmd
set P4_TARGET=http://192.168.5.210
npm run dev
```

The Vite dev server proxies `/api/...` requests to the ESP32-P4.

---

# API Overview

Examples:

```text
/api/command_center
/api/plc_data
/api/plc_write
/api/script_status
/api/files/list
/api/files/upload
/api/files/delete
/api/tags
```

---

# Current Project Status

## Alpha / Experimental

This project is currently:

- Experimental
- Under active development
- Subject to breaking changes
- Not optimized
- Not hardened for production deployment

The focus right now is architecture, usability, workflow, and iteration speed.

---

# NOT Safety Certified

This project is:

- NOT SIL rated
- NOT safety certified
- NOT validated for industrial safety applications
- NOT guaranteed deterministic under all conditions

Do NOT use this system for:

- Human safety systems
- Emergency stop systems
- Critical industrial control
- Hazardous environments

This is currently an experimental development platform.

---

# Known Limitations

- Limited long-term runtime testing
- Minimal security/authentication
- No user management
- Limited protocol support
- File operations restricted while PLC is running
- No persistent project backup/export system yet

---

# Screenshots

The screenshots below show the current browser-based PiLab PLC interface running against the ESP32-P4 controller.

## Command Center

Live PLC status, scan timing, memory, script state, digital I/O, run/stop control, and system pulse history.

![PiLab PLC Command Center](docs/images/command-center.png)

---

## Script Editor

Browser-based AngelScript editor with upload, compile status, saved scripts, diagnostics, console output, and keyboard shortcuts.

![PiLab PLC Script Editor](docs/images/script-editor.png)

---

## HMI Builder

Browser-based HMI design mode with draggable widgets, tag-bound controls, live preview support, and an inspector panel.

![PiLab PLC HMI Builder](docs/images/hmi-build.png)

---

## HMI Live Operator View

Runtime HMI operator screen showing live toggles and output indicators driven by PLC tags.

![PiLab PLC HMI Live View](docs/images/hmi-live.png)

---

## Tag Registry

PLC tag editor for user tags, AngelScript globals, type selection, writable/retentive/HMI flags, and descriptions.

![PiLab PLC Tag Registry](docs/images/tag-registry.png)

---

## File Manager

LittleFS file browser with folders, upload controls, and write-lock protection while the PLC scan is running.

![PiLab PLC File Manager](docs/images/file-manager.png)

---

# Why This Project Exists

Many PLC/HMI systems still rely on:

- Large desktop IDEs
- Proprietary runtimes
- Heavy installation requirements
- Complex deployment workflows

PiLab explores a different direction:

- Browser-first tooling
- Embedded web technologies
- Lightweight deployment
- Fast iteration cycles
- Open experimentation
- AI-assisted control scripts and HMI screens

---

# License

- MIT

---

# Contributing

Contributions, testing, bug reports, and ideas are welcome.

This project is evolving rapidly.

---

# Acknowledgements

- Espressif
- Vue.js
- Vite
- TailwindCSS
- AngelScript
