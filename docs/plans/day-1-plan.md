# Day 1 plan — repo skeleton, CI, core basics, wafer model + simulated hardware

Read `docs/specs/day-1-spec.md` before this file. This is the ordered, file-level
checklist to satisfy that spec. Build order respects the dependency rule in
CLAUDE.md §2 (`ssim_core` has no SECS/GEM, network or Qt headers) and the module
table in CLAUDE.md §4.

## 1. Repository skeleton and build system

1. `CMakeLists.txt` (root) — project, C++17, options `SSIM_ENABLE_SECSGEM`,
   `SSIM_BUILD_QT`, `SSIM_BUILD_TESTS`, `SSIM_BUILD_BENCH`, `SSIM_SANITIZER`, all
   default to a Tier-1-only build (Qt/SECSGEM off is fine today since neither
   exists yet).
2. `CMakePresets.json` — `dev`, `release`, `tsan`, `asan` presets (tsan/asan
   presets can be stubs today; they matter from Day 6 but should exist now so CI
   doesn't need reshaping later).
3. `.clang-format`, `.clang-tidy`, `.editorconfig`, `.gitignore` (git-ignore
   `build*/`, `results/`).
4. `cmake/` — helper modules: `Warnings.cmake` (`-Wall -Wextra -Wpedantic -Werror`
   / `/W4 /WX /permissive-`), `Sanitizers.cmake`, `FetchContentPins.cmake` (pin
   GoogleTest, nlohmann/json now; Asio pin goes here too once Q3 is answered).
5. `LICENSE` — resolves open question Q6.
6. `include/ssim/core/` and `src/core/` — created now since Day 1 populates them;
   other `include/ssim/<module>/` and `src/<module>/` directories stay uncreated
   until their first file lands (repo-layout rule in PRD §6.7).

## 2. Core library (`ssim_core` target)

Each item: file(s), then the test that proves it.

1. **Config** — `include/ssim/core/config.hpp`, `src/core/config.cpp`.
   Loads/validates the JSON sections from PRD §8.1 (machine, wafer, scan, noise,
   faults, analysis, cassette, comm, output). Range checks per PRD §7.1 (e.g. scan
   lines 1–32, points/mm 10–80, edge exclusion 0–20 mm). Exit code 2 on invalid
   value; warn (not abort) on unknown key.
   → `tests/unit/core/config_test.cpp` (valid file; missing-key defaults;
   out-of-range → exit 2; unknown key → warning only).
2. **Clock** — `include/ssim/core/clock.hpp` (+ `system_clock` impl, + a
   `FakeClock` test double). Injectable everywhere per D-08; no direct
   `std::chrono::system_clock` calls elsewhere in the codebase from today onward.
   → `tests/unit/core/clock_test.cpp` (fake clock advances deterministically).
3. **Bounded queue (v1)** — `include/ssim/core/queue.hpp` behind an interface
   (`IQueue<T>`), `src/core/mutex_queue.cpp` as the v1 mutex/condvar
   implementation, per D-09 (so a v2 lock-free ring can replace it later without
   touching callers). Configurable back-pressure policy (block / drop-oldest /
   raise alarm) per C5.
   → `tests/unit/core/queue_test.cpp` (bounded capacity enforced; each
   back-pressure policy behaves as documented; predicate-based wait, no busy loop).
4. **Event bus** — `include/ssim/core/event_bus.hpp`, `src/core/event_bus.cpp`.
   Observer pattern; publish/subscribe; no lock held while invoking a subscriber
   (C3).
   → `tests/unit/core/event_bus_test.cpp` (subscriber receives published event;
   unsubscribe stops delivery).
5. **Logger** — `include/ssim/core/logger.hpp`, `src/core/logger.cpp`. Owns a
   dedicated thread reading a bounded log queue, writes JSON-lines per the format
   in PRD §8.5 (t_mono, t_wall, level, thread, component, event, fields). Rotation
   is size-capped (FR-LOG-4).
   → `tests/unit/core/logger_test.cpp` (log line shape/fields correct; queue
   overflow policy; thread joins cleanly on shutdown).
6. Document thread-safety on every public class per PRD §6.4 ("Thread-safe",
   "Owned by thread X", or "Not thread-safe") in the header comment.

## 3. Simulated hardware (`ssim_hw` target, depends only on `ssim_core`)

1. **Interfaces** — `include/ssim/hw/istage.hpp`, `include/ssim/hw/ilaser_sensor.hpp`,
   `include/ssim/hw/iload_port.hpp`. No concrete device type named outside the
   hardware factory (FR-HW-1).
2. **Wafer model** — `include/ssim/hw/wafer_model.hpp`, `src/hw/wafer_model.cpp`.
   Implements the forward model from PRD §8.2: builds hidden truth (stress,
   initial curvature k0, tilt, anisotropy) from config + seed; one seeded RNG per
   wafer, never `rand()` (PRD §8.3). Same seed → identical output (FR-HW-2).
   → `tests/unit/hw/wafer_model_test.cpp`: reproduce the §8.2 worked example
   numbers (this *is* the forward-model half of UT-STONEY-1 named in the spec);
   same-seed determinism test.
3. **Simulated stage + laser sensor** — `src/hw/stage_sim.cpp`,
   `src/hw/laser_sensor_sim.cpp`. Stage moves along a programmed line at
   `speed_mm_per_s`; sensor returns `z(s)` from the wafer model plus noise (PRD
   §8.3 Gaussian, sigma default 0.5 µm). Real-time factor 0 vs 1.0 (FR-HW-5).
   → `tests/unit/hw/stage_sim_test.cpp`, `tests/unit/hw/laser_sensor_sim_test.cpp`
   (line traversal produces expected sample count at a given points/mm; noise
   statistics sane at rtf=0).
4. **Fault injector** — `include/ssim/hw/fault_injector.hpp`,
   `src/hw/fault_injector.cpp`. Implements spike, burst, dropout, stuck, drift,
   saturation, stage-stall per PRD §8.3, selectable per wafer from config/scenario
   (FR-HW-4). Not wired into any alarm yet — that happens when the processing
   pipeline exists (Day 2).
   → `tests/unit/hw/fault_injector_test.cpp` (each fault type produces the
   expected sample pattern given a fixed seed).
5. **Hardware factory** — `src/hw/hardware_factory.cpp`: builds `IStage`/
   `ILaserSensor` instances from config. This is the *only* place that names the
   concrete simulated types (enforces FR-HW-1).

## 4. CI

1. `.github/workflows/ci.yml` — matrix: macOS (clang), Ubuntu (gcc), Windows
   (MSVC). Steps: configure with `dev` preset, build with warnings-as-errors,
   `ctest --output-on-failure`. clang-format check job. (clang-tidy and coverage
   can be added once there's enough source to make them meaningful — note as
   `TODO(habib): add clang-tidy/coverage jobs once src/ has more than the Day 1
   surface` rather than skipping silently.)
2. Confirm the workflow is red until code exists, then green once steps 2–3 land
   — this is what "CI created on day one" (PRD §6.7 principle / §12.4) buys: any
   Windows-only build break shows up today, not on Day 4+.

## 5. Scope freeze + open questions

1. Resolve **Q3** (Asio vs Qt Network) — record the decision as a short entry in
   `docs/decisions/0001-networking-library.md` (create `docs/decisions/` now since
   this is its first file). Given `ssim_core` must stay Qt-free (dependency rule)
   and the machine must run headless without Qt at all (FR-MC-3), Asio is the
   PRD's own stated default (D-03) — confirm it's actually FetchContent-able
   cleanly from CI on all three OSes before locking it in.
2. Resolve **Q6** — pick repository name + MIT licence, add `LICENSE` (already in
   step 1.5).
3. Re-read PRD section 5 (tiers/cut order) and section 3 (goals/non-goals) once
   more before ending the day; this is the "scope frozen" checkpoint from PRD §13
   — no requirement text changes after today without updating the PRD in the same
   commit (CLAUDE.md §1).

## Day 1 done-checklist (maps back to day-1-spec.md)

- [ ] Repo skeleton matches PRD §6.7 / CLAUDE.md §4 (only dirs with a first file
      exist).
- [ ] `ssim_core`: config, clock, queue (v1), event bus, logger — each with a
      passing unit test.
- [ ] `ssim_hw`: interfaces, wafer model, stage/sensor sim, fault injector,
      hardware factory — each with a passing unit test.
- [ ] Forward-model half of UT-STONEY-1 passes (matches PRD §8.2 worked example).
- [ ] CI green on macOS, Linux, Windows (SM9); clang-format check passes.
- [ ] Q3 and Q6 answered and recorded.
- [ ] Nothing outside `hardware_factory.cpp` names a concrete `WaferModel`/
      `StageSim`/`LaserSensorSim` type (spot-check with grep before calling the
      day done).
