# 0001. Networking library: standalone Asio

Date: 2026-09-21 (Day 1)
Status: accepted

## Context

`ssim_secsgem`'s HSMS layer (Day 4) needs TCP sockets and timers. PRD D-03
proposes standalone Asio over Qt Network, so `ssim_core` and the headless
build never depend on Qt (dependency rule, CLAUDE.md §2/§3.2). Open question
Q3 asked to confirm this is actually practical — specifically, that
standalone (non-Boost) Asio is cleanly `FetchContent`-able — before locking
it in, rather than taking the PRD's stated default on faith.

## Decision

Use standalone Asio (header-only, `chriskohlhoff/asio` on GitHub, not
Boost.Asio), pinned to tag `asio-1-30-2`.

## Verification done today

A throwaway CMake project (not part of this repo) declared this exact
`FetchContent` pin, built a trivial program including `<asio.hpp>` with
`ASIO_STANDALONE` defined, and actually bound an `asio::ip::tcp::acceptor`
to a loopback port at runtime. It configured, built and ran cleanly on
macOS/AppleClang.

Linux (gcc) and Windows (MSVC) are **not yet verified** — Asio is not wired
into any real target yet (no networking code exists before Day 4), so
nothing exercises it in the CI matrix today. That verification happens for
real when `ssim_secsgem` is added and CI builds it on all three systems;
if it turns out not to be clean there, this decision gets revisited then,
not assumed.

## Consequences

- `cmake/FetchContentPins.cmake` gets a `FetchContent_Declare` for `asio`
  pinned to `asio-1-30-2`, alongside nlohmann/json — declared now,
  `FetchContent_MakeAvailable`'d and linked into `ssim_secsgem` starting
  Day 4.
- `ASIO_STANDALONE` (no Boost) and `ASIO_NO_DEPRECATED` must be defined
  wherever Asio headers are included.
- Boost.Asio remains an alternative if standalone Asio's Linux/Windows CI
  build turns out not to be clean; not expected, but this document is the
  place that decision gets revisited if it happens.

## Alternatives considered

- **Qt Network**: rejected — would pull Qt into `ssim_core`/headless
  builds, violating the dependency rule and FR-MC-3 (machine must work
  fully with Qt absent).
- **Raw BSD sockets**: rejected — reinventing cross-platform timers and
  async I/O portably (especially IOCP on Windows) is out of scope for a
  portfolio project's time budget.

## Update, 2026-09-24 (Day 4)

Asio is now used by `ssim_secsgem`. It is fetched as the `asio-1-30-2` source
tarball with a SHA-256 pin (`cmake/FetchContentPins.cmake`) instead of a git
clone: same release, but a few megabytes instead of the whole repository, and
a stricter pin. The verification gap noted above (Linux and Windows) is still
open until CI has built the module on those systems.
