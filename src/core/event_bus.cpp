#include "ssim/core/event_bus.hpp"

#include <algorithm>

namespace ssim::core {

namespace {

// The subscriptions whose handlers the current thread is inside of, so that a
// handler which unsubscribes itself is not made to wait for its own call.
thread_local std::vector<const void*> t_executing;

}  // namespace

EventBus::SubscriptionId EventBus::subscribe_erased(std::type_index type, ErasedHandler handler) {
    auto subscriber = std::make_shared<Subscriber>();
    subscriber->handler = std::move(handler);
    std::lock_guard lock(mutex_);
    const SubscriptionId id = next_id_++;
    subscriber->id = id;
    subscribers_by_type_[type].push_back(std::move(subscriber));
    type_by_id_.emplace(id, type);
    return id;
}

void EventBus::unsubscribe(SubscriptionId id) {
    std::shared_ptr<Subscriber> removed;
    {
        std::lock_guard lock(mutex_);
        const auto type_it = type_by_id_.find(id);
        if (type_it == type_by_id_.end()) {
            return;
        }
        auto& subscribers = subscribers_by_type_[type_it->second];
        const auto it = std::find_if(subscribers.begin(), subscribers.end(),
                                     [id](const auto& s) { return s->id == id; });
        if (it != subscribers.end()) {
            removed = *it;
            subscribers.erase(it);
        }
        type_by_id_.erase(type_it);
    }
    if (!removed) {
        return;
    }
    // Outside the bus lock: stop new calls, then wait for the ones in flight.
    std::unique_lock lock(removed->mutex);
    removed->removed = true;
    const bool inside_own_handler =
        std::find(t_executing.begin(), t_executing.end(), removed.get()) != t_executing.end();
    if (!inside_own_handler) {
        removed->idle.wait(lock, [&] { return removed->running == 0; });
    }
}

void EventBus::publish_erased(std::type_index type, const void* event) const {
    std::vector<std::shared_ptr<Subscriber>> subscribers;
    {
        std::lock_guard lock(mutex_);
        const auto it = subscribers_by_type_.find(type);
        if (it == subscribers_by_type_.end()) {
            return;
        }
        subscribers = it->second;
    }
    // The bus lock is released before any subscriber code runs (rule C3).
    for (const auto& subscriber : subscribers) {
        {
            std::lock_guard lock(subscriber->mutex);
            if (subscriber->removed) {
                continue;
            }
            ++subscriber->running;
        }
        t_executing.push_back(subscriber.get());
        // Leave the bookkeeping right even if a handler throws.
        struct Done {
            Subscriber& s;
            ~Done() {
                t_executing.pop_back();
                {
                    std::lock_guard lock(s.mutex);
                    --s.running;
                }
                s.idle.notify_all();
            }
        } done{*subscriber};
        subscriber->handler(event);
    }
}

}  // namespace ssim::core
