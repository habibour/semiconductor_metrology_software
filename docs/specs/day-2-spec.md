# Day 2 spec — scan thread, processing pipeline, controller, alarms, exports, CLI

Source of truth: `docs/PRD.md`. This file is a filtered index into it.

PRD §13, row D2: "Scan thread, processing pipeline, controller and alarms,
exports, CLI." Exit: "Standalone run produces JSON, CSV, PNG; SM1 and SM2 met;
tag v0.1."

## In scope today

| ID / section | What it requires | PRD ref |
|---|---|---|
| Scan thread | Own thread, drives `IStage`/`ILaserSensor` from Day 1; pushes sample blocks into the bounded sample queue | PRD §6.3 |
| FR-SCN-1 | Multi-line diametric scan: N lines (default 6, max 32) evenly spaced over 180°; 40 pts/mm default; diameters 100/150/200/300 mm (default 300) | PRD §7.3 |
| FR-SCN-2 | Sample blocks (line index, angle, positions, heights, timestamps) into bounded queue; configurable back-pressure (default block; alt drop + QueueOverflow) | PRD §7.3 |
| FR-SCN-3 | Abort takes effect within 200 ms at rtf 1.0, consistent state after | PRD §7.3 |
| FR-SCN-4 | Scan progress 0–100% published for UI/status variable | PRD §7.3 |
| Processing pool | N threads (default max(1, cores−1)) running fit/analysis jobs from a job queue | PRD §6.3 |
| FR-PRC-1..8 | Edge exclusion, outlier rejection (median/MAD), per-line least-squares fit `z=as²+bs+c`, combine to mean curvature + anisotropy, subtract k0 + Stoney stress + uncertainty, quality gates (poor fit / non-finite / implausible / out-of-spec), 2D polar-interpolated height map | PRD §7.4, §8.2 (inverse model), §8.4 (pipeline steps 1–10) |
| FR-PRC-9 | Per-line fits run in the Day-1 thread pool; bit-identical to single-threaded | PRD §7.4 |
| Controller + process state machine | Owns Idle/Scanning/Processing/Alarm/Stopping; illegal transitions rejected with reason code | PRD §6.5, FR-MC-1 |
| FR-MC-2 | All commands (Start/Stop/Abort/ClearAlarm/SetControlMode — SetControlMode UI-only today, SECS source comes Day 5) enter through one thread-safe command queue; source recorded | PRD §7.5 |
| FR-MC-4 | Control-state rules enforced for every command source (only UI/CLI sources exist today) | PRD §7.5, §6.5 |
| FR-MC-6 | Graceful shutdown: drain queues, join threads, flush logs/files within 2 s, no thread leaks | PRD §7.5 |
| FR-ALM-1..3 | Alarm catalogue (PRD §8.6.5 ALIDs 1001–1008 as applicable today: SensorSpikeRateHigh, SensorDropout, FitQualityPoor, ScanStall, ProcessingTimeout, QueueOverflow, StressImplausible); set/clear announced once; watchdogs (scan stall, processing timeout, queue overflow) | PRD §7.6 |
| FR-OUT-1..3, 5 | Per-wafer JSON summary, per-line + sample CSV, PNG wafer map (headless, no Qt), `results/<run_id>/<wafer_id>/` layout, never overwrite | PRD §7.7, §8.5 |
| FR-CLI-1..3 | `equipment_cli`: config file, port, seed, rtf, output dir, scenario options; exit codes 0/2/3/4; `demo` mode | PRD §7.9 |
| Patterns added today | State (process state machine), Command (Start/Stop/Abort/ClearAlarm/SetControlMode objects with source + correlation id), Producer-consumer (scan → processing pool), Pipeline (fixed-order analysis stages), Strategy (outlier filter, fit method) | PRD §6.6 |

Not in scope today: Qt panel, cassette loop (Day 3), SECS/GEM, benchmarking/v2 ring
buffer (Day 6).

## Tests that must exist and pass today

- UT-FIT-1 (noise-free parabola, exact curvature to numeric precision)
- UT-FIT-2 (noisy parabola, curvature within statistical bound; reported SE matches
  repeated trials)
- UT-STONEY-1, full version (hidden truth −180 MPa recovered within 2% on the
  nominal wafer — this is where the Day 1 forward-model-only test becomes the real
  known-answer test)
- UT-STONEY-2 (sign convention: convex/concave ⇒ negative/positive stress)
- UT-ANISO-1 (N≥2 lines: anisotropy doesn't bias mean curvature; N=1: documented
  bias appears)
- UT-FAULT-1 (spikes/bursts/dropouts/stuck/drift ⇒ correct result within 5% or
  correct alarm)
- UT-SM-1 (every illegal state transition rejected with correct reason)
- UT-CTRL-1 (commands from UI and CLI sources obey control-state rules — SECS
  source added Day 5)
- FT-SHUTDOWN-1 (stop during scan/processing/alarm; all threads join within 2 s)

## Exit criteria (verbatim from PRD §13, row D2)

> Standalone run produces JSON, CSV, PNG; SM1 and SM2 met; tag v0.1.

SM1 = stress recovery within 2% on the nominal wafer. SM2 = within 5% (or correct
alarm) under injected spikes/dropouts.
