#include "ssim/analysis/fit_pool.hpp"

#include <algorithm>
#include <condition_variable>
#include <mutex>

#include "ssim/analysis/line_fit.hpp"

namespace ssim::analysis {

namespace {
constexpr std::size_t kJobQueueCapacity = 256;

std::size_t resolve_thread_count(std::size_t n_threads) {
    if (n_threads != 0) {
        return n_threads;
    }
    const unsigned hw = std::thread::hardware_concurrency();
    return std::max<std::size_t>(1, hw > 0 ? hw - 1 : 1);
}
}  // namespace

FitThreadPool::FitThreadPool(std::size_t n_threads)
    : jobs_(kJobQueueCapacity, ssim::core::BackpressurePolicy::kBlock) {
    const std::size_t count = resolve_thread_count(n_threads);
    workers_.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        workers_.emplace_back([this] {
            while (auto job = jobs_.pop()) {
                (*job)();
            }
        });
    }
}

FitThreadPool::~FitThreadPool() {
    jobs_.close();
    for (auto& worker : workers_) {
        worker.join();
    }
}

std::vector<LineFitResult> FitThreadPool::fit_lines(
    const std::vector<FilterResult>& filtered_lines) {
    std::vector<LineFitResult> results(filtered_lines.size());

    std::mutex done_mutex;
    std::condition_variable done_cv;
    std::size_t remaining = filtered_lines.size();

    for (std::size_t i = 0; i < filtered_lines.size(); ++i) {
        jobs_.push([&filtered_lines, &results, &done_mutex, &done_cv, &remaining, i] {
            results[i] = fit_line(filtered_lines[i]);
            std::lock_guard lock(done_mutex);
            --remaining;
            if (remaining == 0) {
                done_cv.notify_one();
            }
        });
    }

    std::unique_lock lock(done_mutex);
    done_cv.wait(lock, [&remaining] { return remaining == 0; });
    return results;
}

}  // namespace ssim::analysis
