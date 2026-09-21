#pragma once

// Thread-safety: BoundedQueue<T> is thread-safe for any number of concurrent
// producers and consumers.
//
// This is the v1 implementation (PRD D-09): a plain mutex and two condition
// variables, one predicate-guarded wait each (CLAUDE.md §6.2 — never wait
// without a predicate, to survive spurious wakeups). A lock-free ring buffer
// can become v2 later without touching any caller, because callers only ever
// see this class's public method signatures (push/pop/close/size), not an
// abstract base class — CLAUDE.md §6.1 keeps virtual dispatch out of hot
// loops, so "behind an interface" here means "behind a stable API", not
// runtime polymorphism.
//
// Every queue in this project must be bounded and have a documented
// back-pressure policy (CLAUDE.md §6.2); BackpressurePolicy is that policy,
// chosen per queue at construction time.

#include <condition_variable>
#include <cstddef>
#include <deque>
#include <mutex>
#include <optional>

namespace ssim::core {

enum class BackpressurePolicy {
    // push() blocks the caller until space is available or the queue is
    // closed. This is the FR-SCN-2 default.
    kBlock,

    // push() never blocks: if the queue is full, the new item is discarded
    // and dropped_count() is incremented. The caller is expected to check
    // the return value of push() and raise its own alarm/event
    // (e.g. QueueOverflow) — the queue itself only counts drops, it does not
    // know about the event bus.
    kDropNewest,
};

template <typename T>
class BoundedQueue {
public:
    BoundedQueue(std::size_t capacity, BackpressurePolicy policy)
        : capacity_(capacity), policy_(policy) {}

    BoundedQueue(const BoundedQueue&) = delete;
    BoundedQueue& operator=(const BoundedQueue&) = delete;

    // Enqueues one item. Under kBlock, waits for space or for close().
    // Under kDropNewest, never waits: returns false and increments
    // dropped_count() if the queue is full. Returns false without enqueuing
    // if the queue is already closed.
    bool push(T item) {
        std::unique_lock lock(mutex_);
        if (closed_) {
            return false;
        }
        if (items_.size() >= capacity_) {
            if (policy_ == BackpressurePolicy::kDropNewest) {
                ++dropped_;
                return false;
            }
            not_full_.wait(lock, [this] { return items_.size() < capacity_ || closed_; });
            if (closed_) {
                return false;
            }
        }
        items_.push_back(std::move(item));
        lock.unlock();
        not_empty_.notify_one();
        return true;
    }

    // Blocks until an item is available or the queue is closed and drained.
    // Returns std::nullopt exactly once there will never be another item.
    std::optional<T> pop() {
        std::unique_lock lock(mutex_);
        not_empty_.wait(lock, [this] { return !items_.empty() || closed_; });
        if (items_.empty()) {
            return std::nullopt;
        }
        T item = std::move(items_.front());
        items_.pop_front();
        lock.unlock();
        not_full_.notify_one();
        return item;
    }

    // Wakes every blocked push()/pop() caller. Further push() calls fail;
    // pop() keeps returning queued items until drained, then nullopt.
    void close() {
        {
            std::lock_guard lock(mutex_);
            closed_ = true;
        }
        not_empty_.notify_all();
        not_full_.notify_all();
    }

    std::size_t size() const {
        std::lock_guard lock(mutex_);
        return items_.size();
    }

    std::size_t dropped_count() const {
        std::lock_guard lock(mutex_);
        return dropped_;
    }

    bool closed() const {
        std::lock_guard lock(mutex_);
        return closed_;
    }

private:
    mutable std::mutex mutex_;
    std::condition_variable not_full_;
    std::condition_variable not_empty_;
    std::deque<T> items_;
    std::size_t capacity_;
    BackpressurePolicy policy_;
    std::size_t dropped_ = 0;
    bool closed_ = false;
};

}  // namespace ssim::core
