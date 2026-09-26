#pragma once

// Tiny timing helpers for the micro-benchmarks. No benchmark library is used
// (CLAUDE.md 6.10: ask before adding dependencies), just steady_clock and the
// median of several runs, which is less sensitive to a single noisy run than
// the mean. Benchmarks are only meaningful in a Release build, so every one
// prints the environment and warns loudly when optimisation is off.

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <thread>
#include <vector>

namespace ssim::bench {

using Clock = std::chrono::steady_clock;

inline double seconds_between(Clock::time_point a, Clock::time_point b) {
    return std::chrono::duration<double>(b - a).count();
}

// Runs f() `repeats` times and returns the elapsed seconds of each run.
template <typename F>
std::vector<double> time_runs(int repeats, F&& f) {
    std::vector<double> seconds;
    seconds.reserve(static_cast<std::size_t>(repeats));
    for (int i = 0; i < repeats; ++i) {
        const auto start = Clock::now();
        f();
        seconds.push_back(seconds_between(start, Clock::now()));
    }
    return seconds;
}

inline double median(std::vector<double> values) {
    std::sort(values.begin(), values.end());
    const std::size_t n = values.size();
    return n % 2 == 1 ? values[n / 2] : 0.5 * (values[n / 2 - 1] + values[n / 2]);
}

inline double min_of(const std::vector<double>& values) {
    return *std::min_element(values.begin(), values.end());
}

inline double max_of(const std::vector<double>& values) {
    return *std::max_element(values.begin(), values.end());
}

inline void print_environment(const char* title) {
    std::printf("# %s\n", title);
#if defined(__VERSION__)
    std::printf("# compiler: %s\n", __VERSION__);
#endif
#ifdef NDEBUG
    std::printf("# build: optimised (NDEBUG defined)\n");
#else
    std::printf(
        "# WARNING: NOT an optimised build; these numbers mean nothing. Use "
        "-DCMAKE_BUILD_TYPE=Release.\n");
#endif
    std::printf("# hardware threads: %u\n", std::thread::hardware_concurrency());
}

}  // namespace ssim::bench
