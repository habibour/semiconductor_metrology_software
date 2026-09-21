cmake_minimum_required(VERSION 3.21)
project(ssim LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Tier-1-only build by default: neither Qt nor SECS/GEM exists yet.
option(SSIM_ENABLE_SECSGEM "Build the SECS/GEM module (ssim_secsgem, host_sim)" OFF)
option(SSIM_BUILD_QT "Build the Qt operator panel (equipment_qt)" OFF)
option(SSIM_BUILD_TESTS "Build unit/integration/scenario tests" ON)
option(SSIM_BUILD_BENCH "Build micro-benchmarks (Release only)" OFF)
set(SSIM_SANITIZER "" CACHE STRING "Sanitizer to build with: address, thread, undefined, or empty for none")
set_property(CACHE SSIM_SANITIZER PROPERTY STRINGS "" "address" "thread" "undefined")

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(Warnings)
include(Sanitizers)

# nlohmann_json is needed by ssim_core itself (Config), independent of
# whether tests are built.
include(FetchContentPins)

add_subdirectory(src/core)
add_subdirectory(src/hw)

if(SSIM_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
