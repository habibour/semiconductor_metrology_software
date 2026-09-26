# Micro-benchmarks (BM-QUEUE-1, BM-ANALYSIS-1, NFR-PERF-3). Off by default:
# -DSSIM_BUILD_BENCH=ON with a Release build. Results go in docs/benchmarks.md.
if(NOT CMAKE_BUILD_TYPE STREQUAL "Release")
    message(WARNING "Benchmarks built without CMAKE_BUILD_TYPE=Release; the numbers will mean nothing.")
endif()

find_package(Threads REQUIRED)

add_executable(bench_queue bench_queue.cpp)
target_link_libraries(bench_queue PRIVATE ssim_core Threads::Threads)
ssim_apply_warnings(bench_queue)

add_executable(bench_analysis bench_analysis.cpp)
target_link_libraries(bench_analysis PRIVATE ssim_hw ssim_analysis Threads::Threads)
ssim_apply_warnings(bench_analysis)

if(TARGET ssim_secsgem)
    add_executable(bench_codec bench_codec.cpp)
    target_link_libraries(bench_codec PRIVATE ssim_secsgem Threads::Threads)
    ssim_apply_warnings(bench_codec)
endif()
