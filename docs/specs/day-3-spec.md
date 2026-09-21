# Day 3 spec — Qt panel, wafer map, control modes, progress; README draft; cassette loop if time

Source of truth: `docs/PRD.md`. This file is a filtered index into it.

PRD §13, row D3: "Qt panel, wafer map, control modes, progress; README draft;
cassette loop if time." Exit: "Panel usable; first GIF."

## In scope today

| ID / section | What it requires | PRD ref |
|---|---|---|
| FR-UI-1 | Status bar: comm/control/process states; Start/Stop/Abort/ClearAlarm enabled only when rules allow; control-mode selector | PRD §7.8 |
| FR-UI-2 | Live scan progress, wafer map, last result + uncertainty, alarm banner, scrolling log + message trace | PRD §7.8 |
| FR-UI-3 | GUI thread never runs long work; updates via queued signals at ≤30 Hz | PRD §7.8, NFR-PERF-4 |
| FR-UI-4 | Settings dialog for permitted values | PRD §7.8 |
| FR-UI-5 | Export and open-results-folder actions | PRD §7.8 |
| FR-UI-6 | Demo mode suitable for recording a clean video (could-have; nice if time) | PRD §7.8 |
| Control state model | Offline / Online-Local / Online-Remote fully operator-switchable in the panel (host-side S1F15/S1F17 handling is Day 5, but the state model and operator switch exist now) | PRD §6.5 |
| FR-MC-4 | Control-state rules enforced for the UI command source specifically | PRD §7.5 |
| Facade pattern | `MachineApi` — the small, stable surface the Qt panel (and later CLI/SECS-GEM) calls into | PRD §6.6 |
| NFR-DOC-1 (partial) | README quickstart draft started (full "under 10 minutes from clean checkout" claim isn't verifiable until more exists — today is a draft) | PRD §9 |

### Stretch (Tier 2, only if Days 1–2 are fully green — PRD's own cut order keeps
cassette loop as the *last* Tier-2 item to cut, i.e. safest stretch pick)

| ID | What it requires | PRD ref |
|---|---|---|
| FR-HW-6 | Cassette simulator, 1–25 slots, empty slots, per-wafer truth values | PRD §7.2 |
| FR-MC-5 | Cassette processing: slot order, skip empty, continue after out-of-spec, halt on alarm, cassette summary | PRD §7.5 |
| FR-OUT-4 | Cassette summary CSV | PRD §7.7 |

Not in scope today: SECS/GEM (Days 4–5), benchmarks/v2 ring buffer (Day 6).

## Tests / manual verification today

- No new PRD-listed unit-test IDs are assigned to Day 3 specifically (UI is
  verified by demo/inspection, "D" in the verification key) — but FR-UI-3's ≤30 Hz
  and "GUI thread never blocks" claims should get at least one benchmark-style
  check (D, B) before being called done, even if informal today.
- If the cassette stretch is attempted: cover it with `tests/integration/cassette_test.cpp`
  (FR-MC-5: skip empty slot, continue after out-of-spec wafer, halt on alarm).

## Exit criteria (verbatim from PRD §13, row D3)

> Panel usable; first GIF.

"Panel usable" means: an operator can load the nominal config, switch control
mode, Start/Stop/Abort, watch progress and the wafer map update live, see an
alarm banner on a fault, and clear it — end to end, without touching the CLI.
