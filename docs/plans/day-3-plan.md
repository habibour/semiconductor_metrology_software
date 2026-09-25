# Day 3 plan — Qt panel, wafer map, control modes, progress; README draft; cassette loop if time

Read `docs/specs/day-3-spec.md` first. Builds on Day 2's controller/analysis/CLI;
`equipment_qt` is the *only* place Qt headers may appear (dependency rule).

## 1. Facade for the UI (and future CLI/SECS-GEM callers)

1. `include/ssim/core/machine_api.hpp` + `src/core/machine_api.cpp` — small,
   stable surface: `start()`, `stop()`, `abort()`, `clearAlarm()`,
   `setControlMode()`, plus read-only accessors for current state/progress/last
   result, all just forwarding to the Day 2 command queue / event bus. This is
   what the Qt panel (and later `host_sim`/GEM handlers) calls — nothing reaches
   into the controller directly.
   → `tests/unit/core/machine_api_test.cpp`.

## 2. Qt operator panel (`equipment_qt` target)

1. `src/app_qt/main.cpp`, `src/app_qt/main_window.{hpp,cpp}`.
2. Status bar widget: comm/control/process state labels; Start/Stop/Abort/Clear
   buttons wired to `MachineApi`, enabled/disabled per current state (FR-UI-1);
   control-mode selector (Offline/Online-Local/Online-Remote).
3. Live views: progress bar, wafer map widget (render the same PNG/height-grid
   data the analysis writers produce — reuse `ssim_analysis` map data, don't
   reimplement interpolation in Qt), last-result panel with uncertainty, alarm
   banner, scrolling log/message-trace view (FR-UI-2).
4. Event delivery: subscribe to the Day 2 event bus, marshal into Qt via queued
   signals only, capped to 30 Hz (FR-UI-3) — the GUI thread must never call into
   `ssim_core`/`ssim_analysis` synchronously for anything that can block.
5. Settings dialog for the permitted-at-runtime config subset (FR-UI-4, ties to
   FR-CFG-3 — full runtime-change plumbing can stay minimal today if FR-CFG-3
   itself isn't built yet; wire what exists).
6. Export / open-results-folder actions (FR-UI-5).
7. If time: a demo-mode toggle that runs a canned scenario for clean recording
   (FR-UI-6).

## 3. README draft

1. `README.md` — plain-English summary, architecture diagram (ASCII from PRD
   §6.1 is fine as a placeholder), quickstart steps as they exist today (build +
   run CLI; Qt panel launch). Leave results-table/limits/interview-note sections
   as placeholders — they get filled with *measured* numbers, never invented
   ones, as later days produce them (NFR-DOC-1, PRD §12.1).

## 4. Stretch: cassette loop (only start this if Days 1–2 exit criteria are fully met)

1. `include/ssim/hw/cassette_sim.hpp` + `src/hw/cassette_sim.cpp` — 1–25 slots,
   empty-slot support, per-wafer truth values (FR-HW-6).
2. `src/core/controller.cpp` extension: cassette run mode — slot order, skip
   empty, continue after out-of-spec, halt on alarm, cassette summary event
   (FR-MC-5).
3. `src/analysis/writers/cassette_csv_writer.cpp` (FR-OUT-4).
4. → `tests/integration/cassette_test.cpp`.
5. If time runs out partway through this section, stop and leave it for Day 6's
   stretch list rather than eating into Day 4's SECS-II start — cassette loop is
   explicitly the *safest* Tier-2 item to defer, not the one to force through.

## Day 3 done-checklist (maps back to day-3-spec.md)

- [x] `MachineApi` facade exists and the Qt panel only talks through it.
- [x] Panel demonstrates: load config, switch control mode, Start/Stop/Abort,
      live progress + wafer map, alarm banner + clear — recorded as the "first
      GIF".
- [x] GUI thread confirmed non-blocking (no long call on the Qt thread) and
      update rate ≤30 Hz.
- [x] README draft committed with real quickstart steps (no invented numbers).
- [x] Cassette loop done *or* explicitly deferred to Day 6 (deferred) — either is a valid
      outcome, silent scope creep into Day 4 is not.
