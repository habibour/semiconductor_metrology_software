# StressScan-Sim

A simulator of a cassette-to-cassette wafer film-stress metrology tool, written
as a portfolio project in C++17.

**Status: work in progress (draft README, Day 5 of 7).** The standalone machine,
the headless CLI and the Qt operator panel work. The optional SECS/GEM module
is built as a subset of publicly documented behaviour (SECS-II codec, HSMS
session and server, GEM communication and control states, remote commands,
events, alarms, status variables, S9 errors) with a scripted host simulator, seven
scenario scripts and an interop test against an independent open-source host.
Not done yet: the cassette loop, equipment constants and dynamic reports,
benchmarks, the Docker demo and the demo video. This README claims nothing
about those. It is not a certified or complete GEM implementation.

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

### Running the machine with a host

```
# the machine, listening on a port chosen by the OS (loopback only), Online-Remote
./build/src/app_cli/equipment_cli serve --port 0 --control remote --rtf 0
# prints:  listening on 127.0.0.1:<port>

# in another terminal: run a scenario script against it
./build/src/host_sim/host_sim --connect 127.0.0.1:<port> --script scenarios/normal_run.scn

# the independent interop check (needs: pip install secsgem==0.3.0)
python3 scripts/interop_secsgem.py --cli build/src/app_cli/equipment_cli --config config/demo_alarm.json
```

The script language is described in `docs/scenario-format.md`. Configure with
`-DSSIM_INTEROP_PYTHON=<python with secsgem>` to have CTest run the interop
check as `XT-SECSGEM-1`; without it CTest says at configure time that the test
is not registered.

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
includes the SECS/GEM module) has 336 passing tests. With
`-DSSIM_ENABLE_SECSGEM=OFF` the same Day 1 to 3 suite (140 tests) still builds
and passes, so the machine does not depend on the module. The build with the Qt
panel adds an offscreen test that clicks through the real window. Only what has
actually been run is claimed here.

SECS/GEM subset (this project's own implementation):

- The SECS-II codec is tested with known answers, round trips at every length
  boundary, every rejection case, and 100,000 seeded mutated inputs.
- The HSMS session's timers T3, T6, T7 and T8 are tested on a fake clock, and
  the server on loopback sockets.
- The GEM layer is tested with the real controller and fake sockets (33 tests).
- Seven scenario scripts (`scenarios/`) run against a real machine over a real
  socket: normal run, alarm recovery, bad commands, wrong control state,
  malformed frames, link loss and T3 timeout.
- **Interop (XT-SECSGEM-1):** the independent Python library `secsgem` 0.3.0,
  acting as a host, performed S1F13, S1F3 and S2F41 and received S6F11 and S5F1
  against `equipment_cli serve`: 15 of 15 checks passed, and a run set up to
  fail did fail. Details and what it does not confirm are in
  `docs/protocol-notes.md`.

Not verified yet:

- Linux CI is not yet green (a test fails on Ubuntu, see the Actions tab). Windows is out of scope.
- The cassette-run scenario (ST-cassette_run) does not exist: the cassette loop is not built.
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

_To be completed as the project stabilises._ Already known: equipment constants
(S2F13-16) and dynamic reports (S2F33-38) are not implemented and answer S9F5;
there is no spooling; the communication state model is simplified; the cassette loop is not implemented; the panel shows raw
heights, so the simulated wafer tilt dominates the map.

## How to explain this in an interview

_To be written once the project is finished._

## Licence

See `LICENSE`. Third-party licences (Qt under LGPL with dynamic linking,
GoogleTest BSD, nlohmann/json MIT, stb public domain) will be listed here in
full before submission.
