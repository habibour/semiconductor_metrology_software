# Day 6 spec — refactor, sanitizers, v2 ring buffer + parallel fits + benchmarks, Octave check, Docker, release

Source of truth: `docs/PRD.md`. This file is a filtered index into it.

PRD §13, row D6: "Refactor commit; sanitizer findings fixed; v2 ring buffer and
parallel fits with benchmarks; Octave check; Docker demo; release workflow."
Exit: "Numbers recorded; tag v0.3."

This is the "maintain, fix, refactor, optimize" day the job posting explicitly
asks for (PRD §15) — the commit story matters as much as the code: a refactor
commit, real bug-fix commits (only for real bugs actually found), and a
performance commit with before/after numbers (PRD §13, commit-story paragraph).
Never invent a benchmark number or a bug story (CLAUDE.md §11, PRD §6.6/§9 rule 7).

## In scope today

| ID / section | What it requires | PRD ref |
|---|---|---|
| Refactor commit | At least one real refactor visible in history (e.g. extracting the controller and state machine cleanly) — no behavior change, tests still green before and after | PRD §13 |
| NFR-CON-1 | Whole test suite ThreadSanitizer clean; lock hierarchy documented in `docs/architecture.md` | PRD §9 |
| NFR-REL-1, SM6 | AddressSanitizer + UBSan clean; no crash/hang on ≥100,000 mutated frames per CI run | PRD §9, §3.3 |
| NFR-REL-2 | Soak test: 1,000 wafers at rtf 0, host connected, random link drops; no deadlock; resident memory growth <5% | PRD §9 |
| D-09 / SM3 | v2 lock-free SPSC ring buffer replacing the Day 1 mutex queue for the sample path, swappable behind the existing `IQueue` interface, ≥2x throughput vs v1 | PRD §11.1, §3.3 |
| FR-PRC-9 / SM4 | Parallel per-line fits benchmarked against serial; record baseline then ≥25% faster after optimization for the 72,000-sample nominal wafer at rtf 0 | PRD §7.4, §3.3 |
| SM5, SM7 | ThreadSanitizer-clean CI job; fuzz/mutation CI job at ≥100,000 frames | PRD §3.3 |
| SM10 | Line coverage ≥80% on `ssim_core`, `ssim_analysis`, `ssim_secsgem`, measured via llvm-cov/gcov | PRD §3.3 |
| FR-TOOL-1 | `scripts/check_stress.m` recomputes stress from exported CSV, reports % difference, runs in Octave or MATLAB | PRD §7.14 |
| 12.2 | GitHub Release on version tag (Windows zip via windeployqt, macOS app via macdeployqt, Linux archive); Docker image (`equipment_cli` + `host_sim`), `docker compose up` runs the demo scenario, amd64+arm64 | PRD §12.2 |
| NFR-PERF-1..3 | Baseline-then-improved numbers for wafer analysis time, sample-path throughput, SECS-II codec encode+decode rate — recorded in `docs/benchmarks.md`, copied to README from there, never invented | PRD §9, §6.6 |

### Stretch, in the PRD's own cut order (attempt only after everything above is
green; cut in this order if time runs out — earliest-listed goes first):

1. `device_sim` (Tier 2, FR-HW-7): framed binary protocol, CRC-16, over TCP.
2. C# .NET host (Tier 2, FR-HOST-4): subset of S1F1/S1F13/S2F41/S6F11-ack.
3. Runtime-settable equipment constants + dynamic reports if not already done
   Day 5 (FR-CFG-3, FR-GEM-7/8) and spooling (FR-HSMS-8).
4. MATLAB check is already must-do above via Octave; a *real MATLAB* run (not
   just Octave) is the stretch variant, only if MATLAB is actually available —
   otherwise the Octave result stands and no MATLAB claim is made.
5. Cassette loop (FR-HW-6/FR-MC-5/FR-OUT-4) — only if it was deferred from Day 3.

## Tests that must exist and pass today

- FT-FUZZ-1 (100,000 mutated frames, no crash/hang/sanitizer finding)
- FT-SOAK-1 (1,000 wafers, random link drops, no deadlock, <5% memory growth)
- BM-QUEUE-1 (mutex queue v1 vs lock-free ring v2 at 1/2/4 producers)
- BM-ANALYSIS-1 (serial vs thread-pool per-line fits)
- XT-OCTAVE-1 (Octave stress from exported CSV within 0.5% of machine result)

## Exit criteria (verbatim from PRD §13, row D6)

> Numbers recorded; tag v0.3.

## Open questions to resolve today (PRD §16.3)

Q1 (if not already resolved Day 5), Q4 (which academic projects stay on the
CV), Q5 (internship duties for honest bullet alignment), Q6 (if not already
resolved Day 1), Q7 (competitive-programming profile / travel line).
