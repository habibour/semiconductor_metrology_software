# Measurements

Only numbers that were actually measured are recorded here. README figures are
copied from this file.

## Environment for the Day 6 benchmarks

| Field | Value |
|---|---|
| Date | 2026-09-25 |
| Machine | Apple M4 (10 cores: 4 performance, 6 efficiency), on AC power |
| OS | macOS 26.5.1 |
| Compiler | Apple clang 17.0.0 (clang-1700.6.4.2) |
| Build | Release (`-O3 -DNDEBUG`), `-DSSIM_BUILD_BENCH=ON` |
| Method | own harness (`bench/bench_common.hpp`), no benchmark library; median of several runs, worst and best shown |
| Caveat | ordinary desktop use in the background (load average about 2 while measuring), so run-to-run noise is real; the efficiency cores make the worst case slower |

Run them with `cmake -S . -B build-bench -DCMAKE_BUILD_TYPE=Release -DSSIM_BUILD_BENCH=ON
-DSSIM_ENABLE_SECSGEM=ON && cmake --build build-bench` and then
`build-bench/bench/bench_queue`, `bench_analysis`, `bench_codec`.

## Baselines (before any optimisation)

Recorded first, as CLAUDE.md 6.6 requires: the simple version stays until its
numbers are written down.

### Sample-path queue, v1 (mutex and condition variable), BM-QUEUE-1

Producer thread(s) to one consumer, capacity 1024, blocking back-pressure, 2 M items
per run, 5 runs. Every run checks that each item arrived exactly once.

| queue | payload | producers | median M items/s | worst | best |
|---|---|---|---|---|---|
| v1 mutex + condvar | 8 B | 1 | 23.00 | 16.92 | 23.63 |
| v1 mutex + condvar | 8 B | 2 | 11.60 | 10.55 | 12.11 |
| v1 mutex + condvar | 8 B | 4 | 5.95 | 5.87 | 6.35 |
| v1 mutex + condvar | 64 B | 1 | 20.13 | 19.62 | 20.75 |

### Analysis of one nominal wafer, BM-ANALYSIS-1 and NFR-PERF-1

6 lines, 72,006 samples, real-time factor 0, 15 runs. NFR-PERF-1 asks for at most
100 ms; the baseline **misses it (112 ms)**.

| stage (single thread) | median ms |
|---|---|
| edge exclusion, 6 lines | 0.462 |
| outlier rejection, 6 lines | 113.233 |
| line fits, 6 lines | 0.113 |
| wafer map | 1.123 |

| pool threads | pipeline median ms | min | max |
|---|---|---|---|
| 1 | 112.697 | 111.873 | 120.882 |
| 2 | 112.136 | 111.651 | 113.032 |
| 4 | 112.305 | 111.567 | 124.211 |
| 6 | 112.069 | 111.142 | 112.570 |

What this shows: nearly all of the time is outlier rejection, and the thread pool
makes no difference (1 thread and 6 threads are the same) because it only runs the
line fits, which take 0.1 ms. The PRD's plan to speed analysis up with parallel
per-line fits would not have helped; the bottleneck had to be found by measuring.

### SECS-II codec, NFR-PERF-3

A 62-byte S6F11-style body (a wafer-result event with a seven-value report), one
thread, 400,000 iterations per run, 9 runs. Target: at least 100,000 encode plus
decode per second.

| operation | median k ops/s | worst | best |
|---|---|---|---|
| encode | 1817 | 1138 | 2052 |
| decode | 588 | 464 | 628 |
| encode + decode | 451 | 427 | 466 |

The target is met (4.5 times over) without any optimisation, so none was done.


## After optimisation

### Analysis of one nominal wafer, step 1: outlier rejection (BM-ANALYSIS-1, NFR-PERF-1, SM4)

Change: the sliding-window median/MAD filter no longer allocates two vectors and
runs two `nth_element` calls per sample. It keeps one sorted window that gains
and loses one value per step, and finds the MAD by merging the two sorted sides
of the window around the median. The result is bit-identical to the old version:
`outlier_equivalence_test.cpp` keeps the old code as a reference and compares
every flag on 2,500 random lines (short lines, ties, spikes, edge-excluded
gaps, several window radii) and on full-size 12,001-sample lines.

Same machine, same build, same wafer (6 lines, 72,006 samples), median of runs.
Load average was 4.5 during this run, higher than for the baseline, so the
"after" numbers are if anything pessimistic.

| | before | after | change |
|---|---|---|---|
| outlier rejection, 6 lines, single thread | 113.233 ms | 28.001 ms | 4.0x faster |
| pipeline, 1 pool thread | 112.697 ms | 29.719 ms | 3.8x faster (-73.6%) |
| pipeline, 2 pool threads | 112.136 ms | 29.684 ms | |
| pipeline, 4 pool threads | 112.305 ms | 29.610 ms | |
| pipeline, 6 pool threads | 112.069 ms | 29.627 ms | |

NFR-PERF-1 (under 100 ms): met, 29.7 ms. SM4 (at least 25% faster): met, 73.6%
faster. The pool size still changes nothing because the per-line fits are only
0.117 ms; the filter is still run serially, line by line. Whether spreading the
six lines over threads helps is measured in the next step.

### Analysis of one nominal wafer, step 2: per-line filtering on the pool (BM-ANALYSIS-1, SM4)

Change: edge exclusion and outlier rejection for each line now run as pool jobs
(`FitThreadPool::for_each_index`); aggregation stays serial and in line order.
`FitThreadPool.PipelineResultDoesNotDependOnThreadCount` checks that stress,
every sample flag and the fits are bit-identical for 1, 2, 3 and 6 threads.

Same machine and wafer, median of runs (run under the same background load as
step 1). The wafer has six lines, so more than six threads cannot help.

| pool threads | baseline (all serial) | step 1 (faster filter) | step 2 (filter on pool) | speed-up vs baseline |
|---|---|---|---|---|
| 1 | 112.697 ms | 29.719 ms | 30.336 ms | 3.7x |
| 2 | 112.136 ms | 29.684 ms | 15.697 ms | 7.1x |
| 4 | 112.305 ms | 29.610 ms | 11.004 ms | 10.2x |
| 6 | 112.069 ms | 29.627 ms | 7.455 ms | 15.0x |

Four threads are slower than the ideal (30.3 / 4 = 7.6 ms) because six lines do
not divide evenly over four workers (two workers get two lines). The production
default pool is one thread fewer than the hardware threads.

### Sample-path queue, v2 (lock-free SPSC ring), BM-QUEUE-1 and SM3

Change: `SpscRingQueue<T>` (`include/ssim/core/spsc_ring_queue.hpp`), same API
and back-pressure policies as v1. Lock-free fast path with cached indices and
cache-line separated head/tail; it parks on a mutex and condition variable only
when the ring is full or empty. One producer and one consumer only, so only the
one-producer rows exist for it. Same machine and harness as the baseline
(capacity 1024, blocking policy, 2,000,000 items per run, 5 runs, integrity
checksum checked every run). Load average about 3.6 during these runs.

| queue | payload | producers | median M items/s | worst | best | vs v1, same row |
|---|---|---|---|---|---|---|
| v1 mutex + condvar | 8 B | 1 | 21.62 | 18.44 | 21.96 | |
| v2 lock-free ring | 8 B | 1 | 127.72 | 119.69 | 140.36 | 5.9x |
| v1 mutex + condvar | 64 B | 1 | 19.35 | 16.79 | 19.55 | |
| v2 lock-free ring | 64 B | 1 | 36.36 | 34.37 | 37.63 | 1.9x |

(The v1 rows in this table are from the same run as the v2 rows, so the ratio
compares like with like; the baseline section above is an earlier run of v1,
23.00 and 20.13 M items/s.) An independent earlier run of the same binary gave
124.34 and 35.01 M items/s for v2.

SM3 (at least 2x the mutex queue): met for the 8-byte item (5.9x, 2.8x even
against the worst-run of v1). Not met for the 64-byte item: 1.88x. With a 64-byte
payload every hand-over moves a whole cache line between the two cores, and that
transfer, not the queue's bookkeeping, sets the limit. I tried giving every slot
its own cache line; it made the 8-byte case 3x slower (43 M items/s) because
small items no longer share a line, so that change was reverted.

What this means in the product: the sample path moves about 6 blocks per wafer,
so queue speed is not where a wafer's time goes (a scan takes seconds, analysis
about 8 to 30 ms). The ring is there because the design called for a swappable
lock-free v2 with measured numbers, and it removes a mutex from the scan thread's
push, not because the queue was a bottleneck.

## Line coverage, SM10

`llvm-cov`/`llvm-profdata` from Xcode Command Line Tools, against a separate
Debug build instrumented with `-fprofile-instr-generate -fcoverage-mapping`
(`build-cov/`, `SSIM_ENABLE_SECSGEM=ON`). All 461 non-soak tests were run once
with `LLVM_PROFILE_FILE` set to one file per process, merged with
`llvm-profdata merge -sparse`, then reported per module by passing that
module's own `.cpp` files as `llvm-cov report`'s source arguments (not a
filename regex — an earlier attempt at a negative-lookahead ignore-regex
silently matched nothing, which would have been reported as coverage across
every dependency; passing the real source list is unambiguous). The soak test
(FT-SOAK-1) was excluded only for wall-clock budget; it exercises the same
`MachineRuntime`/HSMS code the machine-runtime and scenario tests already
cover, so its lines were not expected to add new ones. Merging profile data
from 47 differently-linked test binaries prints "507 functions have mismatched
data" — a known benign warning for header-only templates
(`BoundedQueue<T>`, `SpscRingQueue<T>`, `Result<T>`, ...) instantiated
differently across binaries; `llvm-cov` still attributes their executed lines
correctly, it just cannot reconcile every instantiation's function-level
counters against every other one.

| Module | Lines | Missed | Line coverage | Target (SM10) |
|---|---|---|---|---|
| `ssim_core` | 1020 | 135 | **86.76%** | 80% — met |
| `ssim_analysis` | 642 | 38 | **94.08%** | 80% — met |
| `ssim_secsgem` | 2134 | 150 | **92.97%** | 80% — met |
| `ssim_hw` (not in SM10's list, measured anyway) | 246 | 26 | 89.43% | — |
| `src/machine/machine_runtime.cpp` (not in SM10's list, measured anyway) | 318 | 48 | 84.91% | — |

Reproduce:

```
cmake -S . -B build-cov -DCMAKE_BUILD_TYPE=Debug -DSSIM_BUILD_TESTS=ON -DSSIM_ENABLE_SECSGEM=ON \
  -DCMAKE_CXX_FLAGS="-fprofile-instr-generate -fcoverage-mapping" \
  -DCMAKE_EXE_LINKER_FLAGS="-fprofile-instr-generate -fcoverage-mapping"
cmake --build build-cov -j2
mkdir -p build-cov/profraw
LLVM_PROFILE_FILE="$PWD/build-cov/profraw/%p.profraw" ctest --test-dir build-cov -j2 -E Soak
xcrun llvm-profdata merge -sparse build-cov/profraw/*.profraw -o build-cov/coverage.profdata
xcrun llvm-cov report build-cov/tests/<one binary> -object build-cov/tests/<...next binary...> ... \
  -instr-profile=build-cov/coverage.profdata src/core/*.cpp   # or src/analysis, src/secsgem
```

## GUI responsiveness during a scan (FR-UI-3, NFR-PERF-4) — informal

Test: `QtPanelSmoke` / `scanKeepsGuiResponsiveAndRateLimited`
(`tests/unit/app_qt/panel_smoke_test.cpp`). A 10 ms heartbeat timer on the GUI
thread records the longest gap between ticks while one full wafer is scanned
and processed; progress signals reaching the GUI are counted.

| Field | Value |
|---|---|
| Date | 2026-09-25 (re-measured after progress became per-percent) |
| Machine / OS | Apple Silicon Mac, macOS 26.5.1 |
| Compiler / Qt | AppleClang 17, Qt 6.9.3 |
| Build | Debug, Qt `offscreen` platform (no real window rendering) |
| Settings | default wafer, real-time factor 4.0 |
| Runs | 3 |

| Run | Scan + processing time | Progress signals to the GUI | Longest GUI-thread gap |
|---|---|---|---|
| 1 | 6077 ms | 105 | 21 ms |
| 2 | 6124 ms | 104 | 21 ms |
| 3 | 6076 ms | 103 | 19 ms |

Reading: the target is a gap of at most 50 ms and at most about 30 progress
updates per second. The gap held in all three runs. The scan thread now reports
progress on every whole percent (about 100 events per wafer), so the bar moves
smoothly; here that is about 17 updates per second, which is **below** the 30 Hz
limit, so the panel's 33 ms coalescing timer had nothing to cut and the limit
itself is not exercised by this measurement. (Before 2026-09-25 progress was
reported once per scan line, 6 events per wafer; an earlier version of this
table recorded 7 signals and gaps of 20 and 21 ms.) Limits of the measurement:
three runs, offscreen (so painting cost is not the real one), Debug build, one
machine.
