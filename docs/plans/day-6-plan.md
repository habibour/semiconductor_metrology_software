# Day 6 plan — refactor, sanitizers, v2 ring buffer + parallel fits + benchmarks, Octave check, local demo, macOS release

Read `docs/specs/day-6-spec.md` first. This day is explicitly "measure first" —
PRD §6.6: never optimize without a benchmark showing the problem, never publish a
number that wasn't measured. Order matters: sanitizers before optimization
(fix real bugs before chasing speed), refactor commit isolated from the
performance commit (separate, reviewable diffs), per the commit-story rule in
PRD §13.

## 1. Sanitizer pass (do this first — it can surface real bugs that change scope)

1. `cmake -S . -B build-tsan -DSSIM_SANITIZER=thread …` — run the *entire* test
   suite (unit, integration, scenario). Fix every real finding; each fix that
   corresponds to an actual bug becomes its own small commit with a regression
   test (CLAUDE.md §6.5, PRD §13 "a fix commit only for a real bug actually
   found").
2. `cmake -S . -B build-asan -DSSIM_SANITIZER=address …` (ASan+UBSan combined
   build per CLAUDE.md §6.5 — thread and address sanitizers cannot combine).
   Same process.
3. Document the lock hierarchy actually used (not aspirational) in
   `docs/architecture.md`, satisfying C4/NFR-CON-1.
4. Wire both sanitizer builds as CI jobs in `.github/workflows/sanitizers.yml`.

## 2. Refactor commit

1. Pick one real refactor with no behavior change — the PRD's own suggestion is
   "extracting the controller and state machine cleanly" if that's still
   entangled from Day 2. Tests must be green identically before and after; this
   is a single, focused commit, not a rewrite (CLAUDE.md §9 rule 5: don't
   reformat or restructure unrelated code).

## 3. Fuzz / mutation testing

1. `tests/fuzz/` — mutate SECS-II frames/bodies (bit flips, length tampering,
   truncation, nesting-depth attacks) and malformed HSMS frames; run ≥100,000
   mutations per CI invocation (FT-FUZZ-1, NFR-REL-1, SM7). Optional libFuzzer
   integration if Homebrew LLVM is available locally; otherwise a custom
   mutation-test harness is sufficient — either satisfies the requirement, don't
   block on libFuzzer specifically.
2. `tests/fuzz/soak_test.cpp` — 1,000 wafers at rtf 0, host connected, inject
   random link drops via the HSMS session layer; assert no deadlock and record
   resident memory growth (<5% target) (FT-SOAK-1, NFR-REL-2).

## 4. Performance work — benchmark first, then optimize, then re-benchmark

1. `bench/bench_queue.cpp` — measure v1 mutex queue throughput at 1/2/4
   producers on the sample path; record the baseline in `docs/benchmarks.md`
   *before* writing the ring buffer (BM-QUEUE-1 baseline half).
2. `src/core/spsc_ring_queue.cpp` — v2 lock-free single-producer/single-consumer
   ring buffer behind the existing `IQueue` interface from Day 1 (D-09 pays off
   here: no caller changes needed). Every explicit memory-order use carries a
   comment explaining why, and gets its own stress test (CLAUDE.md §6.2).
   → `tests/unit/core/spsc_ring_queue_test.cpp` + a TSan-specific stress test.
3. Re-run `bench_queue` with v2; record the after numbers in
   `docs/benchmarks.md`; confirm ≥2x throughput (SM3) before claiming it
   anywhere else.
4. `bench/bench_analysis.cpp` — serial vs Day 2's thread-pool per-line fits on
   the 72,000-sample nominal wafer at rtf 0; record baseline and after numbers
   (BM-ANALYSIS-1); confirm ≥25% improvement target (SM4) and the <100 ms
   target (NFR-PERF-1) — record what was actually measured either way.
5. `bench/bench_codec.cpp` — SECS-II encode+decode throughput on small messages,
   single thread, against the NFR-PERF-3 target (100,000/s).
6. Coverage: run llvm-cov/gcov across `ssim_core`, `ssim_analysis`,
   `ssim_secsgem`; record the real percentage against the 80% target (SM10) —
   report the actual number even if it's below target.

## 5. Octave cross-check

1. `scripts/check_stress.m` — recompute stress from an exported sample CSV,
   report % difference from the machine's own result (FR-TOOL-1); run it in GNU
   Octave (claim MATLAB only if actually run there — CLAUDE.md §11 Never do,
   PRD §12/§18).
   → this is XT-OCTAVE-1; record the actual % difference against the 0.5%
   target.

## 6. Local demo + macOS release (no Docker, no Linux: PRD D-11)

1. `scripts/demo.sh` — one command: starts `equipment_cli serve` on a free
   port, runs `scenarios/normal_run.scn` with `host_sim`, prints the message
   trace, leaves the wafer map and CSV in a results folder, and shuts the
   machine down. Exit code follows the scenario result.
2. `.github/workflows/release.yml` — on a version tag: build the macOS app
   (macdeployqt) and attach it to the GitHub Release. Unsigned; the README
   explains how to open it.
3. Tag `v0.3` once sanitizers are clean and numbers are recorded.

## 7. Stretch list (attempt only after §1–6 are done, strictly in this order —
this mirrors the PRD's own cut order so cutting later items first is correct):

1. `src/device_sim/` — framed binary protocol (start byte, length, command,
   payload, CRC-16) over TCP, `IStage`/`ILaserSensor`-compatible adapter,
   reject-and-count bad CRC frames (FR-HW-7).
2. C# .NET host subset (S1F1, S1F13, S2F41, S6F11-ack) — legitimate C#
   experience without WPF (FR-HOST-4).
3. FR-CFG-3 (runtime-settable equipment constants) / FR-GEM-7/8 (S2F13-16,
   S2F33/35/37) if not done Day 5; FR-HSMS-8 (spooling).
4. Real MATLAB run, only if MATLAB is actually accessible.
5. Cassette loop, only if deferred from Day 3.
6. Q1 is answered (no Windows, PRD D-11): Tier 3 (C# WPF, FR-HOST-5) is
   excluded. Nothing to attempt.

## 8. CV alignment tasks (parallel, small — PRD §13)

- Resolve Q4 (which academic projects stay), Q5 (internship duties, honest
  bullet alignment), Q7 (competitive-programming profile / travel line).
- Fill in Appendix B draft bullets with the numbers actually measured today —
  never a placeholder left in as if it were real.

## Day 6 done-checklist (maps back to day-6-spec.md)

- [ ] ThreadSanitizer and ASan/UBSan builds clean on the whole suite.
- [ ] FT-FUZZ-1 (≥100,000 mutated frames) and FT-SOAK-1 (1,000 wafers, link
      drops) pass.
- [ ] `docs/benchmarks.md` has real, measured before/after numbers for queue
      throughput, analysis time, and codec throughput — nothing invented.
- [ ] SM3 (≥2x queue throughput) and SM4 (≥25% analysis speedup) reported
      truthfully, met or not.
- [ ] Coverage number recorded (SM10 target 80%, report actual).
- [ ] XT-OCTAVE-1 run and % difference recorded.
- [ ] `scripts/demo.sh` runs the demo end-to-end on this Mac.
- [ ] Release workflow builds the macOS app on a tag.
- [ ] Tag `v0.3` pushed.
- [ ] Stretch items attempted in cut order only if time remained, and any
      skipped item is explicitly noted as skipped, not silently absent.
