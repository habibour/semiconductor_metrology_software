# Day 5 plan — GEM module, integration, host simulator, scenarios, interop

Read `docs/specs/day-5-spec.md` first. Create `src/secsgem/gem/` and
`src/host_sim/` today. Build order: GEM state models/handlers (unit-tested in
isolation) → wire into `equipment_cli` behind `SSIM_ENABLE_SECSGEM` → `host_sim`
→ scenario scripts → interop test.

## 1. GEM message catalogue (`src/secsgem/secs2/message_factory.cpp`, extended)

1. Register builders/parsers for: S1F1/F2, S1F13/F14, S1F3/F4, S1F15/F16,
   S1F17/F18, S2F41/F42, S5F1/F2, S6F11/F12, S9F1/F3/F5/F7/F9/F11, per the
   bodies in PRD §8.6.3. This is the "full catalogue" the Day 4 factory
   mechanism was built to hold.
   → `tests/unit/secsgem/secs2/catalogue_test.cpp` (each message round-trips;
   unknown stream/function/illegal data produce the right S9 per FR-S2-3).
2. Identifiers (CEID 2001–2008, RPTID 3001–3003, ALID 1001–1008, SVID
   4001–4012, ECID 5001–5006) live in one catalogue file, not scattered
   constants (CLAUDE.md §5.2): `include/ssim/secsgem/gem/identifiers.hpp`.

## 2. GEM state models and handlers (`src/secsgem/gem/`)

1. `src/secsgem/gem/communication_state.cpp` — NotCommunicating/Communicating;
   S1F13/S1F1 answered in every state (FR-GEM-1).
2. `src/secsgem/gem/control_state.cpp` — Offline/Online-Local/Online-Remote;
   operator switch (already exists in the Day 3 UI, now actually enforced
   against host requests); S1F15/S1F17 handling, `comm.allow_host_online` gate
   (FR-GEM-2).
   → `tests/integration/gem/control_state_test.cpp`.
3. `src/secsgem/gem/remote_commands.cpp` — S2F41 → START/STOP/ABORT/CLEAR_ALARM,
   parameter validation, HCACK values (0 done, 1 unknown, 2 cannot-perform-now,
   3 invalid-parameter, 4 accepted-completion-later), CPACK per parameter
   (FR-GEM-3). This is the point where SECS/GEM becomes a real `MachineApi`
   caller — commands enter the *same* Day 2 command queue as UI/CLI, tagged with
   a SECS/GEM source (FR-MC-2 completed).
   → `tests/integration/gem/remote_commands_test.cpp` covers IT-CTRL-2 (Start
   during alarm refused; ClearAlarm unblocks it).
4. `src/secsgem/gem/event_reporting.cpp` — subscribes to the Day 2 event bus,
   maps ScanStarted/ScanComplete/WaferOutOfSpec/RunAborted/etc. to CEID
   2001–2008, sends S6F11 with RPTID 3001/3002/3003 bodies, awaits S6F12
   (FR-GEM-4).
5. `src/secsgem/gem/alarm_reporting.cpp` — alarm set/clear (from Day 2's alarm
   catalogue) → S5F1, awaits S5F2 (FR-GEM-5).
6. `src/secsgem/gem/status_variables.cpp` — S1F3/S1F4 for SVID 4001–4012,
   sourced from live controller/analysis state; unknown SVID → empty item
   (FR-GEM-6).
7. `src/secsgem/gem/error_handling.cpp` — S9F1/F3/F5/F7/F9/F11 triggered from
   the codec/session layers per the situations in PRD §8.6.3 (FR-GEM-9).
   → `tests/integration/gem/gem_roundtrip_test.cpp` covers IT-GEM-1.

## 3. Wire into `equipment_cli`

1. `src/app_cli/main.cpp`: when `SSIM_ENABLE_SECSGEM` is on and `comm.enabled`
   is true, start the HSMS listener (Day 4) + GEM layer alongside the existing
   controller; confirm `comm.enabled=false` and `SSIM_ENABLE_SECSGEM=OFF` both
   still produce a fully working standalone run (re-run Day 1–3 tests to check
   for regressions — FR-MC-3).

## 4. Host simulator (`host_sim` target, depends on `ssim_secsgem` only)

1. `src/host_sim/script_parser.cpp` — parse the grammar in PRD §8.7 (connect,
   select, send, expect, wait-event, assert, disconnect) into `docs/scenario-format.md`
   (write the real grammar here now that it's being implemented, not just the
   illustrative PRD sketch).
2. `src/host_sim/main.cpp` — runs a script, logs every decoded message, exits
   non-zero on any failed expectation (FR-HOST-1/2).
3. `scenarios/*.scn` — normal_run, cassette_run, alarm_recovery, link_loss,
   malformed_frames, bad_commands, wrong_control_state, t3_timeout (FR-HOST-3).
   → `tests/scenario/*_test.cpp` (or a CI step invoking `host_sim` against a
   spawned `equipment_cli`) covers ST-normal_run through ST-t3_timeout.

## 5. Interop test

1. `scripts/interop_secsgem.py` — using the independent `secsgem` Python
   package, perform S1F13, S1F3, S2F41, receive S6F11 and S5F1 against a live
   `equipment_cli` (FR-TOOL-2).
   → this *is* XT-SECSGEM-1; run it and record the result honestly — if
   `secsgem` isn't installable in the current environment, say so explicitly
   rather than skipping silently (A5 in PRD §16.1 already anticipates this).

## 6. Stretch (only if everything above is green)

- FR-GEM-7 (S2F13-16 equipment constants) and FR-GEM-8 (S2F33/35/37 dynamic
  reports) — should-priority; defer to Day 6 stretch list if today runs long.

## Day 5 done-checklist (maps back to day-5-spec.md)

- [ ] IT-GEM-1 and IT-CTRL-2 pass.
- [ ] All 7 scenario tests (ST-normal_run … ST-t3_timeout) pass.
- [ ] XT-SECSGEM-1 run and result recorded (pass, or honestly reported as
      blocked with the reason).
- [ ] `SSIM_ENABLE_SECSGEM=OFF` / `comm.enabled=false` still fully functional —
      no regression from Days 1–3.
- [ ] Tag `v0.2` pushed.
- [ ] Q1 answered (even if the answer is "no Windows access, Tier 3 excluded").
