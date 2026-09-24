# Scenario script format (host_sim)

Scripts drive `host_sim`, the stand-in for a factory host (FR-HOST-1/2). One
command per line. `#` starts a comment (unless it is inside a string). Blank
lines are ignored. `$NAME` is replaced by a variable: `--connect HOST:PORT`
defines `$HOST` and `$PORT`, and `--var NAME=VALUE` defines any other.

Run one:

```
host_sim --connect 127.0.0.1:5000 --script scenarios/normal_run.scn
```

Every message sent (`>>`) and received (`<<`) is printed decoded. The run stops
at the first failed step and prints `FAIL file:line: step: reason`. Exit codes:
0 every step passed, 1 a step failed, 2 bad arguments or a script that does not
parse. `--quiet` hides the trace.

## Commands

| Command | What it does |
|---|---|
| `connect HOST:PORT` | Opens a TCP connection. |
| `select [status=N]` | Sends Select.req and waits for Select.rsp. Fails unless its status is N (default 0). |
| `deselect [status=N]` | The same for Deselect. |
| `linktest` | Sends Linktest.req and waits for the reply. |
| `separate` | Sends Separate.req (no reply is expected). |
| `send SxFy [nowait\|W] [device=N] [ITEM]` | Sends a data message. The W-bit is set for odd functions unless `nowait`; `W` forces it. `device=N` overrides the session id (default from `--device`, 0). |
| `expect SxFy [timeout=5s] [PATTERN]` | Waits for a received message with this stream and function, then checks its body against the pattern. |
| `expect-no SxFy [timeout=1s]` | Passes only if no such message arrives in the time. |
| `wait-event CEID [timeout=5s] [FIELD=VALUE ...]` | Waits for an S6F11 with this CEID that also carries each named report field with that value (`wait-event 2005 WAFER_ID=W002`); other events stay queued. Values contain no spaces. Later `assert event.*` refer to it. |
| `wait-alarm ALID set\|clear [timeout=5s]` | Waits for an S5F1 for this alarm being set or cleared; `assert alarm.*` refer to it. |
| `expect-closed [timeout=5s]` | Passes once the machine has closed the connection. |
| `expect-status SVID [timeout=5s] PATTERN` | Asks S1F3 for one status variable, repeatedly, until its value matches the pattern or the time runs out. |
| `assert event.FIELD within CENTER +-TOLERANCE` | Numeric check on a field of the last waited event. |
| `assert event.FIELD == VALUE`, `!=` | Number, `true`/`false`, or a `"quoted string"`. |
| `assert alarm.ALID\|ALCD\|ALTX == VALUE` | The same for the last waited alarm. |
| `auto-ack on\|off` | Whether S6F11 and S5F1 are answered automatically (default on). |
| `sleep 500ms\|2s` | Waits (still reading, so events keep being acknowledged). |
| `raw HEXBYTES` | Sends bytes exactly as given, for malformed input: `raw 00 00 00 05 67`. |
| `disconnect` | Closes the connection. |

Options such as `timeout=` and `device=` come **before** the item or pattern
text on a line. Durations are `500ms`, `2s`, `1.5s`, or a bare number of seconds.

### How messages are matched

Received data messages are queued. `expect`, `wait-event`, `wait-alarm` and
`expect-status` take the **first queued message that matches** and leave the
others, so an event that arrives between a command and its reply cannot disturb
the script. Messages nobody asked for are reported at the end, not as failures.
The runner also answers the machine's Linktest.req.

`assert event.FIELD` looks the field up by name in the report layouts of PRD
8.6.5: report 3001 has `WAFER_ID SLOT STRESS_MPA STRESS_UNC_MPA CURVATURE
FIT_RMS_UM OUT_OF_SPEC`, report 3002 `CONTROL_STATE PROCESS_STATE`, report 3003
`WAFER_ID SLOT NUM_LINES`.

## Item and pattern syntax

```
L[ A"START"  L[ L[ A"WAFER_ID" A"W042" ] ] ]     nested lists, ASCII strings
U4[2005]   U1[0x0A 255]   I2[-3, 4]   F4[1.5]   B[0 0x0A]   BOOLEAN[true]
```

- A list holds its items separated by white space. An array holds numbers
  separated by white space or commas (decimal, `0x` hex, negatives for signed
  types, range checked). Strings are `A"..."` with `\"` and `\\` escapes; `A[]`
  is the empty string.
- Types: `L B BOOLEAN A I1 I2 I4 I8 U1 U2 U4 U8 F4 F8`.
- **Patterns** (after `expect`, `expect-status`) also allow wildcards: `*` any
  item, `L[*]` any list, `A[*]` / `U4[*]` any item of that type (any count).
  Otherwise lists match by exact length and item by item, and a value must have
  the same type and value. A message with no body matches only `*`.
- Wildcards are a syntax error in a `send`.

## Example

```
connect 127.0.0.1:$PORT
select
send   S1F13 L[]
expect S1F14 L[ B[0] L[ A[*] A[*] ] ]
send   S2F41 L[ A"START" L[ L[ A"WAFER_ID" A"W042" ] ] ]
expect S2F42 L[ B[4] L[ L[ A"WAFER_ID" B[0] ] ] ]
wait-event 2004 timeout=10s
wait-event 2005 timeout=60s
assert event.STRESS_MPA within -180 +-4
disconnect
```

## The scenario library (FR-HOST-3)

| Script | What it proves | Machine set up |
|---|---|---|
| `normal_run.scn` | Full happy path: communicate, status, START, events, result within tolerance | Online-Remote |
| `alarm_recovery.scn` | Fault raises alarm 1001, next START refused, CLEAR_ALARM, rerun succeeds | Online-Remote, spike fault on W002 |
| `bad_commands.scn` | Unknown command, missing/illegal/unknown parameters, malformed body: the right HCACK, CPACK or S9F7 | Online-Remote |
| `wrong_control_state.scn` | No host commands in Online-Local or Offline; S1F15/S1F17 behaviour | Online-Local |
| `malformed_frames.scn` | S9F1, F3, F5, F7; broken and oversize frames close the connection; the machine keeps running; data before Select is refused | Online-Remote |
| `link_loss.scn` | Link dropped mid-scan: machine survives, keeps the result, accepts a new host | Online-Remote |
| `t3_timeout.scn` | Host never acknowledges an event: S9F9, and the machine carries on | Online-Remote, `comm.t3_s = 1` |
| `cassette_run.scn` | **Not written yet:** needs the cassette loop (FR-MC-5), deferred | |

The tests in `tests/scenario/scenario_test.cpp` run each script against a real
machine in-process with the machine set up as in the last column. Some scripts
(`link_loss`, `t3_timeout`, and the reconnects in `malformed_frames`) wait in
real time, because they exercise real link behaviour and timers: a
one-second pause matches the requirement that the machine is listening again
within a second (FR-HSMS-7).
