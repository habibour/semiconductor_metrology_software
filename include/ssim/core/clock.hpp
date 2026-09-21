#pragma once

#include <chrono>

namespace ssim::core {

// Injectable time source (PRD D-08). Production code takes an IClock&
// (never calls std::chrono::*_clock::now() directly) so timing-dependent
// behaviour is testable with FakeClock instead of real sleeps.
//
// Thread-safety: implementations must be safe to call from any thread.
class IClock {
public:
    virtual ~IClock() = default;

    // Monotonic time: never goes backwards, unaffected by wall-clock
    // adjustments. Used for durations, timers and ordering.
    virtual std::chrono::steady_clock::time_point monotonic_now() const = 0;

    // Wall-clock time: what a human would read off a calendar/clock. Used
    // only for display and log timestamps, never for measuring durations.
    virtual std::chrono::system_clock::time_point wall_now() const = 0;
};

// The real clock. Thread-safe (stateless; both chrono clocks are
// thread-safe to query).
class SystemClock final : public IClock {
public:
    std::chrono::steady_clock::time_point monotonic_now() const override {
        return std::chrono::steady_clock::now();
    }

    std::chrono::system_clock::time_point wall_now() const override {
        return std::chrono::system_clock::now();
    }
};

// A clock that only moves when told to. Used by tests so timer/duration
// logic is deterministic and never needs a real sleep.
//
// Thread-safety: not thread-safe. Intended for single-threaded test setup;
// advance() and the read methods must not be called concurrently.
class FakeClock final : public IClock {
public:
    FakeClock() = default;

    std::chrono::steady_clock::time_point monotonic_now() const override { return mono_; }
    std::chrono::system_clock::time_point wall_now() const override { return wall_; }

    void advance(std::chrono::nanoseconds delta) {
        mono_ += delta;
        wall_ += std::chrono::duration_cast<std::chrono::system_clock::duration>(delta);
    }

private:
    std::chrono::steady_clock::time_point mono_{};
    std::chrono::system_clock::time_point wall_{};
};

}  // namespace ssim::core
