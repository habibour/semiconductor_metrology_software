#pragma once

// Thread-safety: Not thread-safe; owned by the session.
//
// FR-HSMS-4 / C8: a timer is only a deadline on the injected clock's time
// line. It never reads a clock and never sleeps, so tests move time by
// advancing a FakeClock and calling the session's on_tick().

#include <chrono>
#include <optional>

namespace ssim::secsgem::hsms {

using TimePoint = std::chrono::steady_clock::time_point;
using Duration = std::chrono::milliseconds;

class Timer {
public:
    void start(TimePoint now, Duration duration) { deadline_ = now + duration; }
    void stop() { deadline_.reset(); }

    bool running() const { return deadline_.has_value(); }

    // True once now has reached the deadline. Stays true until stopped or
    // restarted, so a missed tick cannot lose an expiry.
    bool expired(TimePoint now) const { return deadline_.has_value() && now >= *deadline_; }

private:
    std::optional<TimePoint> deadline_;
};

}  // namespace ssim::secsgem::hsms
