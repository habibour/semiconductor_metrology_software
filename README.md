# StressScan-Sim

A cassette-to-cassette wafer film-stress metrology tool, simulated end to end in C++17, as a portfolio project.

[![CI](https://github.com/habibour/semiconductor_metrology_software/actions/workflows/ci.yml/badge.svg)](https://github.com/habibour/semiconductor_metrology_software/actions/workflows/ci.yml)
[![Sanitizers](https://github.com/habibour/semiconductor_metrology_software/actions/workflows/sanitizers.yml/badge.svg)](https://github.com/habibour/semiconductor_metrology_software/actions/workflows/sanitizers.yml)
[![Release](https://github.com/habibour/semiconductor_metrology_software/actions/workflows/release.yml/badge.svg)](https://github.com/habibour/semiconductor_metrology_software/actions/workflows/release.yml)

**Status: tag `v0.3`.** Standalone machine, headless CLI and Qt panel all work. SECS/GEM is an optional module &mdash; a subset of publicly documented behaviour (SECS-II codec, HSMS, GEM states/events/alarms/remote commands), with a host simulator, 7 scenario scripts and an interop test against an independent open-source host. Not built: cassette loop, equipment constants, dynamic reports. Not certified or complete. One open gap: the Octave/MATLAB cross-check script exists but hasn't actually run in Octave (see Results).

No Frontier or SEMI names, logos or standard text anywhere in this repository.

## What it is

A washing machine with an optional phone app: the machine scans alone, the app can start it remotely. The "clothes" are silicon wafers; the "cycle" is a laser scan that measures how much a thin film bends the wafer, turned into a stress number with Stoney's equation. A simulator invents noisy readings from a hidden "true" stress and the software has to recover it &mdash; so every error is measurable against a known answer.

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

![Operator panel: a scan, an alarm, clearing it, and Online-Remote](docs/media/panel_demo.gif)

W001 scans cleanly (stress -180.0 MPa recovered), W002 raises and clears a sensor alarm, Online-Remote disables operator Start. **Not a screen recording** &mdash; real panel widgets rendered offscreen and driven by a script (`scripts/make_demo_gif.sh`). Day 6 was backend-only, so this GIF is still accurate as of `v0.3`.

**Video:** a two-minute walkthrough is linked here once recorded.

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
