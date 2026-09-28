# Third-party licences

This project's own code is under the MIT licence (see `LICENSE`). It depends
on the following third-party libraries, pulled in via pinned CMake
`FetchContent` entries (`cmake/FetchContentPins.cmake`, `tests/CMakeLists.txt`)
or, for Qt, via Homebrew. Each is used exactly as documented below; none of
their source is copied into this repository.

| Library | Version pinned | Licence | Link |
|---|---|---|---|
| Qt 6 (`qtbase`) | Homebrew `qtbase` (arm64) | LGPL v3 | https://www.qt.io/licensing/ |
| Asio (standalone, no Boost) | `asio-1-30-2` | Boost Software License 1.0 | https://think-async.com/Asio/ |
| GoogleTest | `v1.15.2` | BSD-3-Clause | https://github.com/google/googletest |
| nlohmann/json | `v3.11.3` | MIT | https://github.com/nlohmann/json |
| stb (`stb_image_write.h`) | commit `2c980bb59875b0d32144a71867fbdebb2f77cd20` | Public domain / MIT (dual, per the header's own licence block) | https://github.com/nothings/stb |

Notes:

- **Qt** is linked dynamically only (`equipment_qt` links against the shared
  Homebrew `qtbase` libraries, and the release build bundles them with
  `macdeployqt` rather than statically linking) — dynamic linking is what
  makes shipping under the LGPL straightforward without also open-sourcing
  this project's own code.
- **Asio** is used standalone (no Boost dependency), only inside
  `src/secsgem/`; `ssim_core` never includes it (see `docs/architecture.md`).
- **GoogleTest** and **stb** are build/test-time and output-writer
  dependencies respectively; neither ships inside `equipment_cli` or
  `equipment_qt`'s runtime behaviour beyond stb's PNG-writing code.

This list satisfies the project's own licensing rule (PRD NFR-LIC-1: list
third-party licences, checked for permissive-or-LGPL-with-dynamic-linking
terms) by naming each dependency, its exact pinned version and its licence
type with a link to the authoritative text, rather than reproducing full
licence bodies here.
