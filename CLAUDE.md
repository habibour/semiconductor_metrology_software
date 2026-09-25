# CLAUDE.md: StressScan-Sim

Guidance for Claude (and any human contributor) working in this repository. Read it fully at the start of every session. The full requirements live in `docs/PRD.md`; this file is the short operating manual.

## 1. Read this first

**Current day: Day 7** (README, video, CV, submit; Day 6 work is carried over, see the Status bullet). Set this to `Day 1` through `Day 7` before starting a session, matching the plan in PRD section 13. Habib edits this line by hand; Claude never changes it on its own.

- Source of truth: `docs/PRD.md` (v0.1, written 21 Sep 2026). If code and PRD disagree, stop, say so, and either fix the code or update the PRD on purpose in the same change. Never let them drift silently.
- Day-by-day breakdown: `docs/specs/day-N-spec.md` filters the PRD down to exactly what day N covers (requirement IDs, tests, exit criteria); `docs/plans/day-N-plan.md` is the ordered, file-level implementation checklist for that spec. Both exist for Day 1 through Day 7, mirroring PRD section 13. They are an index into the PRD, not a replacement for it.
- Status (25 Sep 2026): Days 1 to 5 are built, committed and green in CI (macOS runner: build and test, the SECS/GEM-off build, clang-format, ASan+UBSan, TSan). Tag `v0.2` is on `3ea3fa3`. 342 tests with the SECS/GEM module, 144 without; interop with the independent `secsgem` host passes 15 of 15 checks; 7 of the 8 PRD scenarios pass. **Not built (Day 6 carry-over):** moving `equipment_cli` onto `MachineRuntime` (the CLI still differs from the panel on sensor alarms), the v2 ring buffer and benchmarks, coverage, the Octave check, `scripts/demo.sh`, the macOS release workflow, the cassette loop, and equipment constants / dynamic reports / spooling. Day 7's README results table and CV numbers may only use what has been measured; anything unmeasured is written as "not measured". Only macOS on Apple Silicon is supported (PRD D-11). The demo GIF (`docs/media/panel_demo.gif`) is rendered offscreen from the real panel, not a screen recording; the two-minute video is still to be recorded by Habib.
- Owner: Md. Habibur Rahman (Habib), a CSE graduate applying for the Software Engineer role at Frontier Semiconductor Bangladesh Ltd. Application deadline: 28 Sep 2026. Target submit date: 27 Sep 2026.
- Working style with Habib: explain the concept in plain language before showing commands or code; keep prose conversational and free of jargon where possible; no emojis; say clearly what was verified and what was assumed.

## 2. Project context

What it is: a simulator of a cassette-to-cassette wafer film-stress metrology tool. Simulated hardware produces noisy laser readings from a hidden "true" stress; multithreaded C++17 machine software analyses them and recovers the stress with Stoney's equation; a Qt operator panel and a headless CLI drive it; an optional SECS/GEM module (HSMS, SECS-II, GEM subset) lets a scripted factory host control it.

Why it exists: a portfolio project that maps to the job posting (high-performance multithreaded machine-control software; maintain, refactor and optimize; integrate SECS/GEM; C++, OOP, STL, Qt, data structures and algorithms, networks, OS, design patterns, MATLAB plus).

Build order that must be respected: (1) fully working standalone machine, then (2) SECS/GEM integrated as a separate module, then (3) refactor and optimize with measured numbers.

Hard constraints:

- No hardware. All devices are simulated behind interfaces.
- Developed on an Apple Silicon Mac. macOS is the only supported platform (PRD D-11); nothing may claim Linux or Windows support.
- Public information only. No Frontier or FSM names, logos or proprietary material. No text copied from SEMI standards. Describe SECS/GEM as "a subset based on publicly documented behaviour", never as certified or complete.
- Honesty rule: nothing in code, README or CV material may claim something that has not been built and run. That includes WPF, WinForms, MFC, MATLAB (only if run in real MATLAB), certification, and any performance number. Numbers come from measurements only.
- Not in scope: web app, public-internet service, machine learning, real drivers.

## 3. System architecture

### 3.1 Big picture

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

### 3.2 Modules and the dependency rule

| Module | Responsibility | May depend on |
|---|---|---|
| ssim_core | Config, units, clock, logging, queues, thread pool, event bus, command queue, machine controller, state machines | STL, JSON library |
| ssim_hw | IStage, ILaserSensor, ILoadPort; simulated wafer model, simulated devices, fault injector, cassette simulator | ssim_core |
| ssim_analysis | Edge exclusion, outlier rejection, curve fit, Stoney stress, uncertainty, wafer map, CSV/JSON/PNG writers | ssim_core (types only) |
| ssim_machine | Reusable composition root (`MachineRuntime`): wires core, hardware and analysis for a machine that scans wafers back to back, on its own threads; hosts the optional SECS/GEM link | ssim_core, ssim_hw, ssim_analysis, ssim_secsgem (optional) |
| ssim_secsgem | HSMS session and timers, SECS-II codec, message catalogue, GEM state models and handlers | ssim_core, Asio |
| equipment_cli | Headless machine executable; demo and scenario modes | all of the above |
| equipment_qt | Qt Widgets operator panel | all of the above, Qt |
| host_sim | Scripted factory host with pass/fail exit codes | ssim_secsgem |
| device_sim (Tier 2) | Firmware-style device simulator: framed binary protocol with CRC-16 | ssim_core |

Rules:

- Dependencies point downward only. `ssim_core` must never include SECS/GEM, network or Qt headers.
- The machine must build, pass its tests and complete a scan with the SECS/GEM module absent (`SSIM_ENABLE_SECSGEM=OFF`) or disabled at runtime (`comm.enabled=false`).
- Nothing outside the hardware factory names a concrete simulated device. Everything else uses the interfaces.
- Qt is used only in `equipment_qt`. The demo script and CI scenarios never need Qt at runtime.

### 3.3 Threads

| Thread | Count | Owns | Talks through |
|---|---|---|---|
| Main (CLI loop or Qt GUI thread) | 1 | UI or CLI loop | Command queue (in); event bus via queued signals (out) |
| Controller | 1 | Process state machine, current run | Command queue (in); events (out) |
| Scan | 1 | IStage, ILaserSensor | Sample queue (out); stop flag (in) |
| Processing pool | N (max(1, cores minus 1)) | Fit and analysis jobs | Job queue (in); results to controller |
| HSMS I/O | 1 (Asio io_context) | Sockets, timers, session state | Command queue (to controller); event subscription (from core) |
| Logger | 1 | Log and trace files | Bounded log queue |

### 3.4 Data flow of one wafer

1. A Start command (from UI, CLI or SECS/GEM S2F41) enters the single command queue.
2. The controller checks state and control-mode rules, then tells the scan thread to run.
3. The scan thread moves the simulated stage along N diametric lines and pushes sample blocks into the bounded sample queue.
4. Processing: edge exclusion, outlier rejection, per-line least-squares fit z = a s^2 + b s + c, mean curvature, subtract k0, Stoney stress with uncertainty, quality gates, 2D map.
5. The controller emits events (ScanStarted, ScanComplete, WaferOutOfSpec) and alarms; writers produce JSON, CSV and PNG.
6. The event bus fans out to the UI, the logger and (if enabled) the SECS/GEM module, which turns events into S6F11 and alarms into S5F1.

### 3.5 State machines (summary; details in PRD 6.5)

- Process: Idle, Scanning, Processing, Alarm, Stopping. Illegal transitions are rejected with a reason code.
- Control (GEM subset): Offline, Online-Local, Online-Remote. Host commands only in Online-Remote; queries answered in every state.
- Communication: NotCommunicating or Communicating (simplified from GEM; deviation documented).
- HSMS connection: NOT_CONNECTED, NOT_SELECTED, SELECTED.

### 3.6 Design patterns in use

State (state machines), Command (control commands with source and correlation id), Observer (event bus), Strategy (outlier filter, fit, interpolation, back-pressure policy), Factory (SECS-II message factory, hardware factory), Adapter (HSMS-to-core, device-link driver), Producer-consumer (scan to processing), Pipeline (analysis stages), RAII (all resources), Facade (MachineApi), Dependency injection (clock, hardware, logger).

## 4. Folder architecture

The repository root is the `frontier` folder. Create directories only when the first file for them exists.

```
frontier/
  CLAUDE.md                 this file
  README.md                 reviewer-facing summary, quickstart, real numbers
  LICENSE                   permissive licence (MIT suggested)
  CMakeLists.txt            top-level build; options SSIM_ENABLE_SECSGEM, SSIM_BUILD_QT,
                            SSIM_BUILD_TESTS, SSIM_BUILD_BENCH, SSIM_SANITIZER
  CMakePresets.json         dev, release, tsan, asan presets
  .clang-format  .clang-tidy  .gitignore  .editorconfig
  cmake/                    helper modules (sanitizers, warnings, FetchContent pins)
  config/                   default.json and example configs
  docs/                     PRD.md, architecture.md, protocol-notes.md,
                            scenario-format.md, benchmarks.md, decisions/NNNN-title.md
  include/ssim/             public headers, one directory per module
    core/  hw/  analysis/  secsgem/
  src/
    core/  hw/  analysis/   private headers sit next to their .cpp files
    secsgem/hsms/  secsgem/secs2/  secsgem/gem/
    platform/               the only place for OS-specific code
    app_cli/  app_qt/  host_sim/  device_sim/
  tests/
    unit/  integration/  scenario/  fuzz/     mirror the src layout
  bench/                    micro-benchmarks (Release only)
  scenarios/                *.scn host scripts used by scenario tests and the demo
  scripts/                  check_stress.m, interop_secsgem.py, helper scripts
  scripts/demo.sh           one-command local demo
  .github/workflows/        ci.yml, sanitizers.yml, release.yml
  results/  build*/         generated; git-ignored
```

Placement rules:

- Public API of a module goes in `include/ssim/<module>/`; anything not meant for other modules stays private beside its source.
- Tests mirror source paths (`src/analysis/fit.cpp` has `tests/unit/analysis/fit_test.cpp`).
- Each module has its own `CMakeLists.txt` and exports one target; dependencies are declared on targets so the rule in 3.2 is enforced by the build.
- No `third_party/` copies. Dependencies come from pinned FetchContent entries.
- Generated output never goes into the source tree (use `results/` or the build directory).

## 5. Domain rules

### 5.1 Physics and units

- Units inside the code are SI: metres, pascals, 1/metre, seconds. Convert to mm, um, MPa only at interfaces (config, files, UI, SECS messages).
- Put units in names: `thickness_um`, `stress_mpa`, `curvature_per_m`, `speed_mm_per_s`.
- Use `double` for physics. `float` appears only at the SECS-II F4 boundary.
- Sign convention: film on top, z upward. Concave up (edges higher than centre) gives positive curvature and tensile (positive) stress; centre-high gives negative curvature and compressive (negative) stress. Do not change it without updating PRD 8.2, the tests and the Octave script.
- Forward model (simulator): `k = k0 + 6*sigma*t_f/(M_s*t_s^2)`, `z(s) = z0 + tilt*s + 0.5*k(theta)*s^2`. Inverse model (machine): fit `a`, curvature `2a`, mean over lines, `sigma = M_s*t_s^2*(k_mean - k0)/(6*t_f)`.
- Simulated data uses one seeded generator per wafer; the seed is written to the output. Never use `rand()`.
- Check for finite values before reporting a result. A non-finite or poor-quality result raises an alarm and reports no number.

### 5.2 SECS/GEM rules

- Layers: HSMS (TCP framing, session, timers), SECS-II (typed nested items, Stream and Function), GEM (states, events, alarms, variables, remote commands). Keep them in separate directories and classes.
- The message layouts in PRD 8.6 were written from memory of public descriptions. Before implementing any message, cross-check it against the `secsgem` library documentation and behaviour. Where unsure, write `TODO(verify): <what to check>` and do not present it as settled.
- Identifiers (CEID, RPTID, ALID, SVID, ECID) are project-defined; keep them in one catalogue file, not scattered constants.
- The machine is the passive HSMS entity (listens); the host connects. Default bind is 127.0.0.1:5000. Never expose the port beyond loopback by default.
- The decoder is strict: reject truncated data, inconsistent lengths, wrong list counts, nesting deeper than 32 and frames above the maximum length. Errors are returned as values; the peer receives the correct S9 message where the PRD says so.
- All timers use the injectable clock. Tests never sleep.

## 6. Engineering rules

### 6.1 C++ language

- C++17 only. No C++20 features (no `std::span`, `std::jthread`, concepts, ranges); use a small own `Span` or pointer and size instead.
- RAII for every resource (threads, sockets, files, locks). No naked `new` or `delete`; use `std::make_unique`. Use `shared_ptr` only for genuine shared ownership, such as immutable snapshots.
- Rule of zero where possible; otherwise rule of five with `noexcept` moves.
- `const` by default; `constexpr` where it helps; `enum class` for enumerations; `override` on every override; `[[nodiscard]]` on functions returning results or errors.
- No macros except `#pragma once`, platform detection and `SSIM_ASSERT`-style helpers. No `using namespace` in headers. Everything lives in namespace `ssim` and a sub-namespace per module.
- No global mutable state and no singletons. Pass dependencies in through constructors (clock, hardware, logger).
- Parse bytes with explicit big-endian helpers and `memcpy`; never `reinterpret_cast` a buffer into a struct. Use fixed-width integer types for anything on the wire.
- Prefer value semantics and `std::string_view`; avoid copying sample arrays (move blocks through queues).
- Virtual functions are for interfaces and strategies, not for hot inner loops.
- Warnings are errors in CI: `-Wall -Wextra -Wpedantic -Werror` (Apple clang).

### 6.2 Concurrency

- The controller is the only writer of machine state. Other threads read immutable snapshots or atomics.
- Threads exchange data only through queues. No shared mutable data outside a queue or an atomic.
- Never hold a lock while calling a callback, subscriber or any code you do not own. At most one lock at a time unless docs/architecture.md documents the order.
- Always wait on a condition variable with a predicate. Keep critical sections tiny. Use `std::scoped_lock` or `std::lock_guard`.
- Use default sequentially consistent atomics everywhere except inside the lock-free ring buffer, where each explicit memory order carries a comment explaining why and a stress test exists.
- No `volatile` for synchronization. No detached threads. Each thread is owned by a RAII wrapper that requests stop and joins on destruction. Name every thread for the logs.
- Shutdown protocol: request stop, wake all waiters, drain or discard by policy, join in reverse dependency order, flush logs. It must complete within 2 seconds.
- Every queue is bounded and has a documented back-pressure policy. Queues sit behind an interface so v1 (mutex and condition variable) can be replaced by v2 (single-producer single-consumer ring) without touching callers.
- Exceptions never cross a thread boundary.
- Document thread-safety on every public class: "Thread-safe", "Owned by thread X", or "Not thread-safe".

### 6.3 Error handling

- Expected failures (bad config, bad frame, refused command, hardware fault) are values: a small `Result<T>` with an error code and message. Exceptions are for programming errors and are caught at thread entry points, converted to an InternalError alarm and logged.
- `SSIM_ASSERT` states invariants (on in debug and sanitizer builds). It is never used to validate external input.
- Validate every external input at the boundary: config, frames, script files, CLI arguments, equipment constants. Reject with a clear message and exit code 2 for configuration errors.
- Every alarm and refusal carries a stable reason code that tests can assert on.

### 6.4 Style and structure

- `.clang-format` is authoritative; format before committing. `clang-tidy` runs in CI on a selected check set.
- Names: types `PascalCase`; functions and variables `snake_case`; members `trailing_underscore_`; constants `kPascalCase`; macros `SSIM_UPPER_CASE`; files `snake_case.hpp` and `.cpp`.
- Functions do one thing and stay short (aim for under 50 lines). Prefer early returns over deep nesting.
- Comments explain why, not what. Every public header states purpose, units and thread-safety.
- No dead code, no commented-out code, no TODO without a name and reason (`TODO(habib): ...`, `TODO(verify): ...`).

### 6.5 Testing

- Write the test first for the codec, state machines and analysis; add a regression test with every bug fix.
- Tests are deterministic: fixed seeds, fake clock, no real sleeps, no hard-coded ports (bind port 0 and read the assigned port).
- Compare floating-point values with stated tolerances, never with equality.
- Test names read as behaviour (`Fit.RecoversCurvatureUnderNoise`). One behaviour per test. Tests do not depend on each other or on run order.
- Test the unhappy paths as hard as the happy ones: truncated frames, oversized lengths, illegal transitions, link loss, stalls, alarms.
- Known-answer tests are the backbone of the analysis: hidden truth in, recovered value out, within the tolerance in the PRD.
- ThreadSanitizer and AddressSanitizer plus UBSan builds are separate build directories and must stay clean.

### 6.6 Performance

- Measure first. Do not optimize without a benchmark that shows the problem. Keep the simple version 1 until its benchmark numbers are recorded.
- Benchmarks run in Release, record the machine and compiler, and are stored in `docs/benchmarks.md`. README numbers are copied from there.
- Prefer algorithmic and structural wins (batching, avoiding copies, better queue) over micro-tuning.

### 6.7 Logging and observability

- Structured JSON-lines logs with monotonic time, wall time, level, thread name, component, event and fields, written by the logger thread through a bounded queue.
- Log every state change, command (with source and correlation id), alarm and SECS message (full trace file). No `printf` debugging left in committed code.
- Logs must never contain secrets or unbounded data dumps.

### 6.8 Security

- Bind to loopback by default. Enforce maximum frame length, nesting depth and string length. Never trust a length field.
- No `system()` or shell invocation with external input. No secrets in the repository. Pin dependency versions.

### 6.9 Portability

- OS-specific code lives only in `src/platform/` behind an interface. Use `std::filesystem` for paths, explicit endianness, no assumptions about the size of `long`, no compiler extensions.
- Keep file names case-consistent (macOS is usually case-insensitive, so a mismatch would hide until the code moves elsewhere).

### 6.10 Dependencies

- Allowed: Qt 6 (GUI only), standalone Asio, nlohmann/json, GoogleTest, stb_image_write. Pinned by FetchContent tag or hash.
- Ask before adding anything else. Check the licence (permissive or LGPL with dynamic linking) and list it in the README.

### 6.11 Documentation

- Keep docs in the same commit as the behaviour change. Architectural decisions get a short record in `docs/decisions/NNNN-title.md` (context, decision, alternatives, consequences).
- README numbers, test counts and coverage come from real runs. A bug story appears in the README only if the bug was real.

## 7. Git and CI

- Small commits, each one builds and passes tests. Conventional Commits: `feat:`, `fix:`, `refactor:`, `perf:`, `test:`, `docs:`, `build:`, `ci:`, `chore:`.
- Do not force-push `main`. Do not skip hooks or CI. Do not commit `build*/`, `results/`, binaries, IDE files or anything secret.
- Tags mark the story: `v0.1` standalone machine, `v0.2` SECS/GEM integrated, `v0.3` refactored and optimized with benchmarks.
- Make "maintain, fix, refactor, optimize" visible in history: simple v1, then the SECS/GEM integration commit series, then a refactor commit, real fix commits (only for real bugs), and a perf commit with before and after numbers.
- CI runs on macOS (Apple clang): build with warnings as errors, unit, integration and scenario tests, ThreadSanitizer job, ASan and UBSan job, clang-format check, clang-tidy, coverage. CI is created on day one, so portability problems show up early. A red CI is fixed before new features.

## 8. Commands (macOS; names are placeholders until the targets exist)

```
xcode-select --install
brew install cmake qt

cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=/opt/homebrew/opt/qtbase
cmake --build build -j
ctest --test-dir build --output-on-failure

# headless machine, then a scripted host in a second terminal
./build/equipment_cli --config config/default.json --port 5000
./build/host_sim --connect 127.0.0.1:5000 --script scenarios/normal_run.scn

# sanitizers (separate build directories; thread and address cannot be combined)
cmake -S . -B build-tsan -DSSIM_SANITIZER=thread -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build-tsan -j && ctest --test-dir build-tsan --output-on-failure
cmake -S . -B build-asan -DSSIM_SANITIZER=address -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build-asan -j && ctest --test-dir build-asan --output-on-failure

# benchmarks, format, tidy, interop
./build/bench_queue
clang-format -i $(git ls-files '*.cpp' '*.hpp')
python3 scripts/interop_secsgem.py --port 5000
octave scripts/check_stress.m results/<run_id>/<wafer_id>/samples.csv
scripts/demo.sh
```

Mac limits to remember: no Valgrind or GDB on Apple Silicon (use ASan, Instruments, LLDB); MFC, WPF and WinForms do not build here; Windows is not supported.

## 9. How Claude should work in this repository

1. Session start: read this file (CLAUDE.md) in full, then `docs/specs/day-<Current day>-spec.md`, then `docs/plans/day-<Current day>-plan.md` for whatever `Current day` (section 1) is set to, before writing any code. Those files filter `docs/PRD.md` down to the day's scope; consult the PRD section they cite for full detail, and state which requirement IDs the change serves.
2. Follow the tier order: Tier 1 first, then Tier 2, never Tier 3 (it needs Windows, which is out of scope). Do not start extras while a Tier 1 item is red.
3. Plan small steps. Explain the concept first when it is new to Habib (for example the HSMS timers or the lock-free ring), then implement.
4. Test first for codec, state machines and analysis. Run the tests after every change and report the real result. Never say something passes unless it was run.
5. Keep diffs focused. Do not reformat or restructure unrelated code. Do not add dependencies without asking.
6. When something is uncertain (a SECS/GEM layout, a physics constant, a sign convention), mark it `TODO(verify)`, say so in the reply, and check it against a reference or the interop test.
7. Never invent numbers, benchmark results, coverage figures, bug stories or interview claims. Leave blanks or placeholders until measured.
8. If a requirement is wrong, ambiguous or too big for the time left, say so and propose a cut following the order in PRD section 5, instead of quietly changing scope.
9. At the end of each task, report: what changed, which tests ran and their results, what is still unverified, and what comes next.

## 10. Definition of done for any change

- Builds warning-free on the local machine; CI expected green on macOS.
- New or changed behaviour has tests (including unhappy paths); bug fixes have a regression test.
- Sanitizer builds still clean for anything touching threads, memory or parsing.
- Public headers document units and thread-safety; docs updated in the same commit.
- Module dependency rule still holds (`ssim_core` free of SECS/GEM, network and Qt).
- No unrelated changes, no dead code, no secrets, no generated files committed.
- Commit message follows the convention and states the requirement ID where relevant.

## 11. Never do

- Never claim certification, completeness or affiliation with Frontier or SEMI; never use their names, logos or standard text.
- Never put WPF, WinForms, MFC or MATLAB claims anywhere unless it was built and run.
- Never expose the machine port publicly or bind to all interfaces by default.
- Never sleep in tests, use unseeded randomness, or depend on wall-clock timing.
- Never hold a lock across a callback, detach a thread, or let an exception cross threads.
- Never merge with a red CI or a failing sanitizer job.
- Never optimize before a benchmark exists, and never publish a benchmark number that was not measured.

## 12. Milestone snapshot (update as work lands)

| Day | Date | Target | Status |
|---|---|---|---|
| D1 | Mon 21 Sep | Skeleton, CI on macOS, core basics, wafer model and simulated hardware, scope frozen | done; CI was red from the first commit because of a real bug (zero-noise `normal_distribution`), found and fixed on 24 Sep; green since `3ea3fa3` |
| D2 | Tue 22 Sep | Scan, processing, controller, alarms, exports, CLI; tag v0.1 | done (Day 2 exit criteria met in `1259b8c`); **tag v0.1 not placed**: no standalone-only commit had a green CI, so it waits on a decision |
| D3 | Wed 23 Sep | Qt panel, wafer map, control modes; README draft | done Thu 24 Sep; demo GIF (`docs/media/panel_demo.gif`) is rendered from the real widgets offscreen, not a screen recording; a true screen recording is still worth making for the video; cassette loop deferred |
| D4 | Thu 24 Sep | SECS-II codec, fuzz tests, HSMS framing and timers | done Thu 24 Sep; UT-CODEC and UT-HSMS pass; sanitizers clean on the GitHub macOS runner; some protocol values still `TODO(verify)` |
| D5 | Fri 25 Sep | GEM module, integration into the app as a separate step, host_sim and scenarios, interop test | done Thu 24 Sep, **tag v0.2 on `3ea3fa3`**: GEM layer, `serve`, host_sim, 7 scenarios, XT-SECSGEM-1 15/15; CI, ASan+UBSan and TSan all green on macOS; ST-cassette_run deferred (no cassette loop) |
| D6 | Sat 26 Sep | Refactor, sanitizer fixes, ring buffer and benchmarks, Octave check, local demo script; tag v0.3 | **not started; carried over.** Sanitizers are already green in CI. The refactor commit, the measured optimization (ring buffer, before and after numbers) and `scripts/demo.sh` are needed for the Definition of Done and for real numbers on Day 7 |
| D7 | Sun 27 Sep | README and video final, CV final, submit | planned, see `docs/plans/day-7-plan.md`; work that does not depend on Day 6 can start now (README, badges, licences, NFR-IP-1 clean-up, DoD walk); the video, the CV and the submission are Habib's |

Open questions are tracked in PRD section 16.3 (message-layout verification, Asio versus Qt Network, CV project selection, internship duties, licence, competitive-programming and travel lines).
