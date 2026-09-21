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

# Declared but not yet made available: no networking code exists before Day
# 4 (ssim_secsgem), so nothing should force this download today. Q3 is
# resolved (docs/decisions/0001-networking-library.md) — standalone Asio,
# pinned here — but ssim_secsgem's own CMakeLists.txt is what will call
# FetchContent_MakeAvailable(asio) once there is code to link it into.
FetchContent_Declare(
    asio
    GIT_REPOSITORY https://github.com/chriskohlhoff/asio.git
    GIT_TAG asio-1-30-2
)
