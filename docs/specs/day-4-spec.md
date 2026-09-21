# Day 4 spec — SECS-II codec, fuzz tests, HSMS framing and timers

Source of truth: `docs/PRD.md`. This file is a filtered index into it.

PRD §13, row D4: "SECS-II codec, fuzz tests, HSMS framing and timers." Exit:
"UT-CODEC and UT-HSMS pass."

This is the first day that touches `ssim_secsgem`. Per the dependency rule
(CLAUDE.md §2, PRD §6.2), this module depends on `ssim_core` + Asio only — never
on `ssim_hw`, `ssim_analysis` or Qt. The machine must still build and pass all
Days 1–3 tests with `SSIM_ENABLE_SECSGEM=OFF` (FR-MC-3) — flip that option off
once today's work lands and confirm nothing else broke.

## In scope today

| ID / section | What it requires | PRD ref |
|---|---|---|
| FR-S2-1 | Encode/decode item types L, B, BOOLEAN, A, I1/I2/I4/I8, U1/U2/U4/U8, F4, F8, 1–3 length bytes, big-endian; encode∘decode = identity | PRD §7.12, §8.6.2 |
| FR-S2-2 | Strict decoder: reject truncated / inconsistent-length / wrong list-count / nesting >32; never read past buffer; errors as values | PRD §7.12 |
| FR-S2-4 | Human-readable text dump of any message (for logs/traces — used from Day 5 onward) | PRD §7.12 |
| 8.6.2 item header | Format byte = (format code << 2) \| length-byte-count; format codes in octal per the table | PRD §8.6.2 |
| FR-HSMS-1 | Machine is passive: listens on configurable address:port (default 127.0.0.1:5000) | PRD §7.11 |
| FR-HSMS-2 | Framing: 4-byte length + 10-byte header + body; handle partial reads and coalesced messages; reject frames over `max_frame_bytes` (default 1 MiB), close connection | PRD §7.11, §8.6.1 |
| FR-HSMS-3 | Session control: Select.req/rsp, Deselect.req/rsp, Linktest.req/rsp, Reject.req, Separate.req | PRD §7.11 |
| FR-HSMS-4 | Timers T3/T6/T7/T8 (T5 is host-side, not needed yet) on the injectable clock; T7 expiry closes the connection | PRD §7.11 |
| FR-HSMS-5 | Match replies to requests by system bytes; multiple outstanding transactions; unique system-byte generation | PRD §7.11 |
| FR-HSMS-6 | One host session at a time; second connection refused | PRD §7.11 |
| HSMS connection state | NOT_CONNECTED → NOT_SELECTED (T7 running) → SELECTED; Deselect/Separate/timeout/error return toward NOT_CONNECTED | PRD §6.5 |
| C8 | Injectable clock used for every timer; no real sleeps in tests | PRD §6.4 |
| Patterns added today | Adapter (HSMS-to-core adapter), Factory (SECS-II message factory by Stream/Function — full catalogue comes Day 5, but the factory mechanism starts today) | PRD §6.6 |
| Security groundwork | Bind to loopback by default; enforce max frame length and nesting depth (NFR-SEC-1, partial — full validation-at-every-boundary claim matures as GEM lands Day 5) | PRD §6.8, §9 |

Not in scope today: GEM behaviour (states/events/alarms/remote commands —
Day 5), `host_sim`, interop test.

## Tests that must exist and pass today

- UT-CODEC-1: round-trip every item type at length boundaries (0, 1, 255, 256,
  65535, 65536 bytes).
- UT-CODEC-2: truncated / oversized / inconsistent-length / too-deep / wrong-count
  inputs rejected without crash.
- UT-HSMS-1: partial frames, coalesced frames, maximum length, bad header.
- UT-HSMS-2: fake-clock tests of T3/T6/T7/T8 expiry and Linktest behaviour.

## Exit criteria (verbatim from PRD §13, row D4)

> UT-CODEC and UT-HSMS pass.

## Open question to resolve today (PRD §16.3)

- **Q2**: Do the message layouts in §8.6 match the `secsgem` open-source
  library's documentation and behaviour? Needed by D4. Cross-check every layout
  implemented today against that library before treating it as settled — mark
  anything unverified as `TODO(verify): <what to check>` per CLAUDE.md §9 rule 6.
