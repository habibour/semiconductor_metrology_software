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
| Date | 2026-09-24 |
| Machine / OS | Apple Silicon Mac, macOS 26.5.1 |
| Compiler / Qt | AppleClang 17, Qt 6.9.3 |
| Build | Debug, Qt `offscreen` platform (no real window rendering) |
| Settings | default wafer, real-time factor 4.0 |
| Runs | 2 |

| Run | Scan + processing time | Progress signals to GUI | Longest GUI-thread gap |
|---|---|---|---|
| 1 | 6071 ms | 7 | 20 ms |
| 2 | 6141 ms | 7 | 21 ms |

Reading: the target is a gap of at most 50 ms and at most about 30 progress
updates per second. Both held in these two runs. Limits of this measurement:
two runs only, offscreen (so painting cost is not the real one), Debug build,
one machine. Progress is currently published once per scan line by the scan
thread, so 7 signals (6 lines plus the reset) says little about the 30 Hz cap;
that cap is enforced by the bridge's 33 ms timer and would need a scan that
reports faster to be exercised.
