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
