# StressScan-Sim

A simulator of a cassette-to-cassette wafer film-stress metrology tool, written
as a portfolio project in C++17.

**Status: work in progress (draft README, Day 4 of 7).** The standalone machine,
the headless CLI and the Qt operator panel work. The SECS/GEM module has its
lowest two layers so far: a SECS-II item codec and an HSMS session with a
loopback server. The GEM behaviour (events, alarms, remote commands), the host
simulator, interop, benchmarks and the demo video do not exist yet, and this
README does not claim them.

## What it is, in plain English

Think of a washing machine with a phone app. The machine works alone: you load
it, press start, and it runs a cycle. The app is an optional extra that can
start the machine remotely.

Here the "clothes" are silicon wafers and the "cycle" is a laser scan that
measures how much a thin coating bends the wafer. The bend is turned into a
number (film stress) with Stoney's equation. There is no real laser or motor:
a simulator invents noisy readings from a hidden "true" stress, and the machine
software has to recover it. Because the answer is known, any error is
measurable.

Build order: (1) a fully working standalone machine, (2) SECS/GEM added later as
a separate module, (3) refactor and optimise with measured numbers.

No Frontier or SEMI names, logos or standard text are used. When SECS/GEM
arrives it will be described as a subset based on publicly documented
behaviour, not as certified or complete.

## Architecture

```
        optional link: TCP, HSMS / SECS-II / GEM      (not built yet)
 +------------------+  <------------------------------->  +----------------------------------+
 | Host simulator   |                                     | Equipment application            |
 +------------------+                                     |  +----------------------------+  |
                                                          |  | Core library               |  |
 +------------------+   operator actions / display        |  | controller, pipeline, logs |  |
 | Operator (Qt)    | <---------------------------------> |  +-------------+--------------+  |
 +------------------+                                     |                | IStage / ILaserSensor |
                                                          |  +-------------v--------------+  |
                                                          |  | Simulated hardware         |  |
                                                          |  +----------------------------+  |
                                                          +----------------------------------+
```

Modules (dependencies point downward only): `ssim_core` (config, queues, event
bus, controller, state machines, `MachineApi`), `ssim_hw` (hardware interfaces
and simulated devices), `ssim_analysis` (filters, fit, Stoney stress, wafer map,
writers), `ssim_machine` (`MachineRuntime`, which wires the three together),
then the `equipment_cli` and `equipment_qt` executables. `ssim_core` never
includes Qt, network or SECS/GEM headers. The reasoning is in `docs/PRD.md` and
`docs/decisions/`.

## Build and run (macOS, Apple Silicon)

Requirements: CMake 3.21+, a C++17 compiler, Qt 6 for the panel (optional).

```
# headless machine and tests
cmake --preset dev
cmake --build build -j2
ctest --test-dir build --output-on-failure

# one scan from the command line; writes results/<run_id>/W001/
./build/src/app_cli/equipment_cli --config config/default.json --rtf 0
```

Qt panel (needs Qt 6, for example `brew install qtbase`; the preset assumes it
is in `/opt/homebrew/opt/qtbase`, edit `CMakePresets.json` if yours differs):

```
cmake --preset qt
cmake --build build-qt -j2
./build-qt/src/app_qt/equipment_qt --config config/demo_alarm.json
```

Notes from the machine this was developed on:

- `-j2` is deliberate: with many parallel jobs, GoogleTest's 5-second test
  discovery step can time out on the first build.
- If your `cmake` is an Intel build running under Rosetta, add
  `-DCMAKE_OSX_ARCHITECTURES=arm64` for anything that links arm64 libraries
  (the `qt` preset already does).

### Using the panel

`config/demo_alarm.json` is set up for a short demo: the first wafer (W001)
scans cleanly, the second (W002) has an injected sensor-spike fault.

1. Press Start and watch progress; the wafer map and result appear when it ends.
2. Start again: W002 raises alarm 1001, the red banner appears and Start stays
   disabled until you press Clear alarm.
3. Switch Control to Online-Remote: Start is disabled (the host is in charge),
   while Abort stays available.
4. File > Load config... and File > Settings... rebuild the machine; both are
   only possible while idle. File > Open results folder shows the output files.

## What is verified

Run `ctest` for the current list. At the time of writing the dev build (which
includes the SECS/GEM module) has 252 passing tests. With
`-DSSIM_ENABLE_SECSGEM=OFF` the same Day 1 to 3 suite (136 tests) still builds
and passes, so the machine does not depend on the module. The build with the Qt
panel adds an offscreen test that clicks through the real window. Only what has
actually been run is claimed here.

SECS-II and HSMS (this project's own implementation, not a certified or
complete one): the item codec is tested with known answers, round trips at
every length boundary, every rejection case, and 100,000 seeded mutated inputs.
The HSMS session's timers are tested on a fake clock, and the server on
loopback sockets. Frame and item-header layout was cross-checked against the
open-source `secsgem` 0.3.0 library; a few values could not be confirmed and
are marked `TODO(verify)` in `docs/protocol-notes.md`.

Not verified yet:

- Linux and Windows CI results are not confirmed.
- ThreadSanitizer and AddressSanitizer/UBSan could not be run on the
  development machine (the sanitizer runtime fails even on an empty program
  there). A sanitizer workflow exists in `.github/workflows/sanitizers.yml` but
  has not run yet, so the multithreaded code and the codec fuzz test have not
  been checked by a sanitizer.
- The first demo GIF has not been recorded.

## Results

| Item | Value |
|---|---|
| Stress recovery accuracy | _to be filled from a measured run_ |
| Analysis time per wafer | _not yet benchmarked_ |
| Queue throughput v1 vs v2 | _not yet built_ |
| SECS/GEM interop | _not yet built_ |

## Known limitations

_To be completed as the project stabilises._ Already known: GEM behaviour is
not implemented (only the HSMS and SECS-II layers below it); the cassette loop is not implemented; the panel shows raw
heights, so the simulated wafer tilt dominates the map.

## How to explain this in an interview

_To be written once the project is finished._

## Licence

See `LICENSE`. Third-party licences (Qt under LGPL with dynamic linking,
GoogleTest BSD, nlohmann/json MIT, stb public domain) will be listed here in
full before submission.
