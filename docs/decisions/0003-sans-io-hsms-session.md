# 0003. HSMS session as a sans-I/O state machine

Date: 2026-09-24 (Day 4)
Status: accepted

## Context

HSMS has four timers (T3, T6, T7, T8), a three-state connection model,
several control messages and transaction matching. The requirement (C8,
NFR-TST-1) is that timers use the injectable clock and that tests never
sleep. Putting the protocol inside socket callbacks would make every timer
test depend on real time and real sockets, and would tie the logic to Asio.

## Decision

Split the layer in two:

- `Session` (pure): the caller passes the current time into every call
  (`on_connect`, `on_bytes`, `on_tick`, `send_request`, ...) and gets back a
  `SessionOutput` (frames to send, data to deliver, requests whose T3
  expired, a close flag). It owns no socket, clock or lock. `FrameDecoder`,
  `ConnectionStateMachine` and `Timer` are equally pure.
- `HsmsServer` (thin): one `hsms_io` thread running an Asio `io_context`. It
  reads bytes into the session, writes the frames it returns, closes the
  socket when told to, and calls `on_tick` from a steady timer about every
  20 ms. It reads time from the injected `IClock`. Everything on that thread
  is single-threaded, so no locks. Asio is included only in
  `src/secsgem/hsms/server.cpp`, behind a pimpl.
- The seam upward is `IHsmsHandler` (Adapter pattern). Day 5's GEM module
  implements it and forwards into the core command queue. An empty
  `adapter.cpp` stub was not written: it would be dead code.

## Consequences

- Timer behaviour is tested exactly, one millisecond either side of each
  deadline, without waiting. The server tests only need to prove wiring, and
  advance an atomic test clock instead of sleeping.
- `FakeClock` is documented as not thread-safe, so tests that give a clock to
  the server use a small atomic one. The `IClock` contract already says
  implementations must be callable from any thread.
- Timer expiry latency in production is up to one tick (about 20 ms), which is
  negligible against timers of seconds.
- T3 expiry is reported, not acted on: the connection stays up. What to do
  about it (S9F9, event handling) is Day 5 GEM behaviour.

## Alternatives considered

- **Protocol logic inside Asio handlers with `steady_timer` per timer**:
  rejected, because the tests would need real waits or a fake Asio executor.
- **A general timer-wheel service**: more machinery than four deadlines need.
