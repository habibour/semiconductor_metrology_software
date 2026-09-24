# Pinned third-party dependencies (PRD §11). GoogleTest is pinned separately
# in tests/CMakeLists.txt, since it is only needed when SSIM_BUILD_TESTS is
# on.
include(FetchContent)

# Needed unconditionally: ssim_core itself uses nlohmann/json (Config).
FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.11.3
)
FetchContent_MakeAvailable(nlohmann_json)

# Standalone Asio (docs/decisions/0001-networking-library.md). Fetched as the
# tag's source tarball and pinned by SHA-256: a full git clone of this
# repository is large and slow, and a hash pin is stricter than a tag. Made
# available by src/secsgem/CMakeLists.txt, the only place that needs it.
FetchContent_Declare(
    asio
    URL https://github.com/chriskohlhoff/asio/archive/refs/tags/asio-1-30-2.tar.gz
    URL_HASH SHA256=755bd7f85a4b269c67ae0ea254907c078d408cce8e1a352ad2ed664d233780e8
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)

# Needed by ssim_analysis's PNG writer (FR-OUT-3). stb is header-only and
# untagged (no SemVer releases), so this pins an exact commit instead of a
# tag; it has no CMakeLists.txt, so FetchContent_MakeAvailable only
# populates it (see CMake docs: add_subdirectory only runs when the
# populated source has a CMakeLists.txt) — src/analysis/CMakeLists.txt adds
# ${stb_SOURCE_DIR} as an include directory directly.
FetchContent_Declare(
    stb
    GIT_REPOSITORY https://github.com/nothings/stb.git
    GIT_TAG 2c980bb59875b0d32144a71867fbdebb2f77cd20
)
FetchContent_MakeAvailable(stb)
