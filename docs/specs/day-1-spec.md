# Day 1 spec — repo skeleton, CI, core basics, wafer model + simulated hardware

Source of truth: `docs/PRD.md`. This file is a filtered index into it — it restates
IDs and one-line summaries so a session can scope itself quickly; it does not
replace reading the referenced PRD section when implementing.

PRD section 13, row D1: "Repository skeleton, CMake, CI on three systems with a
passing test; config, clock, queues (v1), event bus, logger; wafer model and
simulated hardware; scope frozen."

## In scope today

| ID / section | What it requires | PRD ref |
|---|---|---|
| Repo layout | Create the fixed directory skeleton (only dirs whose first file exists) | PRD 6.7, CLAUDE.md §4 |
| Tech stack | CMake + presets, GoogleTest via FetchContent, nlohmann/json, clang-format/clang-tidy wired into CI | PRD §11 |
| FR-CFG-1 | Load JSON config (machine/wafer/scan/analysis/alarms/comm/output sections); documented defaults; unknown keys warn; invalid values abort with exit code 2 | PRD §7.1, §8.1 |
| FR-CFG-2 | Validate every config value against a stated range | PRD §7.1 |
| C1–C9 | Concurrency rules: single-writer controller (not yet exercised today), queues-only handoff, no lock across callbacks, documented lock hierarchy, bounded queues with back-pressure policy, clean shutdown, exceptions never cross threads, injectable Clock, queues behind an interface (v1 = mutex/condvar) | PRD §6.4 |
| D-07, D-08, D-09 | Controller as single writer (design only today), injectable clock everywhere, queues behind an interface for later v1→v2 swap | PRD §11.1 |
| Logger thread | Structured JSON-lines log, monotonic + wall time, level, thread, component, event, fields; dedicated logger thread; bounded log queue | PRD §6.3, FR-LOG-1, FR-LOG-4 |
| Event bus | Observer/pub-sub pattern; core stays unaware of subscribers | PRD §6.6 |
| FR-HW-1 | Define `IStage`, `ILaserSensor`, `ILoadPort`; no code outside the hardware factory names a concrete device | PRD §7.2 |
| FR-HW-2 | Simulated wafer generator builds hidden truth (stress, geometry, initial bow, tilt, anisotropy) from config + seed; same seed → identical data | PRD §7.2, §8.2 |
| FR-HW-3 | Simulated stage moves along a scan line; simulated laser returns height + tilt + noise | PRD §7.2 |
| FR-HW-5 | Real-time factor: 1.0 realistic timing, 0 as-fast-as-possible | PRD §7.2 |
| FR-HW-4 | Fault injection primitives (spike, burst, dropout, stuck, drift, saturation, stall) — build the mechanism now; wiring it into alarms is Day 2 | PRD §7.2, §8.3 |
| 8.2 forward model | `k = k0 + 6*sigma*t_f/(M_s*t_s^2)`, `z(s) = z0 + tilt*s + 0.5*k(theta)*s^2`, sign convention (film on top, z upward, concave-up = positive/tensile stress) | PRD §8.2 |
| Patterns in play today | RAII (threads/files/sockets), Dependency injection (clock, hardware, logger passed via constructors), Observer (event bus) | PRD §6.6 |
| Scope freeze | PRD status becomes frozen at end of Day 1 — no further requirement changes without updating the PRD in the same change (CLAUDE.md §1) | CLAUDE.md §1 |

Not in scope today (later days): scan thread, processing pipeline, controller
state machine, alarms wiring, exports, CLI, Qt, SECS/GEM, benchmarks.

## Tests that must exist and pass today

- **UT-STONEY-1 (forward-model half only)**: the simulated wafer generator
  reproduces the PRD §8.2 worked example — sigma = −180 MPa, t_f = 1 µm,
  t_s = 775 µm, M_s = 180.5 GPa ⇒ film curvature ≈ −0.00996 /m, total curvature
  with k0 = 0.002 /m ≈ −0.00796 /m, centre-lift on a 300 mm wafer ≈ 112 µm. This
  only checks the simulator's forward model matches hand calculation — it is
  **not** the full recovery test (that needs the Day 2 analysis pipeline and is
  covered by the full UT-STONEY-1 in `day-2-spec.md`).
- Config load/validate unit tests (valid file, missing keys use defaults,
  out-of-range value aborts with exit code 2, unknown key warns not aborts).
- Same-seed-same-data unit test for the wafer generator (FR-HW-2).
- Queue unit tests: bounded capacity, back-pressure policy, no data race (will be
  fully exercised by ThreadSanitizer later, but basic push/pop tests run today).

## Exit criteria (verbatim from PRD §13, row D1)

> CI green; UT-STONEY-1 forward model tests pass.

CI here means: GitHub Actions matrix builds and runs the (currently small) test
suite warning-free on macOS (clang), Linux (gcc) and Windows (MSVC) — SM9.

## Open questions to resolve today (PRD §16.3)

- **Q3**: Confirm standalone Asio (D-03) or fall back to Qt Network. Needed by D1.
  (Networking code isn't written yet, but the CMake dependency decision — whether
  to FetchContent Asio now — should be settled today since it affects `ssim_secsgem`'s
  later target setup.)
- **Q6**: Repository name and licence (MIT suggested). Needed by D1.
