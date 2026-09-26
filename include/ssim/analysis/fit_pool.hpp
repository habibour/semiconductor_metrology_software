#pragma once

// Thread-safety: FitThreadPool is safe to call fit_lines() on repeatedly
// from a single owning thread (matches the composition root's use: one
// pipeline run at a time). Internally, each worker only ever writes to its
// own job's output slot, so there is no shared mutable state to race.
//
// FR-PRC-9: fans per-line fits out across a thread pool, reusing
// ssim::core::BoundedQueue<std::function<void()>> as the job queue (the
// same primitive Controller's inbox already uses) rather than inventing a
// second queue type. Each job is a pure function of its own line's data, so
// the result is bit-for-bit identical to fitting the lines serially,
// regardless of which worker thread ran which job.

#include <cstddef>
#include <functional>
#include <thread>
#include <vector>

#include "ssim/analysis/types.hpp"
#include "ssim/core/queue.hpp"

namespace ssim::analysis {

class FitThreadPool {
public:
    // n_threads == 0 selects max(1, hardware_concurrency - 1), matching the
    // Day 2 processing-pool default (PRD §6.3).
    explicit FitThreadPool(std::size_t n_threads);
    ~FitThreadPool();

    FitThreadPool(const FitThreadPool&) = delete;
    FitThreadPool& operator=(const FitThreadPool&) = delete;

    // Runs fn(0) .. fn(count - 1) on the workers and blocks until all have
    // returned. fn must write only to its own index's output slot and must not
    // throw (exceptions never cross a thread boundary). Used for the per-line
    // filter stage and, through fit_lines, the per-line fits.
    void for_each_index(std::size_t count, const std::function<void(std::size_t)>& fn);

    // Fits every line, blocks until all are done, and returns results in
    // the same order as filtered_lines.
    std::vector<LineFitResult> fit_lines(const std::vector<FilterResult>& filtered_lines);

private:
    ssim::core::BoundedQueue<std::function<void()>> jobs_;
    std::vector<std::thread> workers_;
};

}  // namespace ssim::analysis
