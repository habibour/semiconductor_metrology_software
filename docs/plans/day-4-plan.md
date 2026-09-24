# Day 4 plan — SECS-II codec, fuzz tests, HSMS framing and timers

Read `docs/specs/day-4-spec.md` first. First day touching `ssim_secsgem`; create
`include/ssim/secsgem/` and `src/secsgem/{hsms,secs2}/` now (first files land
today; `src/secsgem/gem/` stays uncreated until Day 5).

## 1. SECS-II codec (`src/secsgem/secs2/`)

1. `include/ssim/secsgem/secs2/item.hpp` — a variant/tagged-union type covering
   L, B, BOOLEAN, A, I1/I2/I4/I8, U1/U2/U4/U8, F4, F8 (F4 is the only place
   `float` is allowed per CLAUDE.md §5.1 — everywhere else physics stays
   `double`, but wire values at this boundary are `float` for F4 specifically).
2. `src/secsgem/secs2/codec.cpp` — encode: format byte = (format-code<<2)|len-bytes,
   1–3 length bytes, big-endian payload, per PRD §8.6.2. Decode: strict —
   truncated data, inconsistent lengths, list counts that don't match, nesting
   depth >32, never reads past the buffer end; every failure returns an error
   value, not an exception or UB (FR-S2-2). Parse bytes with explicit
   big-endian helpers + `memcpy` only — no `reinterpret_cast` on a buffer
   (CLAUDE.md §6.1).
   → `tests/unit/secsgem/secs2/codec_roundtrip_test.cpp` (UT-CODEC-1: every type
   at length boundaries 0/1/255/256/65535/65536 bytes).
   → `tests/unit/secsgem/secs2/codec_reject_test.cpp` (UT-CODEC-2: each rejection
   case above, asserting no crash / no UB — run this under ASan locally even
   before Day 6's sanitizer CI job exists).
3. `src/secsgem/secs2/text_dump.cpp` — human-readable dump of any decoded item
   tree, for later log/trace use (FR-S2-4).
4. `src/secsgem/secs2/message_factory.hpp/.cpp` — Factory pattern skeleton keyed
   by (Stream, Function); today it only needs to support building/parsing the
   generic envelope, since the full catalogue (S1F1, S2F41, S6F11, …) is Day 5's
   job — don't pre-build catalogue entries today, just the factory mechanism and
   registration API it'll use.

## 2. HSMS layer (`src/secsgem/hsms/`)

1. `include/ssim/secsgem/hsms/frame.hpp` + `src/secsgem/hsms/frame.cpp` — 4-byte
   length + 10-byte header (session id, header byte 2 [W-bit+stream], header
   byte 3 [function], PType, SType, system bytes) + body, per PRD §8.6.1.
   Streaming decoder that handles partial reads and multiple frames in one
   read buffer; rejects frames over `max_frame_bytes` and closes the connection
   (FR-HSMS-2).
   → `tests/unit/secsgem/hsms/frame_test.cpp` (UT-HSMS-1).
2. `include/ssim/secsgem/hsms/connection_state.hpp` — NOT_CONNECTED /
   NOT_SELECTED / SELECTED state machine (PRD §6.5), reusing the Day 2
   state-machine pattern/style, not the process state machine itself.
3. `src/secsgem/hsms/session.cpp` — Select.req/rsp, Deselect.req/rsp,
   Linktest.req/rsp, Reject.req, Separate.req (FR-HSMS-3); system-byte
   generation + transaction matching, multiple outstanding transactions
   (FR-HSMS-5); one session at a time, second connection refused (FR-HSMS-6);
   passive listener on configurable bind:port, default 127.0.0.1:5000 (FR-HSMS-1)
   — loopback by default per NFR-SEC-1.
4. `src/secsgem/hsms/timers.cpp` — T3, T6, T7, T8 all driven by the injectable
   `Clock` from Day 1 (C8); T7 expiry closes the connection. No real sleeps in
   tests (NFR-TST-1).
   → `tests/unit/secsgem/hsms/timers_test.cpp` (UT-HSMS-2: fake-clock expiry of
   each timer; Linktest request/response behaviour).
5. `src/secsgem/hsms/adapter.cpp` — the Adapter pattern entry point that will
   connect this session layer to the Day 2 command queue / event bus once GEM
   exists (Day 5); today it can be a thin stub that compiles and is unit-tested
   in isolation, not yet wired into `equipment_cli`.

## 3. Build wiring

1. Add `ssim_secsgem` CMake target (Asio + `ssim_core` deps only, confirming the
   Q3 decision from Day 1 actually works as a FetchContent dependency).
2. Gate it behind `SSIM_ENABLE_SECSGEM`; build twice locally — once ON, once
   OFF — and confirm all Days 1–3 tests still pass with it OFF (FR-MC-3).

## 4. Open question Q2

For every layout implemented today (frame header, SType table, item header
format codes), cross-check against the `secsgem` Python library's own docs/source
before trusting the PRD §8.6 numbers. Anything not yet checked gets a
`TODO(verify): <what to check>` comment, not a silent assumption (CLAUDE.md §9
rule 6). Full message-catalogue verification (S1F1 etc.) happens Day 5 when
those messages are actually built — today's cross-check is scoped to frame/item
header structure only.

## Day 4 done-checklist (maps back to day-4-spec.md)

- [x] UT-CODEC-1 and UT-CODEC-2 pass.
- [x] UT-HSMS-1 and UT-HSMS-2 pass.
- [x] `SSIM_ENABLE_SECSGEM=OFF` build still passes every Day 1–3 test.
- [x] Frame/item-header structure cross-checked against `secsgem`; any gaps
      marked `TODO(verify)` (see docs/protocol-notes.md).
- [x] No `reinterpret_cast` on wire buffers anywhere in `secsgem/`.
