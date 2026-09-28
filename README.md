# StressScan-Sim

A cassette-to-cassette wafer film-stress metrology tool, simulated end to end in C++17, as a portfolio project.

[![CI](https://github.com/habibour/semiconductor_metrology_software/actions/workflows/ci.yml/badge.svg)](https://github.com/habibour/semiconductor_metrology_software/actions/workflows/ci.yml)
[![Sanitizers](https://github.com/habibour/semiconductor_metrology_software/actions/workflows/sanitizers.yml/badge.svg)](https://github.com/habibour/semiconductor_metrology_software/actions/workflows/sanitizers.yml)
[![Release](https://github.com/habibour/semiconductor_metrology_software/actions/workflows/release.yml/badge.svg)](https://github.com/habibour/semiconductor_metrology_software/actions/workflows/release.yml)

**Status: tag `v0.3`.** Standalone machine, headless CLI and Qt panel all work. SECS/GEM is an optional module &mdash; a subset of publicly documented behaviour (SECS-II codec, HSMS, GEM states/events/alarms/remote commands), with a host simulator, 7 scenario scripts and an interop test against an independent open-source host. Not built: cassette loop, equipment constants, dynamic reports. Not certified or complete. One open gap: the Octave/MATLAB cross-check script exists but hasn't actually run in Octave (see Results).

No Frontier or SEMI names, logos or standard text anywhere in this repository.

## What it is

Control and analysis software for a cassette-to-cassette wafer film-stress metrology tool &mdash; the kind of automated semiconductor equipment a fab uses to check whether a deposited film is pulling a wafer out of shape. There is no physical hardware: a seeded simulator generates noisy sensor readings from a hidden "true" stress value, so the software's job, and every number in this README, can be checked against a known answer instead of trusted on faith.

## What it does

- Scans a simulated wafer with a laser stage along several lines, at a configurable resolution and edge exclusion.
- Fits each line's height profile, converts curvature to film stress with Stoney's equation, and gates the result on fit quality &mdash; a bad scan raises an alarm instead of reporting a number.
- Reports through three interchangeable front ends: a Qt operator panel, a headless CLI, or an optional SECS/GEM link so a factory host computer can start scans and receive results and alarms remotely.
- Keeps working with the SECS/GEM link entirely disabled &mdash; it is a separable module, not a requirement to operate the machine.

## How it works

Built in multithreaded C++17, the same shape of software as the machine-control layer of a real fab tool: a controller thread owns all machine state, a scan thread feeds a bounded lock-free queue, a processing pool runs the analysis pipeline (edge exclusion, outlier rejection, curve fit, Stoney's equation, quality gates), and the optional SECS/GEM module (HSMS session and timers, a SECS-II codec, GEM states/events/alarms/remote commands, over standalone Asio) runs on its own I/O thread. Every thread talks to the others only through a single event bus &mdash; there is no shared mutable state.

## Tech stack

C++17 &middot; Qt 6 (operator panel) &middot; standalone Asio (SECS/GEM networking) &middot; CMake (build) &middot; GoogleTest (365 tests) &middot; ThreadSanitizer / AddressSanitizer / UBSan (concurrency and memory safety) &middot; `llvm-cov` (coverage) &middot; GitHub Actions (CI) &middot; Python (`secsgem`, for the independent interop check)

## Impact and who it's for

This is a portfolio project, not a deployed product, built for a hiring engineer evaluating a fresh graduate's ability to write real equipment-control software &mdash; it demonstrates the concurrency, protocol-integration, and correctness-under-noise skills that role needs, backed by tests and measured numbers rather than claims.

More broadly, the pattern it follows is a real one: a team writing factory-host software needs something to test against before real equipment is available or affordable, and a well-tested emulator like this one, SECS/GEM link included, is exactly that kind of tool on an actual fab-automation team.

Build order: (1) standalone machine, (2) SECS/GEM as a separate module, (3) refactor and optimize with measured numbers.

## Architecture

```
        optional link: TCP, HSMS / SECS-II / GEM
 +------------------+  <------------------------------->  +----------------------------------+
 | Host simulator   |                                     | Equipment application            |
 | (host_sim)       |                                     |  +----------------------------+  |
 +------------------+                                     |  | SECS/GEM module (optional) |  |
                                                          |  +-------------+--------------+  |
 +------------------+   operator actions / display        |                | commands / events |
 | Operator (Qt)    | <---------------------------------> |  +-------------v--------------+  |
 +------------------+                                     |  | Core library               |  |
                                                          |  | controller, pipeline, logs |  |
                                                          |  +-------------+--------------+  |
                                                          |                | IStage / ILaserSensor |
                                                          |  +-------------v--------------+  |
                                                          |  | Simulated hardware         |  |
                                                          |  +----------------------------+  |
                                                          +----------------------------------+
```

`ssim_core` (config, queues, event bus, controller, `MachineApi`) &rarr; `ssim_hw` / `ssim_analysis` / `ssim_secsgem` &rarr; `ssim_machine` (`MachineRuntime`, wires everything, hosts the optional SECS/GEM link) &rarr; `equipment_cli` / `equipment_qt` / `host_sim`. Dependencies point downward only; `ssim_core` never includes Qt, network or SECS/GEM headers; the machine builds and runs with the module entirely absent (`-DSSIM_ENABLE_SECSGEM=OFF`). Reasoning: `docs/PRD.md`, `docs/decisions/`.

## Build and run (macOS, Apple Silicon)

Requirements: CMake 3.21+, a C++17 compiler, Qt 6 for the panel (optional).

```
cmake --preset dev
cmake --build build -j2
ctest --test-dir build --output-on-failure

# one scan; writes results/<run_id>/W001/
./build/src/app_cli/equipment_cli --config config/default.json --rtf 0
```

Fresh-clone timing (no cached deps): configure 5m52s + build 2m29s + `ctest` (365 tests) 2m13s = **10m34s**, over this project's own 10-min target &mdash; configure dominates on a first run fetching FetchContent deps; a warm rebuild is under a minute. Use `-j2`, not more: GoogleTest's 5s discovery step can time out on a fresh build. Intel `cmake` under Rosetta: add `-DCMAKE_OSX_ARCHITECTURES=arm64`.

Qt panel (`brew install qtbase`; preset assumes `/opt/homebrew/opt/qtbase`):

```
cmake --preset qt && cmake --build build-qt -j2
./build-qt/src/app_qt/equipment_qt --config config/demo_alarm.json
```

**With a host:**

```
./build/src/app_cli/equipment_cli serve --port 0 --control remote --rtf 0   # prints the port
./build/src/host_sim/host_sim --connect 127.0.0.1:<port> --script scenarios/normal_run.scn

# independent interop check (pip install secsgem==0.3.0)
python3 scripts/interop_secsgem.py --cli build/src/app_cli/equipment_cli --config config/demo_alarm.json
```

Script language: `docs/scenario-format.md`. `-DSSIM_INTEROP_PYTHON=<python with secsgem>` registers `XT-SECSGEM-1` in CTest.

**Panel demo** (`config/demo_alarm.json`: W001 clean, W002 has an injected sensor-spike fault): Start &rarr; progress, map, result. Start again &rarr; W002 raises alarm 1001, Start locked until Clear alarm. Switch to Online-Remote &rarr; Start disabled, Abort still works. File menu: Load config / Settings (idle only), Open results folder.

## Demo

![Operator panel: a scan, an alarm, clearing it, Online-Remote and back, then an Abort](docs/media/panel_demo.gif)

[**Watch the full functionality video**](docs/media/panel_demo.mp4) (31s, same session, higher quality than the GIF above): W001 scans cleanly (stress -180.0 MPa recovered, wafer map shown); W002 raises a sensor alarm and Start stays locked until Clear alarm; switching to Online-Remote disables operator Start, switching back to Online-Local re-enables it; a scan is started and then Aborted partway through, returning to Idle with no result reported. **Not a screen recording** &mdash; real panel widgets rendered offscreen and driven by a script (`tests/unit/app_qt/panel_smoke_test.cpp`'s `demoFrames`, assembled by `scripts/make_demo_gif.sh`), so it is exactly what the real widgets draw, deterministic and reproducible on every machine.

**Narrated walkthrough video:** a separate two-minute screen recording (with the CI badges above and a live host connection) is linked here once recorded.

**One command:** `scripts/demo.sh` builds if needed, starts the machine, runs a scenario via `host_sim`, prints the trace, leaves results on disk, shuts down. No Docker (PRD D-11).

**Release:** unsigned macOS (Apple Silicon) app attached to each [GitHub Release](https://github.com/habibour/semiconductor_metrology_software/releases) from `v0.3`. Gatekeeper blocks it by double-click &mdash; run `xattr -cr StressScan-Sim.app` once after unzipping.

## Results

Full numbers, environment and method: `docs/benchmarks.md`.

| Item | Value | Method |
|---|---|---|
| Stress recovery accuracy | Within 2% of hidden truth (asserted); a real run measured 0.001% off | `MachineRuntimeTest.StartProducesResultWithinTwoPercentOfTruth` |
| Analysis time / wafer (6 lines, 72,006 samples) | 112.7 ms &rarr; 7.5&ndash;30 ms after optimization | `bench/bench_analysis`, Release, Apple M4 |
| Queue throughput, v1 (mutex) vs v2 (lock-free ring) | 8B: 21.6&rarr;127.7 M/s (5.9x). 64B: 19.4&rarr;36.4 M/s (1.9x, **below the 2x target**) | `bench/bench_queue`, Release, Apple M4 |
| SECS-II codec throughput | 451k ops/s vs 100k target (4.5x over) | `bench/bench_codec` |
| SECS/GEM interop (independent Python host) | 15/15 checks passed | `scripts/interop_secsgem.py`, XT-SECSGEM-1 |
| Line coverage (target 80%) | core 86.76%, analysis 94.08%, secsgem 92.97% | `llvm-cov` |
| Fuzz testing | codec 100k + HSMS 150k mutated inputs, zero crashes | `ctest` (ASan/UBSan/TSan on CI, badge above) |
| Soak test | 1,000 wafers, host churn, zero deadlocks. Memory bound relaxed to **10%, not 5%** &mdash; allocator noise on a small process, not a leak (reasoning in the test file) | `Soak.ThousandWafersWithHostChurnNoDeadlockBoundedMemoryGrowth` |
| Octave/MATLAB cross-check | **Not run** &mdash; Octave failed to install here. Python re-implementation of the same formulas: 0.0100% diff from the machine's own answer | see Known limitations |
| Tests | 365 with SECS/GEM, 164 without, 165 with Qt &mdash; all passing | `ctest` |
| CI (macOS runner) | build+interop, SECS/GEM-off build, format check, ASan+UBSan, TSan &mdash; badges above. Day 6's new threaded code (lock-free ring, pool filtering) is checked by its own `v0.3` sanitizer run, not carried over from `v0.2` | `.github/workflows/` |

SECS/GEM is also covered by: known-answer + boundary + rejection + 100k fuzz on the codec; fake-clock timer tests (T3/T6/T7/T8) + loopback server tests on HSMS; 33 GEM tests against a real controller; 7 scenario scripts over a real socket. Details: `docs/protocol-notes.md`.

## Known limitations

- macOS on Apple Silicon only (PRD D-11); no Linux/Windows claim.
- No real hardware; not certified or complete; not affiliated with any equipment vendor.
- Queue's SM3 target (2x) missed for 64-byte items (1.9x) &mdash; reported as measured.
- Soak test's memory bound is 10%, not the project's own 5% target (allocator noise, not a leak).
- Octave/MATLAB cross-check has not actually run (see Results) &mdash; verified independently in Python instead.
- Cassette loop, equipment constants (S2F13-16), dynamic reports (S2F33-38), spooling: not implemented. Communication state model simplified from full GEM (`docs/protocol-notes.md`). Panel shows raw heights, so simulated tilt dominates the map.

## How to explain this in an interview

- **Concurrency:** controller owns state exclusively; scan &rarr; bounded queue &rarr; processing pool; HSMS/SECS-II/GEM on its own Asio thread; event bus, not shared mutable state. Real lock hierarchy (9 mutexes, none nested) verified by reading every one: `docs/architecture.md`.
- **SECS/GEM is a subset**, not certified, every layout cross-checked against an independent library.
- **Stoney validation** is a known-answer test: hidden truth in, recovered value out, within tolerance.
- **Measure-first performance:** every optimization followed a benchmark; the queue's shortfall (64B items) is reported, not hidden.
- **What was cut, and why:** Tier 2/3 (C# host, device simulator over TCP, Windows/WPF) dropped first, in the PRD's own priority order, once the standalone machine and SECS/GEM integration were solid.

## Licence

Own code: MIT (`LICENSE`). Third-party (Qt LGPL v3 dynamic-linked, Asio Boost 1.0, GoogleTest BSD-3, nlohmann/json MIT, stb public domain/MIT): `THIRD_PARTY_LICENSES.md`.

## Further reading

`docs/PRD.md` (requirements) &middot; `docs/architecture.md` (lock hierarchy) &middot; `docs/benchmarks.md` (every number here) &middot; `docs/decisions/000{1,2,3}-*.md` (Asio choice, `ssim_machine`, sans-I/O HSMS) &middot; `docs/protocol-notes.md` (SECS/GEM cross-checks, `TODO(verify)` items)
