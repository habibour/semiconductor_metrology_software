# Product Requirements Document (PRD)

## StressScan-Sim: a cassette-to-cassette wafer film-stress tool simulator with an optional SECS/GEM host link

| Field | Value |
|---|---|
| Working title | StressScan-Sim (rename freely; do not use any Frontier / FSM name or logo) |
| Document version | 0.1 (draft for build start) |
| Date | 21 September 2026 |
| Author | Md. Habibur Rahman (Habib) |
| Status | Draft; scope to be frozen at the end of Day 1 (21 September) |
| Purpose of the project | Portfolio project targeted at the Software Engineer opening at Frontier Semiconductor Bangladesh Ltd. (application deadline 28 September 2026) |
| Hardware required | None. All hardware is simulated in software |
| Development machine | Apple Silicon Mac (macOS). macOS is the only supported platform (see D-11) |

---

## 0. Plain-English summary

Imagine a smart washing machine that also has a phone app. The machine works alone: you load it, press start, it runs a cycle. The app is an optional extra that can start the machine remotely and receive "done" or "something is wrong" notifications.

This project is the software for that kind of machine, except the "clothes" are thin silicon discs (wafers) and the "cycle" is a laser scan that measures how much a coating on the disc bends it. The bend is turned into a number (film stress) using a standard physics formula. The "phone app" is a factory host computer that talks to the machine using SECS/GEM, the standard language of semiconductor factories.

There is no real laser or motor. A software simulator invents realistic, noisy laser readings from a hidden "true" stress value, and the machine software must find that value again. That makes the project checkable: the answer is known, so any error is measurable.

The project is built in two steps, in this order:

1. A fully working standalone machine application (threads, simulated hardware, analysis, Qt screen, exports).
2. SECS/GEM added as a separate module so a host can control it.

That order mirrors the job posting's wording: "integrate SECS/GEM compliance into a fully functional software system".

---

## 1. Purpose and background

### 1.1 Why this document exists

This PRD defines what will be built, why, for whom, and how we will know it works. It is the single reference for scope, requirements, data formats, tests and delivery. Every requirement has an ID so tests, commits and CV bullets can point back to it.

### 1.2 The target role

The job posting (Software Engineer, 2 openings, Frontier Semiconductor Bangladesh Ltd., Dhaka) asks for:

- Design and develop machine-control, high-performance, multithreaded software for automated semiconductor equipment.
- Maintain the current system: new features, bug fixing, code refactoring, performance optimization.
- Integrate SECS/GEM compliance into a fully functional software system.
- B.Sc. in CSE or related field; 0 to 2 years of experience (freshers encouraged).
- Excellent data structures and algorithms; proficient C++ and object-oriented programming.
- Experience with C# WinForm/WPF; C++ Qt; multithreading and STL; Visual Studio and MFC (preferable).
- Good knowledge of data communications, computer networks, operating systems and design patterns.
- Competitive programming and problem solving (plus); MATLAB (plus).
- Ability to work in research-based projects and learn new technologies; fluent English; willingness to travel abroad.

The company's "About Us" text says it is looking for people for an embedded firmware / new-product research team in Dhaka.

### 1.3 Problem statement

A fresher's CV lists skills, but a hiring engineer cannot verify them. We need one project that (a) touches almost every line above in a way that can be run and inspected, (b) needs no hardware, (c) can be finished in about seven days by one developer, and (d) is honest about its limits.

### 1.4 Public context used to shape the design

Frontier's public product pages describe wafer-metrology tools. The 128 Series performs full-wafer 2D/3D film-stress and bow mapping by laser scanning. Cassette-to-cassette (C2C) models are described as fully automated and SECS/GEM compliant, and one product page lists SECS/GEM as an option. A third-party reseller listing (unofficial) describes a Windows-based control PC, 32 scan lines at 40 data points per millimetre, 3 mm edge exclusion, and export to Excel and JPEG. This PRD borrows those public ideas as inspiration for realistic parameters. It does not reproduce any proprietary software and makes no claim about how Frontier's real software is built.

---

## 2. Product overview

### 2.1 Vision

A credible, well-tested, portable simulator of a film-stress metrology tool: the same kind of software an equipment maker writes, running against simulated hardware, controllable by a factory host over SECS/GEM, and provable through tests, benchmarks and a repeatable demo.

### 2.2 What the product consists of

| Part | What it is | Runs where |
|---|---|---|
| Core library | Machine logic: state machine, scan and analysis pipeline, simulated hardware, events, logging | Everywhere (no Qt, no network dependency) |
| SECS/GEM module | HSMS session, SECS-II codec, GEM behaviour; optional at build time and run time | Everywhere |
| Equipment CLI | Headless machine program, used for demos and CI | macOS |
| Operator panel | Qt Widgets window: status, controls, wafer map, alarms, message trace | macOS |
| Host simulator | Pretend factory computer: scripted conversations with pass/fail checks | macOS |
| Tools | Octave/MATLAB stress cross-check; Python interop test with an open-source SECS/GEM library | Anywhere |
| Optional extras | Firmware-style device simulator; C# host; Windows-only C# WPF console | See section 5 |

### 2.3 Key concepts in one paragraph each

Film stress. A thin coating on a wafer pulls on it and makes it curve very slightly, like tape curling a ruler. Measuring the curvature gives the stress via Stoney's equation. Chip makers care because too much stress cracks films.

Simulation with a hidden answer. The simulator picks a true stress for each wafer, computes the resulting curved surface, adds tilt and sensor noise, and streams readings as a scan would. The machine software analyses the noisy readings and must recover the true stress within a stated tolerance.

SECS/GEM. A stack of three standards that let a factory host control a machine. HSMS is the TCP "phone line" with timers and a hello/heartbeat protocol. SECS-II is the message format (Stream and Function numbers with typed, nested data). GEM defines behaviour: communication and control states, events, alarms, status data and remote commands.

---

## 3. Goals, non-goals and success metrics

### 3.1 Goals

| ID | Goal |
|---|---|
| G1 | Demonstrate high-performance, multithreaded C++17 machine-control software with a clean, testable architecture. |
| G2 | Demonstrate correct integration of a SECS/GEM subset (HSMS, SECS-II, GEM) into a working application, as a separable module. |
| G3 | Demonstrate real engineering habits: hardware abstraction, design patterns, automated tests, sanitizers, benchmarks, CI, refactoring and optimization visible in history. |
| G4 | Demonstrate physics-aware software: known-answer tests, uncertainty, cross-check in MATLAB/Octave. |
| G5 | Make the work easy to evaluate: a two-minute video, a one-command demo, downloadable builds and a readable README. |
| G6 | Keep every claim on the CV defensible in an interview. |

### 3.2 Non-goals

| ID | Non-goal |
|---|---|
| N1 | Not a certified or complete GEM implementation. It is a subset based on publicly documented behaviour. |
| N2 | No real hardware, drivers, motion control or laser hardware. |
| N3 | Not a web application and no public-internet service. |
| N4 | No machine learning component. |
| N5 | Not a claim to have invented the physics; formulas come from standard literature. |
| N6 | No MFC and no WPF/WinForms unless Windows access is available (see Tier 3). |
| N7 | No copying of SEMI standard documents or of any Frontier proprietary material. |

### 3.3 Success metrics

Numeric targets below are goals to be measured, not results. Record the real numbers in the README once measured.

| ID | Metric | Target | How measured |
|---|---|---|---|
| SM1 | Stress recovery accuracy on the nominal wafer (6 lines, 40 points/mm, 0.5 um noise) | Within 2% of hidden true value | Known-answer unit and scenario tests |
| SM2 | Stress recovery with injected spikes and dropouts | Within 5% or a correct alarm | Fault tests |
| SM3 | Sample-path throughput, version 2 vs version 1 | At least 2x higher | Benchmark |
| SM4 | Time to analyse one wafer (72,000 samples) at real-time factor 0 | Recorded baseline, then at least 25% faster after optimization | Benchmark |
| SM5 | Thread-safety | ThreadSanitizer clean on the whole test suite | CI job |
| SM6 | Memory safety | AddressSanitizer and UBSan clean | CI job |
| SM7 | Robustness | No crash or hang on at least 100,000 mutated protocol frames per CI run | Mutation test |
| SM8 | Interoperability | Independent open-source SECS/GEM host completes the core scenario against the machine | Interop test |
| SM9 | Portability | CI green on macOS (Apple clang) | GitHub Actions |
| SM10 | Line coverage of core, analysis and secsgem modules | At least 80% | llvm-cov / gcov |
| SM11 | Reviewer effort | Video under 2 minutes; `scripts/demo.sh` one-command demo works; README quickstart under 10 minutes | Manual check on a clean machine |

---

## 4. Users and use cases

### 4.1 Personas

| Persona | Needs |
|---|---|
| Hiring engineer (primary reviewer) | Judge code quality, architecture, tests and understanding within 10 to 20 minutes. |
| Recruiter / HR reader | Understand the project in two minutes; see a working demo without building anything. |
| Operator (in-app role) | See machine state, start and stop scans, switch control mode, read alarms. |
| Host software developer (secondary real-world user) | Use the machine as an emulator to test factory host software without real equipment. |
| Developer (Habib) | Iterate quickly, refactor safely, explain every design choice in an interview. |

### 4.2 Use cases

| ID | Use case | Actors | Outcome |
|---|---|---|---|
| UC1 | Run a standalone scan of one wafer | Operator | Stress result, wafer map and files, no host involved |
| UC2 | Process a cassette of wafers | Operator or host | Each wafer scanned in turn; cassette summary produced |
| UC3 | Host establishes communication and starts a scan remotely | Host | Events and results delivered to host |
| UC4 | Operator switches control mode Local/Remote | Operator | Host commands accepted or rejected accordingly |
| UC5 | Sensor fault raises an alarm and blocks scanning until cleared | Machine, operator or host | Alarm event sent; scan refused until cleared |
| UC6 | Host connection drops mid-scan | Machine | Machine survives, keeps result, accepts a new host connection |
| UC7 | Host sends malformed or unknown messages | Host | Machine replies with correct error messages and keeps running |
| UC8 | Replay a recorded session for debugging | Developer | Same sequence re-sent, outputs compared |
| UC9 | Reviewer runs the headless demo with one command on a Mac | Reviewer | Scripted scenario prints message trace and writes result files |
| UC10 | Reviewer runs the test suite | Reviewer | All tests pass; sanitizer builds clean |

---

## 5. Scope and tiers

Priority letters used in requirement tables: M = must (Tier 1), S = should (Tier 2), C = could (Tier 3).

| Tier | Contents | Target completion |
|---|---|---|
| Tier 1 (must) | Standalone machine: config, simulated hardware, multi-line scan, threaded pipeline, analysis and Stoney stress, state machine, alarms, Qt panel, CLI, exports, logging, unit and scenario tests, CI on macOS. SECS/GEM module: HSMS, SECS-II codec, GEM subset (communication, control state, events, alarms, remote commands, status variables, S9 errors), host simulator, interop test. README, demo video, CV. | 27 Sep (submit) |
| Tier 2 (should) | Cassette-to-cassette loop; sample-path optimization with benchmark table; MATLAB/Octave cross-check; firmware-style device simulator with framed binary protocol and CRC; C# .NET host; equipment constants and dynamic reports; spooling; replay; local one-command demo and a macOS release build. | 26 to 27 Sep, only if Tier 1 is green |
| Tier 3 (could) | C# WPF or WinForms operator console (MVVM), built and seen on a real Windows machine or VM; MFC and Visual Studio experience. | Only with Windows access; otherwise excluded and not claimed |

Cut order if time runs short: Tier 3, then device simulator, then C# host, then dynamic reports and spooling, then MATLAB check, then cassette loop. The tests, README, CI and CV are never cut.

---

## 6. System architecture

### 6.1 Context

```
        optional link: TCP, HSMS / SECS-II / GEM
 +------------------+  <------------------------------->  +----------------------------------+
 | Host simulator   |                                     | Equipment application            |
 | (C++ CLI; C#     |                                     |  +----------------------------+  |
 |  host in Tier 2) |                                     |  | SECS/GEM module (optional) |  |
 +------------------+                                     |  +-------------+--------------+  |
                                                          |                | commands / events |
 +------------------+   operator actions / display        |  +-------------v--------------+  |
 | Operator (Qt)    | <---------------------------------> |  | Core library               |  |
 +------------------+                                     |  | controller, pipeline, logs |  |
                                                          |  +-------------+--------------+  |
                                                          |                | IStage / ILaserSensor |
                                                          |  +-------------v--------------+  |
                                                          |  | Simulated hardware         |  |
                                                          |  +----------------------------+  |
                                                          +----------------------------------+
```

### 6.2 Components and dependency rules

| Component | Responsibility | May depend on |
|---|---|---|
| ssim_core | Config, units, clock, logging, queues, thread pool, event bus, command queue, machine controller and state machines | STL, JSON library |
| ssim_hw | Hardware interfaces (IStage, ILaserSensor, ILoadPort), simulated wafer model, simulated devices, fault injector, cassette simulator | ssim_core |
| ssim_analysis | Edge exclusion, outlier rejection, curve fit, Stoney stress, uncertainty, wafer map, CSV/JSON/PNG writers | ssim_core (types only) |
| ssim_machine | Reusable composition root (`MachineRuntime`): wires core, hardware and analysis for a machine that scans wafers back to back, on its own threads; hosts the optional SECS/GEM link | ssim_core, ssim_hw, ssim_analysis, ssim_secsgem (optional) |
| ssim_secsgem | HSMS session and timers, SECS-II codec, message catalog, GEM state models and handlers | ssim_core, Asio |
| equipment_cli | Headless machine executable; demo mode; scenario support | all above |
| equipment_qt | Qt Widgets operator panel | all above, Qt |
| host_sim | Scripted factory host; pass/fail checks for CI | ssim_secsgem |
| device_sim (Tier 2) | Firmware-style device simulator speaking a framed binary protocol with CRC-16 | ssim_core |

Dependency rules: arrows point downward only. ssim_core must never include SECS/GEM, network or Qt headers. The SECS/GEM module is switchable with the CMake option `SSIM_ENABLE_SECSGEM` and with the runtime flag `comm.enabled`. The machine must build, pass its tests and run a full scan with the module absent.

### 6.3 Threading model

| Thread | Count | Owns | Communicates through |
|---|---|---|---|
| Main thread (CLI loop or Qt GUI thread) | 1 | UI widgets or the CLI loop | Command queue (in); event bus delivered through queued signals (out) |
| Controller | 1 | Process state machine, current run context | Command queue (in); events (out) |
| Scan | 1 | IStage and ILaserSensor | Sample queue (out); stop flag (in) |
| Processing pool | N (default max(1, cores minus 1)) | Fit and analysis jobs | Job queue (in); results to controller (out) |
| HSMS I/O | 1 (Asio io_context) | Sockets, timers, session state | Command queue (out to controller); event subscription (in) |
| Logger | 1 | Log and trace file writers | Bounded log queue (in) |

### 6.4 Concurrency rules (enforced in review and in CI)

| ID | Rule |
|---|---|
| C1 | The controller thread is the only writer of machine state. Other threads read immutable snapshots. |
| C2 | Threads hand work to each other only through queues. No shared mutable data outside a queue or an atomic. |
| C3 | No lock is held while calling a callback, subscriber or user code. |
| C4 | A lock hierarchy is documented in docs/architecture.md. At most one lock is held at a time, except where the document says otherwise. |
| C5 | Every queue is bounded and has a stated back-pressure policy (block, drop-oldest, or raise alarm). |
| C6 | Shutdown protocol: request stop, wake all waiters, drain or discard by policy, join in reverse dependency order, flush logs. No detached threads. |
| C7 | Exceptions never cross a thread boundary. Errors travel as values or as events. |
| C8 | Time comes from an injectable Clock. Tests use a fake clock so timers are exact and fast. |
| C9 | Queue implementations sit behind an interface, so version 1 (mutex and condition variable) can be replaced by version 2 (lock-free single-producer single-consumer ring buffer) without touching callers. |

### 6.5 State machines

Process state machine (owned by the controller).

| From | Trigger | Guard | To | Actions |
|---|---|---|---|---|
| Idle | Start | Source allowed by control state; no active alarm; wafer or cassette valid | Scanning | Reset run data; emit ScanStarted |
| Idle | Start | Any guard fails | Idle | Reject with reason code |
| Scanning | All lines complete | none | Processing | Queue analysis jobs |
| Scanning | Stop | none | Scanning | Set stop-after-wafer flag |
| Scanning | Abort | none | Stopping | Abort stage; discard partial data |
| Scanning | Fault | none | Alarm | Stop stage; set alarm |
| Processing | Result ready | More wafers in cassette | Scanning | Emit ScanComplete (and WaferOutOfSpec if flagged); next wafer |
| Processing | Result ready | No more wafers or stop flag set | Idle | Emit ScanComplete; write outputs; emit CassetteComplete if applicable |
| Processing | Fault | none | Alarm | Set alarm; no result reported |
| Processing | Abort | none | Stopping | Discard jobs |
| Alarm | ClearAlarm | Alarm cause cleared | Idle | Emit alarm cleared |
| Stopping | Stage stopped | none | Idle | Emit RunAborted |

Control state model (subset of GEM).

| State | Meaning | Host commands (S2F41) | Operator commands | Host queries |
|---|---|---|---|---|
| Offline | Machine is out of host control | Rejected (HCACK 2) | Accepted | Answered |
| Online-Local | Operator in charge; host may observe | Rejected (HCACK 2) | Accepted | Answered |
| Online-Remote | Host in charge | Accepted | Start disabled; Stop and Abort still accepted for safety | Answered |

The operator switches states in the panel or CLI. The host may request Offline (S1F15) or Online (S1F17). S1F17 is accepted only if `comm.allow_host_online` is true, and the machine then enters Online-Local or Online-Remote according to the operator's remote-switch setting. Design decision to verify against public GEM descriptions: queries are answered in every state, commands only in Online-Remote.

Communication state (simplified). NotCommunicating until a successful S1F13 exchange, then Communicating. Loss of the TCP connection, Separate, or T7 expiry returns it to NotCommunicating. This omits GEM's WaitCRA and WaitDelay sub-states; the deviation is documented.

HSMS connection state. NOT_CONNECTED, then NOT_SELECTED after TCP accept (T7 running), then SELECTED after a successful Select. Deselect, Separate, timeout or socket error returns toward NOT_CONNECTED.

### 6.6 Design patterns and where they appear

| Pattern | Where | Why |
|---|---|---|
| State | Process state machine; GEM control state; HSMS session state | Behaviour depends on state; illegal actions are rejected in one place |
| Command | Start, Stop, Abort, ClearAlarm, SetControlMode as objects with source and correlation id | One queue for UI, CLI and SECS/GEM; easy logging and replay |
| Observer / publish-subscribe | Event bus with UI, SECS/GEM module and logger as subscribers | Core stays unaware of who listens |
| Strategy | Outlier filter, fit method, interpolation, queue back-pressure policy | Swap algorithms without changing the pipeline |
| Factory | SECS-II message factory by Stream and Function; hardware factory from config | Add messages or drivers without touching callers |
| Adapter | HSMS-to-core adapter; device-link driver adapting a binary protocol to IStage and ILaserSensor | Isolates external formats |
| Producer-consumer | Scan thread to processing pool through the sample queue | Classic concurrent pipeline |
| Pipeline / template method | Analysis stages run in a fixed order | Testable stage by stage |
| RAII | Sockets, threads, files, scope guards | No leaks on any exit path |
| Facade | MachineApi exposing the few operations UI, CLI and SECS/GEM need | Small, stable surface |
| Dependency injection | Clock, hardware, logger passed into constructors | Testability |

### 6.7 Repository layout (repository root is the frontier folder)

```
frontier/
  CMakeLists.txt
  cmake/                    toolchain helpers, sanitizer options
  docs/                     PRD.md, architecture.md, protocol-notes.md, scenario-format.md, decisions/
  include/ssim/             public headers per module
  src/
    core/                   config, clock, logging, queues, thread pool, events, controller
    hw/                     interfaces, wafer model, simulated devices, fault injector
    analysis/               filters, fit, stoney, mapping, writers
    secsgem/                hsms/, secs2/, gem/
    app_cli/                equipment_cli main
    app_qt/                 operator panel
    host_sim/               scripted host
    device_sim/             firmware-style simulator (Tier 2)
  tests/                    unit/, integration/, scenario/, fuzz/
  bench/                    queue and analysis benchmarks
  scenarios/                *.scn host scripts
  scripts/                  check_stress.m, interop_secsgem.py, demo helpers
  scripts/demo.sh           one-command local demo
  .github/workflows/        CI and release workflows
  results/                  generated output (git-ignored)
```

---

## 7. Functional requirements

Verification key: U unit test, I integration test, S scenario test, F fault or fuzz test, B benchmark, X interoperability test, D demo or inspection.

### 7.1 Configuration (CFG)

| ID | Requirement | Pri | Verify |
|---|---|---|---|
| FR-CFG-1 | Load a JSON configuration (machine, wafer, scan, analysis, alarms, comm, output sections) with documented defaults; unknown keys produce a warning; invalid values abort startup with a clear message and exit code 2. | M | U |
| FR-CFG-2 | Validate every value against a stated range (for example scan lines 1 to 32, points per mm 10 to 80, edge exclusion 0 to 20 mm). | M | U |
| FR-CFG-3 | Allow a permitted subset of values (the equipment constants in 8.6) to change at runtime; each change is validated, logged and announced as an event. | S | I |

### 7.2 Hardware abstraction and simulation (HW)

| ID | Requirement | Pri | Verify |
|---|---|---|---|
| FR-HW-1 | Define IStage and ILaserSensor (and ILoadPort for cassettes) so that no code outside the hardware factory names a concrete device. | M | U, D |
| FR-HW-2 | Simulated wafer generator builds a hidden truth (stress, geometry, initial bow, tilt, anisotropy) from configuration and a random seed, following 8.2. Same seed gives identical data. | M | U |
| FR-HW-3 | Simulated stage moves along a programmed scan line at a configured speed; simulated laser returns height at the stage position plus tilt and noise. | M | U |
| FR-HW-4 | Fault injection: spike, burst, dropout gap, stuck value, drift, saturation, stage stall; selectable per wafer in configuration or scenario. | M | U, F |
| FR-HW-5 | Real-time factor setting: 1.0 uses realistic timing; 0 runs as fast as possible for tests and benchmarks. | M | U |
| FR-HW-6 | Cassette simulator with 1 to 25 slots, empty slots, and per-wafer truth values. | S | U |
| FR-HW-7 | Device-link driver: a separate device_sim process speaks a framed binary protocol (start byte, length, command, payload, CRC-16) over TCP; the core talks to it through IStage and ILaserSensor. Bad CRC frames are rejected and counted. | S | U, I, F |

### 7.3 Scan and acquisition (SCN)

| ID | Requirement | Pri | Verify |
|---|---|---|---|
| FR-SCN-1 | Multi-line diametric scan: N lines (default 6, maximum 32) at evenly spaced angles over 180 degrees; points per mm default 40; wafer diameters 100, 150, 200 and 300 mm (default 300). | M | U |
| FR-SCN-2 | The scan thread emits sample blocks (line index, angle, positions, heights, timestamps) into a bounded queue. Back-pressure policy is configurable; the default blocks, and an alternative drops and raises QueueOverflow. | M | U, B |
| FR-SCN-3 | Abort takes effect within 200 ms at real-time factor 1.0 and leaves the machine in a consistent state. | M | F |
| FR-SCN-4 | Scan progress (0 to 100 percent) is published for the UI and as a status variable. | M | I |

### 7.4 Processing and analysis (PRC)

| ID | Requirement | Pri | Verify |
|---|---|---|---|
| FR-PRC-1 | Edge exclusion: remove samples within `edge_exclusion_mm` (default 3) of each end of every line. | M | U |
| FR-PRC-2 | Outlier rejection using a robust method (median and median absolute deviation); report the count and fraction removed. | M | U, F |
| FR-PRC-3 | Per-line least-squares fit of z = a s^2 + b s + c; curvature is 2a; radius of curvature is 1/curvature; report residual RMS and standard error of a. | M | U |
| FR-PRC-4 | Combine lines into mean curvature and per-angle curvature, and report anisotropy as (max minus min) divided by mean. | M | U |
| FR-PRC-5 | Subtract the pre-coating curvature (from a pre-scan or configuration) and compute stress with Stoney's equation (8.2), using the stated sign convention. | M | U |
| FR-PRC-6 | Report a one-sigma stress uncertainty propagated from the fit standard errors. | M | U |
| FR-PRC-7 | Quality gates: poor fit (RMS above limit) or non-finite result raises an alarm and reports no number; implausible stress raises an alarm; stress outside the specification window is reported with an out-of-spec flag and event, not an alarm. | M | U, F |
| FR-PRC-8 | Build a 2D height map by linear interpolation between adjacent scan lines in polar coordinates, on a grid of configurable resolution (default 1 mm). | M | U |
| FR-PRC-9 | Run per-line fits in a thread pool; results are bit-for-bit identical to a single-threaded run. | S | U, B |

### 7.5 Machine control (MC)

| ID | Requirement | Pri | Verify |
|---|---|---|---|
| FR-MC-1 | The controller owns the process state machine in 6.5; illegal transitions are rejected with a reason code and logged. | M | U |
| FR-MC-2 | All commands (Start, Stop, Abort, ClearAlarm, SetControlMode) enter through one thread-safe command queue from any source (UI, CLI, SECS/GEM); the source is recorded. | M | U, I |
| FR-MC-3 | The machine works fully with the SECS/GEM module disabled or absent. | M | I |
| FR-MC-4 | Control state rules in 6.5 are enforced for every command source. | M | U, S |
| FR-MC-5 | Cassette processing: run wafers in slot order, skip empty slots, continue after an out-of-spec wafer, halt on alarm, produce a cassette summary. | S | I, S |
| FR-MC-6 | Graceful shutdown: on stop, drain queues, join all threads, flush logs and files within 2 seconds. No thread leaks. | M | U, F |

### 7.6 Alarms and watchdogs (ALM)

| ID | Requirement | Pri | Verify |
|---|---|---|---|
| FR-ALM-1 | Alarm catalogue (8.6) with set and clear semantics; an active alarm blocks new scans until cleared. | M | U |
| FR-ALM-2 | Each alarm set and clear transition is announced to all subscribers exactly once. | M | U |
| FR-ALM-3 | Watchdogs: scan stall (no samples for a configured time), processing timeout, queue overflow. | M | F |

### 7.7 Outputs (OUT)

| ID | Requirement | Pri | Verify |
|---|---|---|---|
| FR-OUT-1 | Write a per-wafer JSON summary (8.5). | M | U |
| FR-OUT-2 | Write a CSV of per-line results and (optionally subsampled) processed samples, with a documented header and units, openable in Excel. | M | U |
| FR-OUT-3 | Write a PNG wafer map with colour bar and legend, without needing Qt. | M | U, D |
| FR-OUT-4 | Write a cassette summary CSV. | S | U |
| FR-OUT-5 | Use the directory layout `results/<run_id>/<wafer_id>/`; never overwrite an existing run. | M | U |

### 7.8 Operator panel (UI)

| ID | Requirement | Pri | Verify |
|---|---|---|---|
| FR-UI-1 | Status bar shows communication, control and process states; buttons for Start, Stop, Abort and Clear Alarm are enabled only when the rules allow; selector for control mode. | M | D |
| FR-UI-2 | Live scan progress, wafer map, last result with uncertainty, alarm banner, and a scrolling log and message trace. | M | D |
| FR-UI-3 | The GUI thread never runs long work; updates arrive through queued signals at no more than 30 Hz. | M | D, B |
| FR-UI-4 | Settings dialog for permitted values. | S | D |
| FR-UI-5 | Export and open-results-folder actions. | S | D |
| FR-UI-6 | Demo mode suitable for recording a clean video. | C | D |

### 7.9 Command line and headless mode (CLI)

| ID | Requirement | Pri | Verify |
|---|---|---|---|
| FR-CLI-1 | `equipment_cli` runs the machine without Qt using the same core, with options for config file, port, seed, real-time factor, output directory and scenario. | M | S |
| FR-CLI-2 | Exit codes: 0 success, 2 configuration error, 3 runtime fault, 4 interrupted. | M | U |
| FR-CLI-3 | A `demo` mode runs a built-in scenario against a local host simulator and exits; used by the one-command demo script. | M | S |

### 7.10 Logging, tracing and replay (LOG)

| ID | Requirement | Pri | Verify |
|---|---|---|---|
| FR-LOG-1 | Structured JSON-lines log with monotonic and wall-clock time, level, thread, component, event and fields, written by a dedicated logger thread. | M | U |
| FR-LOG-2 | Full SECS message trace: direction, time, stream, function, W-bit, system bytes and decoded body. | M | U |
| FR-LOG-3 | Replay: `host_sim --replay trace.jsonl` re-sends recorded host messages and compares machine outputs. | S | S |
| FR-LOG-4 | Size-capped rotation of log files. | M | U |

### 7.11 HSMS session (HSMS)

| ID | Requirement | Pri | Verify |
|---|---|---|---|
| FR-HSMS-1 | The machine acts as passive entity: it listens on a configurable address and port (default 127.0.0.1:5000); the host simulator connects. | M | I |
| FR-HSMS-2 | Framing: 4-byte length, 10-byte header, body. Handle partial reads and several messages in one read. Reject a frame above the maximum length (default 1 MiB) and close the connection. | M | U, F |
| FR-HSMS-3 | Session control messages: Select.req and rsp, Deselect.req and rsp, Linktest.req and rsp, Reject.req, Separate.req, with the connection states in 6.5. | M | U, I |
| FR-HSMS-4 | Timers T3, T6, T7 and T8 (and T5 on the host side) are configurable and use the injectable clock; T7 expiry closes the connection. | M | U, F |
| FR-HSMS-5 | Match replies to requests by system bytes; support several outstanding transactions; generate unique system bytes. | M | U |
| FR-HSMS-6 | One host session at a time; a second connection is refused. | M | I |
| FR-HSMS-7 | After link loss the machine returns to listening within 1 second; a scan in progress continues, and events raised while not communicating are logged. | M | S, F |
| FR-HSMS-8 | Optional spooling: events raised while not communicating are held in a bounded queue and delivered after reconnect; overflow raises an alarm. | S | S |

### 7.12 SECS-II codec and messages (S2)

| ID | Requirement | Pri | Verify |
|---|---|---|---|
| FR-S2-1 | Encode and decode item types L, B, BOOLEAN, A, I1, I2, I4, I8, U1, U2, U4, U8, F4, F8 with one to three length bytes, big-endian. Encode then decode returns the original. | M | U |
| FR-S2-2 | Strict decoder: reject truncated data, inconsistent lengths, list counts that do not match, and nesting deeper than 32; never read past the buffer; return errors as values. | M | U, F |
| FR-S2-3 | Message catalogue (8.6) with typed builders and parsers; unknown stream, unknown function or illegal data produce the correct S9 error message. | M | U, I |
| FR-S2-4 | Human-readable text dump of any message for logs and traces. | M | U |
| FR-S2-5 | Text-to-message parser so host scripts can write messages in a readable form. | C | U |

### 7.13 GEM behaviour (GEM)

| ID | Requirement | Pri | Verify |
|---|---|---|---|
| FR-GEM-1 | Communication state model (6.5); S1F13 and S1F1 handled in every state. | M | I |
| FR-GEM-2 | Control state model with operator switching and host requests S1F15 and S1F17 (6.5). | M | I, S |
| FR-GEM-3 | Remote commands (S2F41): START, STOP, ABORT, CLEAR_ALARM, with parameter validation and HCACK and CPACK replies. | M | I, S |
| FR-GEM-4 | Events are sent as S6F11 with fixed report definitions (8.6); the host acknowledges with S6F12. | M | I, X |
| FR-GEM-5 | Alarms are sent as S5F1 on set and on clear; host acknowledges with S5F2. | M | I, X |
| FR-GEM-6 | Status variable query S1F3 and S1F4 for the variables in 8.6; an unknown variable returns an empty item. | M | I |
| FR-GEM-7 | Equipment constants S2F13, S2F14, S2F15, S2F16 with range checks and acknowledgement. | S | I |
| FR-GEM-8 | Dynamic event reports S2F33, S2F35 and S2F37 (host defines reports and links and enables events). | S | I |
| FR-GEM-9 | Error messages S9F1, S9F3, S9F5, S9F7, S9F9, S9F11 in the situations 8.6 lists. | M | I, F |

### 7.14 Host simulator and tools (HOST, TOOL)

| ID | Requirement | Pri | Verify |
|---|---|---|---|
| FR-HOST-1 | `host_sim` connects, selects and runs a script language (send, expect, wait for event, sleep, assert, disconnect; see 8.7). | M | S |
| FR-HOST-2 | Print and log every message decoded; exit non-zero if any expectation fails so CI can use it. | M | S |
| FR-HOST-3 | Scenario library: normal_run, cassette_run, alarm_recovery, link_loss, malformed_frames, bad_commands, wrong_control_state, t3_timeout. | M | S |
| FR-HOST-4 | C# .NET host implementing a subset (S1F1, S1F13, S2F41, S6F11 acknowledge). | S | X |
| FR-HOST-5 | C# WPF or WinForms operator console using MVVM, Windows only. | C | D |
| FR-TOOL-1 | `scripts/check_stress.m` recomputes stress from the exported CSV and reports the percentage difference from the machine result; runs in MATLAB or Octave. | S | X |
| FR-TOOL-2 | `scripts/interop_secsgem.py` runs the core scenario using an independent open-source SECS/GEM host library against the machine. | M | X |

---

## 8. Data specification

Everything in this section is a project definition. Standard numbers and formulas come from public literature and public descriptions; verify them before relying on them (see section 16).

### 8.1 Configuration file (illustrative defaults)

```json
{
  "machine": { "model": "SSIM-128", "softrev": "0.1.0" },
  "wafer": {
    "diameter_mm": 300,
    "thickness_um": 775,
    "film_thickness_um": 1.0,
    "biaxial_modulus_gpa": 180.5,
    "truth": {
      "stress_mpa": -180.0,
      "initial_curvature_1_per_m": 0.002,
      "tilt_x_um_per_mm": 0.8,
      "tilt_y_um_per_mm": -0.3,
      "anisotropy": 0.05
    },
    "seed": 12345
  },
  "scan": {
    "lines": 6, "points_per_mm": 40, "speed_mm_per_s": 150,
    "realtime_factor": 1.0, "edge_exclusion_mm": 3.0
  },
  "noise": { "sigma_um": 0.5 },
  "faults": [
    { "wafer": "W005", "type": "spike", "rate": 0.002, "amplitude_um": 40 }
  ],
  "analysis": {
    "outlier_mad_k": 6.0, "outlier_fraction_alarm": 0.01, "fit_rms_limit_um": 2.0,
    "stress_spec_mpa": [-400, 400], "stress_plausible_mpa": [-5000, 5000],
    "map_grid_mm": 1.0, "threads": 0
  },
  "cassette": { "id": "C001", "slots": 25 },
  "comm": {
    "enabled": true, "bind": "127.0.0.1", "port": 5000, "device_id": 0,
    "allow_host_online": true,
    "t3_s": 45, "t5_s": 10, "t6_s": 5, "t7_s": 10, "t8_s": 5, "linktest_s": 60,
    "max_frame_bytes": 1048576
  },
  "output": { "dir": "results", "save_samples": true, "sample_decimation": 10 }
}
```

Parameter notes: 40 points per millimetre and 6 to 32 scan lines are inspired by publicly listed specifications of commercial tools; 150 mm/s scan speed, the noise level, and the limits are project assumptions. A 300 mm line at 40 points per mm is 12,000 samples; six lines is 72,000 samples per wafer.

### 8.2 Wafer model, Stoney's equation and sign convention

Units: SI inside the code (metres, pascals, 1/metre); millimetres, micrometres and MPa only at interfaces.

Symbols: D wafer diameter; t_s substrate thickness; t_f film thickness; M_s biaxial modulus of the substrate, E_s / (1 minus nu_s), about 180.5 GPa for silicon (100) (verify against a reference); k curvature (1/m); k0 curvature before coating.

Convention: the film is on the top surface, height z is measured upward, and a surface that is higher at the edge than at the centre (concave up) has positive curvature and tensile (positive) film stress. A wafer that is higher at the centre (convex up) has negative curvature and compressive (negative) stress.

Forward model (used by the simulator):

```
k        = k0 + 6 * sigma * t_f / (M_s * t_s^2)          (k in 1/m)
kx, ky   = k * (1 - alpha/2), k * (1 + alpha/2)          (alpha = anisotropy)
z(x, y)  = z0 + p*x + q*y + 0.5*kx*x^2 + 0.5*ky*y^2      (p, q = tilts)
line at angle theta through the centre, position s along it:
z(s)     = z0 + (p*cos(theta) + q*sin(theta))*s + 0.5*k(theta)*s^2
k(theta) = kx*cos(theta)^2 + ky*sin(theta)^2
measured = z(s) + noise (+ injected faults)
```

Inverse model (used by the machine):

```
fit each line:   z(s) = a*s^2 + b*s + c        k_line = 2*a
mean curvature:  k_mean = average of k_line over lines
stress:          sigma  = M_s * t_s^2 * (k_mean - k0) / (6 * t_f)
```

With N evenly spaced angles over 180 degrees and N of at least 2, the average of k(theta) equals (kx + ky) / 2, so anisotropy averages out. With a single line the result is biased when anisotropy is not zero; this is documented, not hidden.

Worked example: sigma = minus 180 MPa, t_f = 1 um, t_s = 775 um, M_s = 180.5 GPa. The film adds a curvature of about minus 0.00996 per m (radius of about 100 m); with the initial curvature k0 = 0.002 per m the total measured curvature is about minus 0.00796 per m. The film alone lifts the centre of a 300 mm wafer by about 112 um relative to the edge (0.5 x 0.00996 x 0.15^2 metres). The machine subtracts k0 and must report a stress within 2 percent of minus 180 MPa.

### 8.3 Noise and fault model

| Item | Definition |
|---|---|
| Noise | Independent Gaussian noise per sample, sigma default 0.5 um |
| Spike | With probability `rate` a sample is offset by plus or minus `amplitude_um` |
| Burst | A run of consecutive spikes of stated length |
| Dropout | A gap of missing samples of stated length |
| Stuck | The sensor repeats one value for stated length |
| Drift | Linear baseline drift in um per second |
| Saturation | Readings clipped at plus or minus a range |
| Stage stall | The stage stops for a stated time, triggering the scan-stall watchdog |
| Randomness | One seeded generator per wafer; the seed is written into the output so any run can be reproduced |

### 8.4 Processing pipeline (per wafer)

1. Collect the sample blocks of each line from the sample queue.
2. Drop samples within the edge-exclusion width at both ends.
3. Reject outliers: flag samples whose residual from a running median exceeds k times the median absolute deviation.
4. If the removed fraction is above `outlier_fraction_alarm` or a gap is too long, raise SensorSpikeRateHigh or SensorDropout.
5. Fit z = a s^2 + b s + c by least squares for each line; record k_line, residual RMS and the standard error of a.
6. If RMS is above `fit_rms_limit_um`, raise FitQualityPoor and stop; no result is reported.
7. Average line curvatures; compute per-angle spread and anisotropy.
8. Subtract k0, apply Stoney's equation; propagate the standard error to a one-sigma stress uncertainty.
9. If the stress is not finite or outside the plausibility window, raise StressImplausible; if it is outside the specification window, set the out-of-spec flag.
10. Build the 2D map by polar interpolation; write CSV, JSON and PNG; publish events and status variables.

### 8.5 Output formats

Per-wafer JSON summary (values illustrative):

```json
{
  "run_id": "20260921-153012", "wafer_id": "W042", "slot": 7, "cassette_id": "C001",
  "result": {
    "stress_mpa": -179.4, "stress_unc_mpa": 0.6,
    "mean_curvature_1_per_m": -0.00793, "delta_curvature_1_per_m": -0.00993,
    "delta_radius_m": -100.7, "delta_bow_um": 111.7,
    "anisotropy": 0.049,
    "fit_rms_um_max": 0.51, "removed_fraction": 0.0004, "out_of_spec": false
  },
  "simulation_truth": { "stress_mpa": -180.0, "seed": 12345 },
  "timing_ms": { "scan": 12043, "analysis": 41 },
  "alarms": []
}
```

Per-line CSV columns: `wafer_id, line_index, angle_deg, n_samples, n_removed, curvature_1_per_m, radius_m, se_curvature, rms_um, tilt_um_per_mm, offset_um`.

Sample CSV columns: `wafer_id, line_index, s_mm, z_um_raw, z_um_clean, flag` (flag 0 kept, 1 edge-excluded, 2 outlier).

Log line (JSON lines): `{"t_mono":12.482,"t_wall":"2026-09-21T15:30:24.482Z","level":"info","thread":"controller","component":"machine","event":"state_change","from":"Scanning","to":"Processing","wafer_id":"W042"}`.

Wafer map: PNG, colour scale in micrometres of height, colour bar, wafer outline, edge-exclusion ring, title with wafer id and stress.

### 8.6 SECS/GEM data dictionary

Important: the message layouts below are written from memory of publicly described behaviour. The SEMI standards are sold, not free. Before implementing, cross-check each layout against the documentation of an open-source SECS/GEM library and against the behaviour of that library when it talks to the machine (requirement FR-TOOL-2). Project-defined identifiers (event, alarm, variable and constant numbers) belong to this project, not to any standard.

#### 8.6.1 HSMS frame

A frame is a 4-byte big-endian length, then a 10-byte header, then the body. The length counts the header and body.

| Bytes | Field | Notes |
|---|---|---|
| 0 to 1 | Session ID | Device ID for data messages |
| 2 | Header byte 2 | Data messages: W-bit (reply wanted) in the top bit, stream in the rest |
| 3 | Header byte 3 | Data messages: function |
| 4 | PType | 0 for SECS-II |
| 5 | SType | Message type (below) |
| 6 to 9 | System bytes | Transaction identifier |

| SType | Meaning |
|---|---|
| 0 | Data message |
| 1 and 2 | Select.req and Select.rsp |
| 3 and 4 | Deselect.req and Deselect.rsp |
| 5 and 6 | Linktest.req and Linktest.rsp |
| 7 | Reject.req |
| 9 | Separate.req |

Timers (typical defaults, all configurable): T3 reply timeout 45 s (how long to wait for the answer to one message); T5 connect separation 10 s (delay between connection attempts, host side); T6 control transaction 5 s (wait for the reply to a control message such as Select); T7 not-selected 10 s (a connected but unselected peer is dropped); T8 network inter-character 5 s (wait for the rest of a half-received frame).

#### 8.6.2 SECS-II item header

The first byte is (format code shifted left by 2) OR (number of length bytes, 1 to 3). Length bytes follow, then the data, all big-endian. Format codes in octal:

| Type | Code | Type | Code | Type | Code |
|---|---|---|---|---|---|
| L (list) | 00 | I8 | 30 | U8 | 50 |
| B (binary) | 10 | I1 | 31 | U1 | 51 |
| BOOLEAN | 11 | I2 | 32 | U2 | 52 |
| A (ASCII) | 20 | I4 | 34 | U4 | 54 |
| JIS-8 (unsupported) | 21 | F8 | 40 | F4 | 44 |

For a list, the length is the number of items, not bytes.

#### 8.6.3 Message catalogue

Direction: H to E means host to equipment (machine); E to H the reverse. Priority letters match section 5.

| Message | Dir | Purpose | Body | Pri |
|---|---|---|---|---|
| S1F1 / S1F2 | H to E / E to H | Are you there / on-line data | S1F1 no body; S1F2 is L2 {MDLN A, SOFTREV A} | M |
| S1F13 / S1F14 | H to E / E to H | Establish communications | S1F13 L0; S1F14 is L2 {COMMACK B (0 accepted, 1 denied), L2 {MDLN, SOFTREV}} | M |
| S1F3 / S1F4 | H to E / E to H | Status variable request / data | S1F3 is Ln {SVID U4}; S1F4 is Ln {values} | M |
| S1F15 / S1F16 | H to E / E to H | Request off-line / acknowledge | S1F16 OFLACK B (0 ok) | M |
| S1F17 / S1F18 | H to E / E to H | Request on-line / acknowledge | S1F18 ONLACK B (0 accepted, 1 not allowed, 2 already on-line) | M |
| S2F41 / S2F42 | H to E / E to H | Host command / acknowledge | S2F41 is L2 {RCMD A, Ln {L2 {CPNAME A, CPVAL}}}; S2F42 is L2 {HCACK B, Ln {L2 {CPNAME, CPACK B}}} | M |
| S2F13 / S2F14 | H to E / E to H | Equipment constant request / data | Ln {ECID U4} / Ln {values} | S |
| S2F15 / S2F16 | H to E / E to H | Equipment constant set / acknowledge | Ln {L2 {ECID, ECV}} / EAC B | S |
| S2F33 / S2F34 | H to E / E to H | Define report / acknowledge | L2 {DATAID, Ln {L2 {RPTID, Lm {VID}}}} / DRACK B | S |
| S2F35 / S2F36 | H to E / E to H | Link event report / acknowledge | L2 {DATAID, Ln {L2 {CEID, Lm {RPTID}}}} / LRACK B | S |
| S2F37 / S2F38 | H to E / E to H | Enable or disable event / acknowledge | L2 {CEED BOOLEAN, Ln {CEID}} / ERACK B | S |
| S5F1 / S5F2 | E to H / H to E | Alarm report / acknowledge | L3 {ALCD B, ALID U4, ALTX A up to 120} / ACKC5 B | M |
| S6F11 / S6F12 | E to H / H to E | Event report / acknowledge | L3 {DATAID U4, CEID U4, Ln {L2 {RPTID U4, Lm {V}}}} / ACKC6 B | M |
| S9F1 | E to H | Unrecognized device ID | MHEAD (10 bytes) | M |
| S9F3 | E to H | Unrecognized stream | MHEAD | M |
| S9F5 | E to H | Unrecognized function | MHEAD | M |
| S9F7 | E to H | Illegal data | MHEAD | M |
| S9F9 | E to H | Transaction timer timeout | SHEAD (10 bytes) | M |
| S9F11 | E to H | Data too long | MHEAD | M |

ALCD: top bit 1 means alarm set, 0 means alarm cleared; the lower bits carry a category. HCACK values used: 0 done, 1 unknown command, 2 cannot perform now, 3 invalid parameter, 4 accepted, completion signalled later by an event. START returns 4; STOP, ABORT and CLEAR_ALARM return 0 when accepted. EAC values used: 0 ok, 1 unknown constant, 3 out of range.

S9 usage: S9F1 wrong device ID; S9F3 unknown stream; S9F5 known stream but unknown function; S9F7 body fails to decode or violates the layout; S9F9 the machine's own reply timer (T3) expired; S9F11 body longer than the limit.

#### 8.6.4 Remote commands

| RCMD | Parameters | Effect |
|---|---|---|
| START | WAFER_ID (A) or CASSETTE_ID (A) | Begin a scan of one wafer or a cassette |
| STOP | none | Finish the current wafer, then stop |
| ABORT | none | Stop immediately and discard partial data |
| CLEAR_ALARM | ALID (U4, optional; none clears all clearable) | Clear alarms whose cause has gone |

#### 8.6.5 Events (CEID), reports (RPTID), alarms (ALID)

| CEID | Name | Default report |
|---|---|---|
| 2001 | ControlStateChanged | 3002 |
| 2002 | ProcessStateChanged | 3002 |
| 2003 | CassetteLoaded | 3001 subset |
| 2004 | WaferScanStarted | 3003 |
| 2005 | WaferScanComplete | 3001 |
| 2006 | WaferOutOfSpec | 3001 |
| 2007 | CassetteComplete | 3001 subset |
| 2008 | RunAborted | 3002 |

| RPTID | Contents (in order) |
|---|---|
| 3001 | WAFER_ID (A), SLOT (U1), STRESS_MPA (F4), STRESS_UNC_MPA (F4), CURVATURE (F4), FIT_RMS_UM (F4), OUT_OF_SPEC (BOOLEAN) |
| 3002 | CONTROL_STATE (U1), PROCESS_STATE (U1) |
| 3003 | WAFER_ID (A), SLOT (U1), NUM_LINES (U1) |

| ALID | Name | Condition | Cleared by |
|---|---|---|---|
| 1001 | SensorSpikeRateHigh | Removed fraction above limit | ClearAlarm after next good scan |
| 1002 | SensorDropout | Missing samples or long gap | ClearAlarm |
| 1003 | FitQualityPoor | Residual RMS above limit | ClearAlarm |
| 1004 | ScanStall | No samples for configured time | ClearAlarm |
| 1005 | ProcessingTimeout | Analysis exceeded time limit | ClearAlarm |
| 1006 | QueueOverflow | Sample queue full under drop policy | ClearAlarm |
| 1007 | StressImplausible | Non-finite or outside plausibility window | ClearAlarm |
| 1008 | InternalError | Unexpected condition | Restart |

#### 8.6.6 Status variables (SVID) and equipment constants (ECID)

| SVID | Name | Type |
|---|---|---|
| 4001 | ControlState (0 offline, 1 local, 2 remote) | U1 |
| 4002 | ProcessState (0 idle, 1 scanning, 2 processing, 3 alarm, 4 stopping) | U1 |
| 4003 | CurrentWaferId | A |
| 4004 | CurrentSlot | U1 |
| 4005 | ScanProgressPercent | F4 |
| 4006 | LastStressMPa | F4 |
| 4007 | LastCurvature (1/m) | F4 |
| 4008 | LastFitRmsUm | F4 |
| 4009 | ActiveAlarmCount | U2 |
| 4010 | SoftwareRevision | A |
| 4011 | UptimeSeconds | U4 |
| 4012 | WafersProcessed | U4 |

| ECID | Name | Type | Range |
|---|---|---|---|
| 5001 | EdgeExclusionMm | F4 | 0 to 20 |
| 5002 | NumScanLines | U1 | 1 to 32 |
| 5003 | StressSpecLowMPa | F4 | minus 5000 to 5000 |
| 5004 | StressSpecHighMPa | F4 | minus 5000 to 5000 |
| 5005 | FitRmsLimitUm | F4 | 0.1 to 50 |
| 5006 | PointsPerMm | U1 | 10 to 80 |

### 8.7 Host scenario script format (illustrative; the final grammar goes in docs/scenario-format.md)

```
# scenarios/normal_run.scn
connect 127.0.0.1:5000
select
send   S1F13  L[]
expect S1F14  L[ B[0]  L[ A[*]  A[*] ] ]
send   S2F41  L[ A"START"  L[ L[ A"WAFER_ID"  A"W042" ] ] ]
expect S2F42  L[ B[4]  L[] ]
wait-event 2004  timeout=5s
wait-event 2005  timeout=30s
assert event.STRESS_MPA  within  -180  +-4
disconnect
```

---

## 9. Non-functional requirements

| ID | Category | Requirement | Verify |
|---|---|---|---|
| NFR-PERF-1 | Performance | Analyse one 72,000-sample wafer at real-time factor 0 in at most 100 ms on an Apple M-series laptop (target; baseline recorded first, then improved). | B |
| NFR-PERF-2 | Performance | Version 2 sample path reaches at least 2 times the throughput of version 1; both numbers are published. | B |
| NFR-PERF-3 | Performance | Encode plus decode of small SECS-II messages at least 100,000 per second on one thread (target). | B |
| NFR-PERF-4 | Performance | GUI event loop never stalls for more than 50 ms during a scan. | D, B |
| NFR-REL-1 | Reliability | No crash, hang or sanitizer finding on at least 100,000 randomly mutated frames per CI run, and on malformed SECS-II bodies. | F |
| NFR-REL-2 | Reliability | Soak test: 1,000 wafers at real-time factor 0 with a host connected and random link drops; no deadlock and resident memory growth below 5 percent. | F |
| NFR-CON-1 | Concurrency | Whole test suite is ThreadSanitizer clean; lock hierarchy documented. | CI |
| NFR-PORT-1 | Portability | C++17; warning-free at high warning levels with Apple clang on macOS. Linux and Windows are not supported or tested (D-11). | CI |
| NFR-MNT-1 | Maintainability | clang-format enforced; selected clang-tidy checks in CI; public headers documented; module boundaries of 6.2 enforced by CMake targets. | CI |
| NFR-TST-1 | Testability | Injectable clock, seeded randomness, no sleeps in unit tests. | U |
| NFR-OBS-1 | Observability | Every state change, command, alarm and SECS message is logged with a correlation id. | U |
| NFR-SEC-1 | Security | Listen on loopback by default; enforce maximum frame length and nesting depth; validate every external input; no secrets in the repository; pinned dependency versions. | U, F |
| NFR-DOC-1 | Documentation | README quickstart works from a clean checkout in under 10 minutes; architecture document; short decision records. | D |
| NFR-LIC-1 | Licensing | Own code under a permissive licence; third-party licences listed (Qt under LGPL with dynamic linking, Asio under Boost licence, GoogleTest BSD, JSON library MIT, stb public domain). | D |
| NFR-IP-1 | Intellectual property | No Frontier or FSM names, logos or proprietary material; no SEMI standard text; describe SECS/GEM as a subset based on public information. | D |

---

## 10. Verification plan

### 10.1 Test levels

| Level | What it checks | Tools | Runs |
|---|---|---|---|
| Unit | One class or function alone (codec, fit, Stoney, state machine, queues, config) | GoogleTest | Every push, on macOS |
| Integration | Several modules together (controller with simulated hardware; HSMS with SECS-II and GEM) | GoogleTest with real sockets on loopback | Every push |
| Scenario | Full conversations between `host_sim` and `equipment_cli` from script files | host_sim exit codes in CI | Every push |
| Fault and fuzz | Bad input, dropped links, stalls, random mutation of frames | Custom mutation test; optional libFuzzer via Homebrew LLVM | Every push (short) and nightly (long) |
| Sanitizers | Data races, memory errors, undefined behaviour | ThreadSanitizer build; AddressSanitizer plus UBSan build (separate builds) | Every push |
| Benchmark | Throughput and latency, before and after optimization | Own micro-benchmarks | On demand; numbers copied to README |
| Interoperability | Independent open-source SECS/GEM host against the machine | Python script with the `secsgem` package | Every push if installable, else manual |
| Cross-check | Independent recomputation of stress | Octave or MATLAB script | On demand |

### 10.2 Key test cases

| ID | Test | Covers |
|---|---|---|
| UT-CODEC-1 | Round trip every item type at length boundaries (0, 1, 255, 256, 65535, 65536 bytes) | FR-S2-1 |
| UT-CODEC-2 | Truncated, oversized, inconsistent-length, too-deep and wrong-count inputs are rejected without crash | FR-S2-2 |
| UT-FIT-1 | Noise-free parabola with tilt and offset: exact curvature recovered to numeric precision | FR-PRC-3 |
| UT-FIT-2 | Noisy parabola: curvature within statistical bound; reported standard error matches repeated trials | FR-PRC-3, 6 |
| UT-STONEY-1 | Known-answer: hidden truth of minus 180 MPa recovered within 2 percent on the nominal wafer | FR-PRC-5, SM1 |
| UT-STONEY-2 | Sign convention: convex and concave surfaces give negative and positive stress | FR-PRC-5 |
| UT-ANISO-1 | With N of at least 2 lines, anisotropy does not bias mean curvature; with N of 1 the documented bias appears | FR-PRC-4 |
| UT-FAULT-1 | Spikes, bursts, dropouts, stuck, drift: correct result within 5 percent or the correct alarm | FR-PRC-2, 7, SM2 |
| UT-SM-1 | Every illegal state transition is rejected with the right reason | FR-MC-1 |
| UT-CTRL-1 | Commands from UI, CLI and SECS/GEM sources obey control-state rules | FR-MC-4 |
| UT-HSMS-1 | Partial frames, coalesced frames, maximum length, bad header | FR-HSMS-2 |
| UT-HSMS-2 | Fake-clock tests of T3, T6, T7 and T8 expiry and Linktest behaviour | FR-HSMS-4 |
| IT-GEM-1 | Establish communications, status query, host command, event and alarm round trips | FR-GEM-1 to 6 |
| IT-CTRL-2 | Start during alarm is refused; ClearAlarm allows the next Start | FR-ALM-1 |
| ST-normal_run | Full happy path over HSMS | HOST-3 |
| ST-cassette_run | Multi-wafer run with an empty slot and an out-of-spec wafer | FR-MC-5 |
| ST-alarm_recovery | Sensor fault, alarm, clear, successful rerun | FR-ALM |
| ST-link_loss | Drop the connection mid-scan; machine survives and accepts a new host | FR-HSMS-7 |
| ST-malformed_frames | Garbage, oversized length, wrong device ID, unknown stream and function | FR-GEM-9 |
| ST-bad_commands | Unknown command, missing parameter, command in Local state | FR-GEM-3 |
| ST-t3_timeout | Host never replies to an event; machine handles the timeout | FR-HSMS-4 |
| FT-SHUTDOWN-1 | Stop during scan, during processing and during alarm; all threads join within 2 s | FR-MC-6 |
| FT-FUZZ-1 | 100,000 mutated frames: no crash, hang or sanitizer report | NFR-REL-1 |
| FT-SOAK-1 | 1,000 wafers with random link drops | NFR-REL-2 |
| BM-QUEUE-1 | Mutex queue (v1) versus lock-free ring (v2) at 1, 2 and 4 producers where applicable | NFR-PERF-2 |
| BM-ANALYSIS-1 | Serial versus thread-pool per-line fits | FR-PRC-9 |
| XT-SECSGEM-1 | `secsgem` host performs S1F13, S1F3, S2F41 and receives S6F11 and S5F1 | FR-TOOL-2, SM8 |
| XT-OCTAVE-1 | Octave stress from exported CSV matches machine result within 0.5 percent | FR-TOOL-1 |

### 10.3 Definition of Done (project level)

- All Tier 1 requirements pass their verification methods, and the results are visible in CI.
- CI is green on macOS; ThreadSanitizer and ASan/UBSan builds are clean.
- The README contains a plain-English summary, architecture diagram, quickstart, demo GIF, real measured numbers and known limitations.
- A demo video of at most two minutes exists and the link is on the CV.
- The local demo runs with one command (`scripts/demo.sh`).
- Git history shows the standalone version, the SECS/GEM integration, at least one refactor and one measured optimization. Any bug story in the README is a real one.
- The CV lists only what was built and can be explained in an interview.

---

## 11. Technology stack, tools and decisions

| Area | Choice | Notes |
|---|---|---|
| Language | C++17 | Apple clang on macOS is the only compiler used; no reliance on C++20 threads |
| Build | CMake with presets | One build description for all platforms |
| GUI | Qt 6 Widgets (Homebrew on Mac; official installer or aqtinstall in CI) | Operator panel only; core does not depend on Qt |
| Networking | Standalone Asio (header only) | Keeps the core and the headless demo free of Qt |
| JSON | nlohmann/json | Configuration, results, logs |
| PNG output | stb_image_write | Headless wafer map |
| Tests | GoogleTest via CMake FetchContent | Unit and integration tests |
| Sanitizers | ThreadSanitizer; AddressSanitizer plus UBSan (separate builds) | Both supported by clang on Apple Silicon |
| Profiling | Instruments (macOS) | Valgrind and GDB are not used on Apple Silicon |
| Static analysis | clang-tidy, clang-format | Enforced in CI |
| Coverage | llvm-cov or gcov | Reported in CI |
| CI | GitHub Actions on macOS | Builds, tests, sanitizers, artifacts, releases |
| Container | None. Docker would be a Linux build, which is out of scope (D-11) | |
| Cross-check | GNU Octave or MATLAB | Claim MATLAB only if run in real MATLAB |
| Interop | Python with `secsgem` | Independent SECS/GEM host |
| C# host (Tier 2) | .NET on macOS, console application | Legitimate C# experience; not WPF |
| C# console (Tier 3) | WPF or WinForms with MVVM | Only on Windows |

### 11.1 Decision records

| ID | Decision | Alternatives | Reason |
|---|---|---|---|
| D-01 | C++17 | C++20 | Safer across compilers; jthread support varies |
| D-02 | Core library independent of Qt | Qt Core everywhere | Headless demos, smaller container, faster CI |
| D-03 | Standalone Asio for TCP and timers | Qt Network; raw sockets | Portable, tested, works without Qt |
| D-04 | SECS/GEM as an optional module (`SSIM_ENABLE_SECSGEM`) | Built into the core | Matches "integrate SECS/GEM" and the optional nature seen in public product pages |
| D-05 | Machine is the passive HSMS entity; host connects | Machine connects out | Typical arrangement in fab practice |
| D-06 | SI units inside, engineering units at interfaces | Mixed units | Avoids unit bugs in the physics |
| D-07 | Controller is the single writer of state | Shared state with locks | Fewer races; easier reasoning |
| D-08 | Injectable clock everywhere | Real sleeps in tests | Fast, exact timer tests |
| D-09 | Queues behind an interface | Direct std::queue | Allows the v1 to v2 optimization story |
| D-10 | Seeded simulation with hidden truth | Random unseeded data | Repeatable known-answer tests |
| D-11 | The only supported platform is macOS on Apple Silicon, the developer's own machine (decided 24 Sep 2026; first Windows was dropped, then Linux) | Also Linux (gcc) and Windows (MSVC), Docker | Only macOS is available and used, and no claim may rest on a platform that was never built and run. Linux and Windows can be added later; the portability fixes already made (for GCC and libstdc++) stay, but nothing claims Linux or Windows support, and the Docker demo, Linux archive and multi-arch image are cut. |

---

## 12. Delivery and deployment

A desktop and headless application is not deployed like a website. Delivery therefore means showing it running and handing over something the reviewer can run.

### 12.1 Level 1 (must)

- README with plain-English summary, architecture diagram, quickstart, results table, limits and a short "how to explain this in an interview" note.
- Demo GIF in the README and a video of at most two minutes, unlisted, linked from the README and the CV. Storyboard: start the panel; switch to Remote; run the host script; watch a scan; show the result and map; trigger a sensor alarm; clear it; show tests and CI badges.
- CI badges (macOS, sanitizers), test count and benchmark table.

### 12.2 Level 2 (should)

- GitHub Release built by CI on a version tag: macOS app (macdeployqt) only (D-11). Unsigned builds show security warnings; the README explains how to open them.
- `scripts/demo.sh`: one command that starts `equipment_cli serve`, runs the normal-run scenario with `host_sim`, prints the message trace and leaves the wafer map and CSV in a results folder. No Docker (D-11).

### 12.3 Level 3 (could; probably skipped)

A WebAssembly build of the panel or a hosted status page. It would lack the SECS/GEM link (browsers cannot open raw TCP), needs special headers for threads, and adds web work. Not planned. The machine's port is never exposed to the public internet.

### 12.4 Design consequence

The code is split into a core library, a headless command-line program and a thin Qt layer, from day one. The demo, scenarios, tests and GUI all share the same core. The CI workflow is created on day one so build problems appear early.

---

## 13. Plan and milestones (21 to 28 September 2026)

| Day | Date | Work | Exit criteria |
|---|---|---|---|
| D1 | Mon 21 Sep | Repository skeleton, CMake, CI on macOS with a passing test; config, clock, queues (v1), event bus, logger; wafer model and simulated hardware; scope frozen | CI green; UT-STONEY-1 forward model tests pass |
| D2 | Tue 22 Sep | Scan thread, processing pipeline, controller and alarms, exports, CLI | Standalone run produces JSON, CSV, PNG; SM1 and SM2 met; tag v0.1 |
| D3 | Wed 23 Sep | Qt panel, wafer map, control modes, progress; README draft; cassette loop if time | Panel usable; first GIF |
| D4 | Thu 24 Sep | SECS-II codec with fuzz and round-trip tests; HSMS framing, session, timers on fake clock | UT-CODEC and UT-HSMS pass |
| D5 | Fri 25 Sep | GEM module, integration into the app as a separate step, host_sim and scenarios, interop test | ST scenarios pass; XT-SECSGEM-1 passes; tag v0.2 |
| D6 | Sat 26 Sep | Refactor commit; sanitizer findings fixed; v2 ring buffer and parallel fits with benchmarks; Octave check; local demo script; macOS release workflow | Numbers recorded; tag v0.3 |
| D7 | Sun 27 Sep | README and video final; CV final; submit | Application sent |
| Buffer | Mon 28 Sep | Deadline day; no planned work | Only emergencies |

Commit story to make "maintain, fix, refactor, optimize" visible: v0.1 standalone machine with simple queue and serial fits; v0.2 SECS/GEM integrated as a module; a refactor commit (for example extracting the controller and state machine); a fix commit only for a real bug actually found by a sanitizer or test; a performance commit with before and after numbers; v0.3 tag. Do not invent a bug story.

Parallel CV tasks (small, daily): rewrite the internship bullets against the job posting; add competitive-programming rating or solved count if available; keep the thesis as the "research-based project" line; state willingness to travel only if true; drop web-related academic projects as advised by the company engineer.

---

## 14. Risks and mitigations

| ID | Risk | Likelihood | Impact | Mitigation |
|---|---|---|---|---|
| R1 | Seven days is not enough | High | High | Tiers and cut order in section 5; standalone machine first; CI early |
| R2 | SECS/GEM details wrong because standards are paywalled | Medium | High | Cross-check with open-source library docs and behaviour; interop test; describe as a subset |
| R3 | Threading bugs (races, deadlocks) | Medium | High | Single-writer rule, queues only, ThreadSanitizer, fake clock, shutdown tests |
| R4 | A build problem appears late | Medium | Medium | CI on macOS from day one; portable libraries only (Linux and Windows are out of scope, D-11) |
| R5 | Qt installation or packaging trouble | Medium | Medium | Homebrew Qt locally; CI installs Qt; headless mode keeps the demo independent of Qt |
| R6 | Physics or sign-convention mistakes | Medium | Medium | Known-answer tests; Octave cross-check; convention documented and tested |
| R7 | Timer tests are flaky | Medium | Low | Injectable clock; no real sleeps in unit tests |
| R8 | Scope creep into extras | High | Medium | Frozen scope; Tier 2 only when Tier 1 is green |
| R9 | Claims on the CV cannot be defended | Medium | High | Claim only what is built; rehearse explanations of every design choice |
| R10 | Reviewer cannot or will not run the code | High | Medium | Video, GIF, CI badges, one-command demo script, macOS release build |
| R11 | Interop library not installable or differs | Low | Medium | Pin a version; fall back to a scripted host and manual protocol review |

---

## 15. Traceability to the job posting

| Job posting line | Requirements | Evidence a reviewer can see |
|---|---|---|
| High-performance, multithreaded machine-control software | 6.3, 6.4, FR-SCN, FR-MC, FR-PRC-9, NFR-PERF, NFR-CON-1 | Threading design, benchmarks, ThreadSanitizer badge |
| Maintain: features, bug fixing, refactoring, optimization | 13 (commit story), FR-CFG-3, FR-LOG-3 | Git history and tags; before and after numbers |
| Integrate SECS/GEM into a fully functional system | 6.2, D-04, FR-HSMS, FR-S2, FR-GEM, FR-MC-3 | Module boundary; machine runs without it; interop test |
| C++ and OOP | 6.6, FR-HW-1 | Interfaces, patterns table, code |
| STL | Whole core | Containers, algorithms, smart pointers |
| Excellent data structures and algorithms | Queues (mutex and lock-free ring), scheduler, least-squares fit, median and MAD, polar interpolation | Unit tests and complexity notes |
| C++ Qt | FR-UI | Panel, demo GIF |
| C# WinForm/WPF | FR-HOST-4 (C# host), FR-HOST-5 (WPF, Windows only) | Claim only what is built |
| Visual Studio and MFC | Not planned; no MSVC or Windows build | No claim of MFC |
| Data communications and computer networks | FR-HSMS, FR-S2, FR-HW-7 (framing and CRC), NFR-SEC-1 | Protocol code, traces, fuzz tests |
| Operating systems | 6.3, 6.4, FR-MC-6 | Thread model, shutdown protocol, sanitizer results |
| Design patterns | 6.6 | Patterns table and code |
| Competitive programming and problem solving | Algorithms above; CV line | Profile link if available |
| MATLAB (plus) | FR-TOOL-1 | Script in the repository; claim only if run in MATLAB |
| Research-based work and learning new technology | Physics validation, learning SECS/GEM in days, thesis on the CV | README notes; thesis line |
| Fluent English | README, docs, video narration | Writing quality |
| Willing to travel | CV statement | Only if true |

---

## 16. Assumptions, constraints and open questions

### 16.1 Assumptions

- A1. Development on an Apple Silicon Mac; Xcode command-line tools, Homebrew, CMake and Qt 6 are installable.
- A2. No Windows machine is assumed; Tier 3 depends on getting one or a virtual machine.
- A3. One developer, about seven working days, public sources only.
- A4. GitHub public repository with free Actions minutes; GitHub Container Registry available.
- A5. The `secsgem` Python package installs and works as a host; otherwise the interop step is manual.
- A6. The values in section 8.1 are project assumptions inspired by public specifications, not Frontier's real parameters.

### 16.2 Constraints

- Application deadline 28 September 2026; only Bangladeshi citizens are eligible.
- No hardware; no proprietary documents; no misleading claims about certification or affiliation.

### 16.3 Open questions

| ID | Question | Owner | Needed by |
|---|---|---|---|
| Q1 | Can a Windows PC or VM be used, so Tier 3 (C# WPF) is real? | Habib | D5, **answered 24 Sep: no. Tier 3 is excluded and Windows is out of scope (D-11).** |
| Q2 | Do the message layouts in 8.6 match the open-source library's documentation and behaviour? | Habib, at D4 | D4 |
| Q3 | Confirm standalone Asio (D-03) or fall back to Qt Network. | Habib | D1 |
| Q4 | Which existing academic projects stay on the CV (thesis and systems-flavoured work), and which are removed? | Habib | D6 |
| Q5 | What exactly were the internship duties, so the bullets can be aligned honestly? | Habib | D6 |
| Q6 | Repository name and licence (MIT suggested). | Habib | D1 |
| Q7 | Competitive-programming profile and willingness to travel: include? | Habib | D6 |

---

## 17. Glossary

| Term | Meaning |
|---|---|
| Wafer | Thin disc of silicon on which chips are made |
| Film | Thin coating deposited on a wafer |
| Film stress | Force per area in the film; tensile (positive) pulls inward, compressive (negative) pushes outward |
| Curvature, bow | How much the wafer bends; bow is the height difference between centre and edge |
| Stoney's equation | Relation between film stress and the change in wafer curvature |
| Metrology | Measurement science; here, measuring wafers and films |
| C2C | Cassette to cassette; automated loading of wafers from a carrier and back |
| FOUP | Sealed carrier holding a stack of 300 mm wafers |
| Load port, slot map | The carrier interface and the list of which slots hold wafers |
| Host, MES | The factory computer that controls machines (Manufacturing Execution System) |
| Equipment | The machine; in this project the program you write |
| SEMI | Industry body that publishes the standards |
| SECS-II (E5) | Message format standard |
| HSMS (E37) | TCP-based transport for SECS-II messages |
| GEM (E30) | Standard behaviour model for equipment |
| Stream, Function (SxFy) | Message category and type; odd function is a request, the next even is the reply |
| W-bit | Flag in the header meaning a reply is expected |
| System bytes | Four-byte transaction id matching a reply to its request |
| Select, Linktest, Separate | HSMS session hello, heartbeat and goodbye |
| SV, EC | Status variable (read-only data) and equipment constant (settable) |
| CEID, RPTID, ALID, RCMD | Event id, report id, alarm id, remote command name |
| HCACK | Host command acknowledge code |
| MAD | Median absolute deviation, a robust spread measure |
| SPSC ring buffer | Lock-free queue with a single producer and single consumer |
| ThreadSanitizer, ASan, UBSan | Compiler tools that find data races, memory errors and undefined behaviour |
| MVVM | Model-View-ViewModel pattern used with WPF |

---

## 18. References

Public and third-party sources consulted for context (not proprietary):

- Frontier Semiconductor public site: home, products, and product pages for the 128L C2C, 128 C2C, 413 C2C, 900 C2C and 900TC-VAC (frontiersemi.com/center/).
- Metrosemi reseller listing for the FSM 128L (metrosemi.com/fsm-128l/) and CAE listings for the 128L C2C (unofficial, secondhand).
- SEMI E5 (SECS-II), E30 (GEM) and E37 (HSMS): purchased documents; not reproduced here. Use public summaries and open-source library documentation instead.
- G. G. Stoney, "The tension of metallic films deposited by electrolysis", Proceedings of the Royal Society A, 82, 1909 (verify citation details).
- `secsgem` open-source Python SECS/GEM library and its documentation.
- Documentation for Qt 6, standalone Asio, GoogleTest, nlohmann/json and stb.

---

## Appendix A. How to build, run and test (macOS)

Names are placeholders until the code exists.

```
xcode-select --install
brew install cmake qt

cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=$(brew --prefix qt)
cmake --build build -j
ctest --test-dir build --output-on-failure

# standalone machine, headless
./build/equipment_cli --config config/default.json --out results

# machine with SECS/GEM, then the scripted host in a second terminal
./build/equipment_cli --config config/default.json --port 5000
./build/host_sim --connect 127.0.0.1:5000 --script scenarios/normal_run.scn

# abuse test: garbage bytes must not stop the machine
printf '\x00\x00\x00\x05garbage' | nc 127.0.0.1 5000

# thread-safety build (separate build directory)
cmake -S . -B build-tsan -DCMAKE_CXX_FLAGS="-fsanitize=thread -g" -DCMAKE_PREFIX_PATH=$(brew --prefix qt)
cmake --build build-tsan -j && ctest --test-dir build-tsan --output-on-failure

# benchmarks and cross-check
./build/bench_queue
octave scripts/check_stress.m results/<run_id>/W042/samples.csv

# containerised demo
scripts/demo.sh
```

## Appendix B. CV usage rules and draft bullets

Rules:

- Put numbers in a bullet only after measuring them. Bracketed values below are placeholders.
- Never write WPF, WinForms, MFC or "certified" unless it is true.
- Describe SECS/GEM as "a subset based on publicly documented behaviour".
- Use the posting's own words in the skills section: multithreading, STL, data structures and algorithms, object-oriented programming, computer networks, operating systems, design patterns, Qt, MATLAB (only if run in MATLAB).
- One page, one column, plain headings, text-based PDF or DOCX; repository and video links as plain text in the header.

Draft bullets (fill after measuring):

- Designed and implemented a multithreaded C++17 machine-control simulator for a wafer film-stress metrology tool (controller, scan, processing pool, HSMS I/O and logger threads); replaced a mutex queue with a lock-free ring buffer, raising sample throughput [X]x and cutting per-wafer analysis from [a] ms to [b] ms.
- Integrated a SECS/GEM subset (HSMS session and timers, SECS-II codec, GEM communication and control states, events, alarms, remote commands) as an optional module; verified against an independent open-source host; [N] automated tests, ThreadSanitizer and AddressSanitizer clean.
- Built a Qt 6 operator panel and a headless CLI on a shared core library; CI builds and tests on macOS; one-command local demo.
- Validated film-stress computation (Stoney's equation) with known-answer tests within [x] percent under sensor noise and injected faults; cross-checked results with an Octave/MATLAB script.
- Applied fuzzing and fault injection to the protocol stack ([N] mutated frames, zero crashes) and fixed [real bug description, if any].
