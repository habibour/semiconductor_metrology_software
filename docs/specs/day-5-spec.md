# Day 5 spec — GEM module, integration, host simulator, scenarios, interop

Source of truth: `docs/PRD.md`. This file is a filtered index into it.

PRD §13, row D5: "GEM module, integration into the app as a separate step,
host_sim and scenarios, interop test." Exit: "ST scenarios pass; XT-SECSGEM-1
passes; tag v0.2."

This is where SECS/GEM stops being standalone-tested and gets wired into
`equipment_cli` as an optional module — build order matters: get the GEM state
models and handlers compiling and unit-tested first, *then* integrate, so a
regression is easy to bisect.

## In scope today

| ID / section | What it requires | PRD ref |
|---|---|---|
| FR-GEM-1 | Communication state model (NotCommunicating/Communicating, simplified); S1F13/S1F1 handled in every state | PRD §7.13, §6.5 |
| FR-GEM-2 | Control state model with operator switching + host requests S1F15/S1F17 | PRD §7.13, §6.5 |
| FR-GEM-3 | Remote commands via S2F41: START, STOP, ABORT, CLEAR_ALARM; parameter validation; HCACK/CPACK replies | PRD §7.13, §8.6.4 |
| FR-GEM-4 | Events as S6F11 with fixed report definitions (RPTID 3001–3003); host acks with S6F12 | PRD §7.13, §8.6.5 |
| FR-GEM-5 | Alarms as S5F1 on set/clear; host acks S5F2 | PRD §7.13, §8.6.5 |
| FR-GEM-6 | Status variable query S1F3/S1F4 for SVID 4001–4012; unknown SVID → empty item | PRD §7.13, §8.6.6 |
| FR-GEM-9 | S9F1/F3/F5/F7/F9/F11 error messages in the situations §8.6.3 lists | PRD §7.13 |
| FR-MC-2 (SECS source) | SECS/GEM becomes a third command source into the same queue, alongside UI/CLI | PRD §7.5 |
| FR-MC-3 | Machine still works fully with the module disabled/absent — verify this *didn't regress* today | PRD §7.5 |
| Communication/control state finalized | Both state models fully wired to the HSMS session (Day 4) and controller (Day 2) | PRD §6.5 |
| FR-HOST-1..3 | `host_sim`: script language (send/expect/wait-event/sleep/assert/disconnect per §8.7); logs every decoded message, exits non-zero on any failed expectation; scenario library (normal_run, cassette_run, alarm_recovery, link_loss, malformed_frames, bad_commands, wrong_control_state, t3_timeout) | PRD §7.14, §8.7 |
| FR-TOOL-2 | `scripts/interop_secsgem.py` runs the core scenario using the independent open-source `secsgem` library against the machine | PRD §7.14 |
| D-04, D-05 | SECS/GEM as an optional module; machine is the passive HSMS entity | PRD §11.1 |
| 8.6.3–8.6.7 | Message catalogue, remote commands, events/reports/alarms, SVID/ECID tables — this is where they actually get built (Day 4 only built the generic envelope + factory mechanism) | PRD §8.6 |

Not fully in scope today (Tier 2, PRD §7.13 marks S at should): FR-GEM-7
(equipment constants S2F13-16) and FR-GEM-8 (dynamic reports S2F33/35/37) are
should-priority — attempt only if the must-priority items above are done and
green; otherwise defer to Day 6's stretch list, same reasoning as the Day 3
cassette-loop deferral.

## Tests that must exist and pass today

- IT-GEM-1 (establish communications, status query, host command, event and
  alarm round trips)
- IT-CTRL-2 (Start during alarm refused; ClearAlarm allows next Start)
- ST-normal_run, ST-cassette_run, ST-alarm_recovery, ST-link_loss,
  ST-malformed_frames, ST-bad_commands, ST-t3_timeout
- XT-SECSGEM-1 (`secsgem` host performs S1F13, S1F3, S2F41, receives S6F11 and
  S5F1)

## Exit criteria (verbatim from PRD §13, row D5)

> ST scenarios pass; XT-SECSGEM-1 passes; tag v0.2.

## Open question to resolve today (PRD §16.3)

- **Q1**: Can a Windows PC or VM be used, so Tier 3 (C# WPF) is real? Needed by
  D5. This doesn't block today's GEM work — it's a scope decision for later
  (Day 6/7), but the PRD flags it as needed by today so it isn't left hanging
  into the final days.
