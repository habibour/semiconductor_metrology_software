# Measurements

Only numbers that were actually measured are recorded here. README figures are
copied from this file.

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
