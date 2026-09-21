# Day 2 plan — scan thread, processing pipeline, controller, alarms, exports, CLI

Read `docs/specs/day-2-spec.md` first. Builds on Day 1's `ssim_core` (queues,
clock, event bus, logger) and `ssim_hw` (interfaces, wafer model, stage/sensor
sim, fault injector) — nothing here touches Qt or SECS/GEM headers.

## 1. Analysis library (`ssim_analysis` target, depends on `ssim_core` types only)

1. `include/ssim/analysis/{edge_exclusion,outlier_rejection,line_fit,stoney,
   uncertainty,wafer_map}.hpp` + matching `src/analysis/*.cpp`.
   - Edge exclusion: drop samples within `edge_exclusion_mm` of each line end
     (FR-PRC-1). → `tests/unit/analysis/edge_exclusion_test.cpp`.
   - Outlier rejection: median + MAD, `outlier_mad_k` threshold, report
     count/fraction removed (FR-PRC-2). → `tests/unit/analysis/outlier_test.cpp`.
   - Line fit: least-squares `z=as²+bs+c`, curvature `2a`, residual RMS, SE of
     `a` (FR-PRC-3). → `tests/unit/analysis/line_fit_test.cpp` covers UT-FIT-1/2.
   - Combine: mean curvature, per-angle curvature, anisotropy
     `(max−min)/mean` (FR-PRC-4). → `tests/unit/analysis/combine_test.cpp`
     covers UT-ANISO-1.
   - Stoney: subtract k0, `sigma = M_s*t_s²*(k_mean−k0)/(6*t_f)`, sign convention
     from PRD §8.2 (FR-PRC-5). → `tests/unit/analysis/stoney_test.cpp` covers the
     full UT-STONEY-1 and UT-STONEY-2 (now wired end-to-end from the Day 1 wafer
     model through this fit/stoney chain).
   - Uncertainty: propagate fit SE to 1-sigma stress uncertainty (FR-PRC-6).
   - Quality gates: RMS-above-limit / non-finite / implausible / out-of-spec, each
     raising the right alarm/flag and reporting no number when required
     (FR-PRC-7). → `tests/unit/analysis/quality_gates_test.cpp`.
   - Wafer map: polar linear interpolation onto a configurable grid (default
     1 mm) (FR-PRC-8). → `tests/unit/analysis/wafer_map_test.cpp`.
2. Thread-pool fan-out for per-line fits (FR-PRC-9): `src/analysis/fit_pool.cpp`,
   using the Day 1 `IQueue` job queue; assert bit-identical output vs a serial run
   in the same test.
3. Writers: `src/analysis/writers/{json_writer,csv_writer,png_writer}.cpp` (PNG via
   stb_image_write, no Qt dependency) — per-wafer JSON (PRD §8.5 shape), per-line
   CSV, sample CSV, PNG map with colour bar/legend/edge-exclusion ring (FR-OUT-1..3).
   → `tests/unit/analysis/writers_test.cpp`.

## 2. Core controller additions (`ssim_core`, still Qt/network-free)

1. `include/ssim/core/process_state_machine.hpp` + `src/core/process_state_machine.cpp`
   — Idle/Scanning/Processing/Alarm/Stopping per PRD §6.5 table; illegal
   transitions return a reason code. → `tests/unit/core/state_machine_test.cpp`
   covers UT-SM-1.
2. `include/ssim/core/command.hpp` — Command pattern objects (Start, Stop, Abort,
   ClearAlarm, SetControlMode) carrying source + correlation id (FR-MC-2).
3. `src/core/controller.cpp` — the single writer of machine state (D-07); reads
   the command queue, drives the process state machine, owns run context, emits
   events (ScanStarted/ScanComplete/WaferOutOfSpec/etc.) via the event bus.
   → `tests/unit/core/controller_test.cpp` covers UT-CTRL-1 (UI/CLI sources) and
   FR-MC-4.
4. `src/core/alarms.cpp` — alarm catalogue (ALIDs 1001–1007 today; 1008
   InternalError is inherent, not scheduled work), set/clear-once semantics
   (FR-ALM-2), watchdogs for scan-stall / processing-timeout / queue-overflow
   (FR-ALM-3). → `tests/unit/core/alarms_test.cpp` covers UT-FAULT-1.
5. Shutdown protocol per PRD §6.4 C6 / FR-MC-6: request stop → wake waiters →
   drain/discard by policy → join in reverse dependency order → flush logs, all
   within 2 s. → `tests/unit/core/shutdown_test.cpp` covers FT-SHUTDOWN-1 (abort
   mid-scan, mid-processing, mid-alarm).

## 3. Scan thread

1. `src/core/scan_thread.cpp` (or `src/hw/` if it's cleaner to co-locate with the
   `IStage`/`ILaserSensor` drivers it calls — either is fine as long as it only
   depends on `ssim_hw` interfaces, never concrete sim types): drives the stage
   across N lines per FR-SCN-1, pushes sample blocks into the bounded sample
   queue (FR-SCN-2), honours abort within 200 ms (FR-SCN-3), publishes progress
   (FR-SCN-4).
   → `tests/unit/core/scan_thread_test.cpp` (line count/angles match config;
   abort latency at rtf=1.0; queue back-pressure policy honoured).

## 4. CLI (`equipment_cli` target, depends on all of the above)

1. `src/app_cli/main.cpp` — options: `--config`, `--port`, `--seed`, `--rtf`,
   `--out`, `--scenario`; exit codes 0/2/3/4 (FR-CLI-2).
2. `demo` subcommand/flag that runs a built-in scenario end-to-end and exits
   (FR-CLI-3) — today this can run standalone (no host) since SECS/GEM doesn't
   exist yet; the Docker demo wiring comes Day 6.
   → `tests/integration/cli_demo_test.cpp` or a scenario-style test invoking the
   built binary, asserting JSON/CSV/PNG appear under `results/<run_id>/<wafer_id>/`.

## 5. Wire it together and verify

1. Run a standalone scan end-to-end with the nominal wafer config from PRD §8.1;
   confirm output files match PRD §8.5 shapes and SM1 (within 2%).
2. Run with injected spike/dropout faults; confirm SM2 (within 5% or correct
   alarm).
3. Tag `v0.1` once CI is green with the new tests included.

## Day 2 done-checklist (maps back to day-2-spec.md)

- [ ] `ssim_analysis` built and unit-tested (edge exclusion → wafer map).
- [ ] Controller + process state machine + alarms + scan thread wired through
      Day 1 queues/event bus; commands carry source + correlation id.
- [ ] `equipment_cli` produces JSON/CSV/PNG under `results/<run_id>/<wafer_id>/`.
- [ ] SM1 and SM2 measured and met (record actual numbers, don't just assert
      pass/fail — README will need them later).
- [ ] FT-SHUTDOWN-1 passes (no thread leaks, ≤2 s).
- [ ] Tag `v0.1` pushed.
