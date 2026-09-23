#pragma once

// Thread-safety: Thread-safe. set()/clear() may be called from any thread
// that detects a fault condition (the scan thread, the processing
// orchestrator, the controller thread) — this is the one place in the
// project a mutex guards cross-cutting state written by more than one
// producer thread. The mutex is never held while calling into the
// EventBus (CLAUDE.md §6.2 rule C3: no lock across a callback).
//
// FR-ALM-1/2, PRD §8.6.5: the seven ALIDs raised during Day 2 scope
// (1008 InternalError is inherent, not raised through this catalogue).
// set()/clear() are idempotent: a repeated set() for an already-active
// alarm, or clear() for an already-inactive one, does nothing and
// publishes nothing — each set/clear transition is announced exactly once.

#include <mutex>
#include <string>
#include <unordered_map>

#include "ssim/core/event_bus.hpp"

namespace ssim::core {

enum class AlarmId : int {
    kSensorSpikeRateHigh = 1001,
    kSensorDropout = 1002,
    kFitQualityPoor = 1003,
    kScanStall = 1004,
    kProcessingTimeout = 1005,
    kQueueOverflow = 1006,
    kStressImplausible = 1007,
};

const char* to_string(AlarmId id);

class AlarmManager {
public:
    explicit AlarmManager(EventBus& bus) : bus_(bus) {}

    AlarmManager(const AlarmManager&) = delete;
    AlarmManager& operator=(const AlarmManager&) = delete;

    void set(AlarmId id, std::string reason);
    void clear(AlarmId id);

    // Day 2 simplification: the process state machine has one aggregate
    // Alarm state, not one per ALID, so clearing it (via a ClearAlarm
    // command) clears every currently-active alarm at once.
    void clear_all();

    bool any_active() const;
    bool is_active(AlarmId id) const;

private:
    mutable std::mutex mutex_;
    std::unordered_map<int, std::string> active_;  // alid -> reason
    EventBus& bus_;
};

}  // namespace ssim::core
