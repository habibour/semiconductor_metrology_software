#include "ssim/core/event_bus.hpp"

#include <algorithm>

namespace ssim::core {

EventBus::SubscriptionId EventBus::subscribe_erased(std::type_index type, ErasedHandler handler) {
    std::lock_guard lock(mutex_);
    const SubscriptionId id = next_id_++;
    subscribers_by_type_[type].push_back(Subscriber{id, std::move(handler)});
    type_by_id_.emplace(id, type);
    return id;
}

void EventBus::unsubscribe(SubscriptionId id) {
    std::lock_guard lock(mutex_);
    const auto type_it = type_by_id_.find(id);
    if (type_it == type_by_id_.end()) {
        return;
    }
    auto& subscribers = subscribers_by_type_[type_it->second];
    subscribers.erase(std::remove_if(subscribers.begin(), subscribers.end(),
                                     [id](const Subscriber& s) { return s.id == id; }),
                      subscribers.end());
    type_by_id_.erase(type_it);
}

void EventBus::publish_erased(std::type_index type, const void* event) const {
    std::vector<ErasedHandler> handlers;
    {
        std::lock_guard lock(mutex_);
        const auto it = subscribers_by_type_.find(type);
        if (it == subscribers_by_type_.end()) {
            return;
        }
        handlers.reserve(it->second.size());
        for (const auto& subscriber : it->second) {
            handlers.push_back(subscriber.handler);
        }
    }
    // Lock is released before any subscriber code runs (rule C3).
    for (const auto& handler : handlers) {
        handler(event);
    }
}

}  // namespace ssim::core
