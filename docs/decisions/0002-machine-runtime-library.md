# 0002. Machine runtime library: ssim_machine

Date: 2026-09-24 (Day 3)
Status: accepted

## Context

Day 2 left the whole composition of a run inside `equipment_cli`'s `main()`:
build the hardware, wire the scan thread and controller, wait, drain the
sample queue, run the analysis pipeline, raise quality alarms, report to the
controller, write files. It is single-shot and blocks the calling thread.

The Qt panel needs the same steps, repeatable (wafer after wafer) and never
on the GUI thread. Copying `main()` into `app_qt` would duplicate the most
delicate code in the project, and `ssim_core` cannot host it because it may
not depend on `ssim_hw` or `ssim_analysis` (PRD 6.2). The Day 5 GEM wiring
will need the same thing again.

## Decision

Add a small Qt-free static library `ssim_machine` (`src/machine/`,
`include/ssim/machine/`) that depends on `ssim_core`, `ssim_hw` and
`ssim_analysis`. Its `MachineRuntime` owns the logger, simulated hardware,
event bus, alarm manager, sample queue, scan thread, controller, fit pool and
a named `processing` thread, and exposes `ssim::core::MachineApi`.

Behaviour worth knowing:

- One run directory per runtime session (`results/<run_id>/`), wafer ids
  W001, W002, ... inside it, so FR-OUT-5 never-overwrite holds by construction.
- Sensor-health alarms (spike rate, dropout) are raised through the
  controller, so the machine enters Alarm and blocks scans until cleared
  (FR-ALM-1, UC5). `equipment_cli` still only sets them in `AlarmManager`;
  that difference is fixed when the CLI is moved onto this library.
- Configuration is immutable per runtime. Changing a setting builds a new
  runtime while Idle. This is not the S2F15 runtime-constants path, so FR-CFG-3
  is not claimed.

## Consequences

- PRD 6.2 and CLAUDE.md 3.2 gain a row for `ssim_machine`.
- Being Qt-free, the runtime is tested on all CI systems
  (`tests/integration/machine_runtime_test.cpp`).
- `equipment_cli` keeps its own copy of the flow for now. Moving it onto
  `MachineRuntime` is the planned Day 6 refactor commit.

## Alternatives considered

- **Put the flow in `app_qt`**: rejected, it would not be testable in CI
  without Qt and would be copied again for GEM.
- **Put it in `ssim_core`**: rejected, it breaks the dependency rule.
- **Refactor `equipment_cli` first**: deferred; it widens the Day 3 diff and
  the CLI already has passing tests.

## Update, 2026-09-24 (Day 5): the optional SECS/GEM link

`MachineRuntime` now also hosts the SECS/GEM link (HSMS server plus GEM
service), which is how the module is "integrated as a separate step" (PRD 13):

- `ssim_machine` links `ssim_secsgem` only when `SSIM_ENABLE_SECSGEM=ON` and
  defines `SSIM_HAS_SECSGEM`; the header never includes secsgem or Asio.
- The link is started by `RuntimeOptions::start_comm` (off by default, so tests
  and the panel do not open a port unasked) and only if `config.comm.enabled`.
  `start_comm()` returns an error value if the port cannot be bound.
- `equipment_cli serve` runs it until SIGINT/SIGTERM and prints the port.
- Shutdown order: the server stops first, then the worker and controller, and
  the GEM service is destroyed last, so no bus event reaches a dead service.
- FR-MC-3 holds both at build time (the machine builds and passes its tests
  with the option OFF) and at run time (`comm.enabled=false`).
