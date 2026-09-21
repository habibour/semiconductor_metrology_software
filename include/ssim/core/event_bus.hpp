#pragma once

// Thread-safety: Thread-safe. subscribe(), unsubscribe() and publish() may
// be called concurrently from any thread.
//
// Observer pattern (CLAUDE.md §6.6): the bus does not know ahead of time
// which event types exist — publishers and subscribers agree on an event
// type (a plain struct) without either one depending on the other. Per
// CLAUDE.md §6.2 rule C3, the bus never holds its lock while invoking a
// subscriber's handler: it copies the relevant handler list under the lock,
// releases it, then calls the handlers.

#include <cstdint>
#include <functional>
#include <mutex>
#include <typeindex>
#include <unordered_map>
#include <vector>

namespace ssim::core {

class EventBus {
public:
    using SubscriptionId = std::uint64_t;

    EventBus() = default;
    EventBus(const EventBus&) = delete;
    EventBus& operator=(const EventBus&) = delete;

    template <typename EventT>
    SubscriptionId subscribe(std::function<void(const EventT&)> handler) {
        auto erased = [handler = std::move(handler)](const void* event) {
            handler(*static_cast<const EventT*>(event));
        };
        return subscribe_erased(std::type_index(typeid(EventT)), std::move(erased));
    }

    void unsubscribe(SubscriptionId id);

    template <typename EventT>
    void publish(const EventT& event) const {
        publish_erased(std::type_index(typeid(EventT)), &event);
    }

private:
    using ErasedHandler = std::function<void(const void*)>;

    SubscriptionId subscribe_erased(std::type_index type, ErasedHandler handler);
    void publish_erased(std::type_index type, const void* event) const;

    struct Subscriber {
        SubscriptionId id;
        ErasedHandler handler;
    };

    mutable std::mutex mutex_;
    std::unordered_map<std::type_index, std::vector<Subscriber>> subscribers_by_type_;
    std::unordered_map<SubscriptionId, std::type_index> type_by_id_;
    SubscriptionId next_id_ = 1;
};

}  // namespace ssim::core
