#include "ssim/core/alarms.hpp"

#include <vector>

#include "ssim/core/events.hpp"

namespace ssim::core {

const char* to_string(AlarmId id) {
    switch (id) {
        case AlarmId::kSensorSpikeRateHigh:
            return "SensorSpikeRateHigh";
        case AlarmId::kSensorDropout:
            return "SensorDropout";
        case AlarmId::kFitQualityPoor:
            return "FitQualityPoor";
        case AlarmId::kScanStall:
            return "ScanStall";
        case AlarmId::kProcessingTimeout:
            return "ProcessingTimeout";
        case AlarmId::kQueueOverflow:
            return "QueueOverflow";
        case AlarmId::kStressImplausible:
            return "StressImplausible";
    }
    return "Unknown";
}

void AlarmManager::set(AlarmId id, std::string reason) {
    bool newly_set = false;
    {
        std::lock_guard lock(mutex_);
        const int key = static_cast<int>(id);
        if (active_.find(key) == active_.end()) {
            active_.emplace(key, reason);
            newly_set = true;
        }
    }
    if (newly_set) {
        bus_.publish(AlarmSet{static_cast<int>(id), to_string(id), reason});
    }
}

void AlarmManager::clear(AlarmId id) {
    bool was_active = false;
    {
        std::lock_guard lock(mutex_);
        was_active = active_.erase(static_cast<int>(id)) > 0;
    }
    if (was_active) {
        bus_.publish(AlarmCleared{static_cast<int>(id), to_string(id)});
    }
}

void AlarmManager::clear_all() {
    std::vector<int> cleared_ids;
    {
        std::lock_guard lock(mutex_);
        cleared_ids.reserve(active_.size());
        for (const auto& [id, reason] : active_) {
            cleared_ids.push_back(id);
        }
        active_.clear();
    }
    for (int id : cleared_ids) {
        bus_.publish(AlarmCleared{id, to_string(static_cast<AlarmId>(id))});
    }
}

bool AlarmManager::any_active() const {
    std::lock_guard lock(mutex_);
    return !active_.empty();
}

std::size_t AlarmManager::active_count() const {
    std::lock_guard lock(mutex_);
    return active_.size();
}

bool AlarmManager::is_active(AlarmId id) const {
    std::lock_guard lock(mutex_);
    return active_.find(static_cast<int>(id)) != active_.end();
}

}  // namespace ssim::core
