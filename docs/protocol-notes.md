# Protocol notes: HSMS and SECS-II structure

Status: Day 5. Day 4 covered the frame header, SType table, connection states
and item header. Day 5 adds the message-level layouts, checked by the interop
test (see the last section).

## How this was cross-checked (PRD open question Q2)

The SEMI standards are paywalled and PRD 8.6 was written from memory of public
descriptions. On 2026-09-24 the sdist of the open-source Python package
`secsgem` 0.3.0 (PyPI) was downloaded and its `hsms/` and `secs/variables/`
modules were read (`header.py`, `reject_req_header.py`, `select_rsp_header.py`,
`connection_state_machine.py`, `protocol.py`, `variables/base.py` and the
per-type files). Nothing was copied; only facts are recorded here. That is
evidence about what a widely used open-source implementation does, not a
reading of the standard, so "matches secsgem" is the strongest claim made.

## Confirmed against secsgem 0.3.0

| Topic | Result |
|---|---|
| Header layout | 10 bytes, big-endian: session id (2), byte 2 (W-bit `0x80` OR stream), byte 3 (function), PType, SType, system bytes (4). Matches PRD 8.6.1. |
| Frame length | 4-byte big-endian length that counts header plus body. |
| SType values | 0 data, 1 Select.req, 2 Select.rsp, 3 Deselect.req, 4 Deselect.rsp, 5 Linktest.req, 6 Linktest.rsp, 7 Reject.req, 9 Separate.req. Matches PRD 8.6.1. |
| Control-message session id | `0xFFFF` in Select, Deselect, Linktest and Reject messages. |
| Reject.req | Session id `0xFFFF`; **header byte 2 carries the SType of the rejected message and byte 3 carries the reason code**. |
| Reject reason 4 | Used for "entity not selected" (a data message while not selected). |
| Connection states | not connected, connected-not-selected, connected-selected; transitions connect, disconnect (from either connected state), select, deselect, T7 timeout (not-selected to not-connected). Matches PRD 6.5. |
| Item header | Format byte = `(format_code << 2) OR length_bytes`; decode is `code = (b & 0xFC) >> 2`, `length_bytes = b & 3`, then 1 to 3 big-endian length bytes. |
| Length bytes on encode | 1 byte up to 0xFF, 2 bytes up to 0xFFFF, 3 bytes up to 0xFFFFFF; larger is an error. |
| Format codes (octal) | L 00, B 10, BOOLEAN 11, A 20, JIS-8 21, I8 30, I1 31, I2 32, I4 34, F8 40, F4 44, U8 50, U1 51, U2 52, U4 54. All match PRD 8.6.2. |

## Where this project intentionally differs from secsgem

- The strict decoder rejects a length-byte count of 0 (format byte low bits
  `00`). secsgem's decoder does not check this and would read a zero length.
- Rejection of unsupported formats (JIS-8), inconsistent lengths, over-deep
  nesting and list counts larger than the remaining bytes is a project rule
  (FR-S2-2); secsgem is a lenient Python decoder and is not a model for it.

## Not confirmed: marked TODO(verify) in the code

- **Select.rsp status.** secsgem always answers Select with function byte 0 and
  never sets a status, so it cannot confirm the standard's status values. This
  project uses 0 = success, 1 = already selected, and marks the values
  `TODO(verify)` against the standard or a second implementation.
- **Reject reasons other than 4.** This project uses 1 = SType not supported,
  2 = PType not supported, 3 = transaction not open (from memory of public
  descriptions); only 4 is confirmed by secsgem. `TODO(verify)`.
- **Linktest while not selected.** Behaviour is unspecified by the sources read
  (secsgem answers it regardless of selection). This project answers it in
  every connected state. `TODO(verify)`.
- **T7 restart after Deselect.** Whether T7 restarts when the state returns to
  not-selected. This project restarts it. `TODO(verify)`.
- **T8 behaviour.** secsgem's protocol module was not seen to implement an
  inter-character timeout; this project closes the connection when a partial
  frame stalls for T8. `TODO(verify)`.
- **Non-minimal length bytes** (for example a 2-byte length holding a value
  below 256) are accepted on decode and always emitted minimally.
  `TODO(verify)`.
- **BOOLEAN values.** Any non-zero byte is decoded as true; only 0 and 1 are
  emitted. `TODO(verify)`.

## Installability check for Day 5

`pip download secsgem --no-deps --no-binary :all:` succeeded (0.3.0). A full
`pip install` and a working host conversation have not been tried yet; that is
FR-TOOL-2 on Day 5.

## Message layouts: confirmed by the interop test (Day 5)

`scripts/interop_secsgem.py` (XT-SECSGEM-1) runs a real `equipment_cli serve`
and connects to it as a host using `secsgem` 0.3.0, sharing no code with this
project. On 2026-09-24 it passed 15 of 15 checks, and a negative run (no fault
configured, so no alarm) failed with exit code 1, so the checks can fail. What
that confirms, from the library's side:

| Confirmed | How |
|---|---|
| HSMS Select handshake and the machine as the passive entity | the library connected and reached "communicating" |
| S1F13 from the host as `L2{MDLN, SOFTREV}` answered by S1F14 `L2{COMMACK, L2{MDLN, SOFTREV}}` | library's own S1F13 exchange succeeded |
| S1F3 with U-type SVIDs, S1F4 with mixed value types, an unknown SVID as an empty item | values decoded by the library |
| S2F41 with ASCII RCMD and `L2{CPNAME A, CPVAL A}` parameters, S2F42 `L2{HCACK B, L}` | START gave HCACK 4, CLEAR_ALARM HCACK 0 |
| S6F11 `L3{DATAID U4, CEID U4, L[L2{RPTID U4, L[V]}]}` with A, U1, F4 and BOOLEAN values | library decoded events 2004 and 2005 and returned S6F12 |
| S5F1 `L3{ALCD B, ALID U4, ALTX A}` with bit 7 = set | library decoded set and cleared alarms and returned S5F2 |

Still **not** confirmed by any independent source (kept as `TODO(verify)` in the
code): the Select/Deselect status values, Reject reasons other than 4, the
CPACK and ALCD category values, the S9 message bodies (the interop test does not
provoke them), T8 and T7-restart behaviour, and how a real host reacts to
S1F17/S1F15. The library also expects hosts to define reports dynamically
(S2F33/S2F35, FR-GEM-8, not built), so the test declares the machine's fixed
report layouts to it up front.
