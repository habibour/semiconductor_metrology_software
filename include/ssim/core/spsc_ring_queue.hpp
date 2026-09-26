#pragma once

// Thread-safety: SPSC. Exactly one thread may call push() (the producer) and
// exactly one thread may call pop() (the consumer) at any time; they may be
// different threads over the queue's life if the hand-over is synchronised by
// the caller (for example a thread join). close(), size(), dropped_count() and
// closed() may be called from any thread.
//
// This is the v2 sample-path queue (PRD D-09, SM3): a bounded ring buffer whose
// fast path takes no lock. It has the same public API and semantics as
// BoundedQueue (v1, mutex and condition variable), so a caller changes one type
// name. Differences to know about:
//   * one producer and one consumer only (BoundedQueue allows many);
//   * close() should come from the producer or after the producer has stopped:
//     an item pushed while another thread closes the queue may be discarded even
//     though push() returned true. The sample path calls close() at shutdown,
//     where that is harmless.
//
// Fast path: an item moves from producer to consumer through a slot in a ring
// with two monotonically increasing indices, head_ (written by the producer)
// and tail_ (written by the consumer), each cached by the other side so the
// shared cache line is read only when the ring looks full or empty.
//
// Slow path: when the ring is full (kBlock) or empty, a side spins briefly and
// then parks on a mutex and condition variable, so an idle machine does not burn
// CPU. The other side wakes it only if a "waiting" flag is set, which keeps the
// mutex off the fast path. The flag and index accesses that form that handshake
// are sequentially consistent (see the comments at each one); the pure data
// hand-over uses acquire and release.

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>
#include <utility>

#include "ssim/core/queue.hpp"  // BackpressurePolicy

namespace ssim::core {

template <typename T>
class SpscRingQueue {
public:
    // capacity is the most items held at once (at least 1).
    SpscRingQueue(std::size_t capacity, BackpressurePolicy policy)
        : capacity_(std::max<std::size_t>(1, capacity)),
          policy_(policy),
          mask_(round_up_to_power_of_two(capacity_) - 1),
          slots_(std::make_unique<std::optional<T>[]>(mask_ + 1)) {}

    SpscRingQueue(const SpscRingQueue&) = delete;
    SpscRingQueue& operator=(const SpscRingQueue&) = delete;

    // Enqueues one item (producer thread only). Under kBlock, waits for space or
    // for close(). Under kDropNewest, never waits: returns false and counts a
    // drop if the ring is full. Returns false without enqueuing if closed.
    bool push(T item) {
        if (closed_.load()) {
            return false;
        }
        if (try_push(item)) {
            wake_consumer_if_waiting();
            return true;
        }
        if (policy_ == BackpressurePolicy::kDropNewest) {
            // Relaxed: a statistics counter, it orders nothing else.
            dropped_.fetch_add(1, std::memory_order_relaxed);
            return false;
        }
        return push_blocking(item);
    }

    // Blocks until an item is available or the queue is closed and drained
    // (consumer thread only). Returns std::nullopt exactly once there will never
    // be another item.
    std::optional<T> pop() {
        for (int spin = 0; spin < kSpinTries; ++spin) {
            if (auto item = try_pop()) {
                wake_producer_if_waiting();
                return item;
            }
            std::this_thread::yield();
        }
        return pop_blocking();
    }

    // Wakes every blocked push()/pop() caller. Further push() calls fail;
    // pop() keeps returning queued items until drained, then nullopt.
    void close() {
        closed_.store(true);
        // Taking and releasing the mutex orders this close after any waiter that
        // has already checked the predicate and is about to sleep: that waiter
        // holds the mutex until wait() releases it, so it is either in wait()
        // (and gets the notification) or has not checked yet (and sees closed_).
        {
            std::lock_guard lock(mutex_);
        }
        not_empty_.notify_all();
        not_full_.notify_all();
    }

    // Approximate when called from a thread other than the producer or consumer.
    std::size_t size() const {
        // tail first, then head: head only grows, so the difference is never
        // negative; a concurrent push can make it briefly exceed capacity.
        const std::size_t tail = tail_.load(std::memory_order_acquire);
        const std::size_t head = head_.load(std::memory_order_acquire);
        return std::min(head - tail, capacity_);
    }

    std::size_t dropped_count() const { return dropped_.load(std::memory_order_relaxed); }

    bool closed() const { return closed_.load(); }

private:
    static constexpr int kSpinTries = 64;
    // Two cache lines on Apple silicon; also fine (just larger) elsewhere.
    static constexpr std::size_t kCacheLine = 128;

    static std::size_t round_up_to_power_of_two(std::size_t n) {
        std::size_t p = 1;
        while (p < n) {
            p <<= 1;
        }
        return p;
    }

    // Producer: places the item if there is room. The item is moved from only on
    // success.
    bool try_push(T& item) {
        // Relaxed: head_ is written only by this thread.
        const std::size_t head = head_.load(std::memory_order_relaxed);
        if (head - cached_tail_ >= capacity_) {
            // Acquire: pairs with the consumer's store to tail_. Once we see the
            // new tail we also see that the consumer finished emptying the slot,
            // so reusing it does not race with the consumer's reset().
            cached_tail_ = tail_.load(std::memory_order_acquire);
            if (head - cached_tail_ >= capacity_) {
                return false;
            }
        }
        slots_[head & mask_].emplace(std::move(item));
        // Sequentially consistent, not just release. Release makes the slot
        // contents visible to a consumer that loads head_ with acquire. The
        // seq_cst ordering is additionally needed because a parked consumer only
        // learns of this item through consumer_waiting_, which we read next in
        // wake_consumer_if_waiting(): the store to head_ must not be reordered
        // after that load, or a consumer that just set its flag and re-checked
        // an empty ring could sleep forever (a lost wake-up).
        head_.store(head + 1);
        return true;
    }

    // Consumer: takes the oldest item if there is one.
    std::optional<T> try_pop() {
        // Relaxed: tail_ is written only by this thread.
        const std::size_t tail = tail_.load(std::memory_order_relaxed);
        if (tail == cached_head_) {
            // Acquire: pairs with the producer's store to head_, so the slot's
            // contents are visible before we read them.
            cached_head_ = head_.load(std::memory_order_acquire);
            if (tail == cached_head_) {
                return std::nullopt;
            }
        }
        std::optional<T>& slot = slots_[tail & mask_];
        std::optional<T> item(std::move(*slot));
        slot.reset();
        // Sequentially consistent for the same reason as the store to head_, in
        // mirror image: a producer parked on a full ring learns of the space
        // through producer_waiting_, read next in wake_producer_if_waiting().
        // It also releases the slot back to the producer (see try_push).
        tail_.store(tail + 1);
        return item;
    }

    bool push_blocking(T& item) {
        for (int spin = 0; spin < kSpinTries; ++spin) {
            if (closed_.load()) {
                return false;
            }
            if (try_push(item)) {
                wake_consumer_if_waiting();
                return true;
            }
            std::this_thread::yield();
        }
        std::unique_lock lock(mutex_);
        for (;;) {
            // Set the flag, then re-check under the same lock the waker takes:
            // either the predicate below sees the space, or the consumer sees
            // this flag and, after we release the lock in wait(), notifies us.
            producer_waiting_.store(true);
            not_full_.wait(lock, [this] { return closed_.load() || has_space(); });
            producer_waiting_.store(false);
            if (closed_.load()) {
                return false;
            }
            if (try_push(item)) {
                lock.unlock();
                wake_consumer_if_waiting();
                return true;
            }
        }
    }

    std::optional<T> pop_blocking() {
        std::unique_lock lock(mutex_);
        for (;;) {
            // Read closed_ before looking at the ring: anything pushed before
            // the close is then guaranteed to be seen, so "closed and empty"
            // really means no further item.
            const bool was_closed = closed_.load();
            if (auto item = try_pop()) {
                lock.unlock();
                wake_producer_if_waiting();
                return item;
            }
            if (was_closed) {
                return std::nullopt;
            }
            consumer_waiting_.store(true);
            not_empty_.wait(lock, [this] { return closed_.load() || has_item(); });
            consumer_waiting_.store(false);
        }
    }

    // Predicate helpers for the waits. They read the other side's index with
    // sequentially consistent loads so they take part in the handshake above.
    bool has_space() const { return head_.load() - tail_.load() < capacity_; }
    bool has_item() const { return head_.load() != tail_.load(); }

    void wake_consumer_if_waiting() {
        if (consumer_waiting_.load()) {
            {
                std::lock_guard lock(mutex_);
            }
            not_empty_.notify_one();
        }
    }

    void wake_producer_if_waiting() {
        if (producer_waiting_.load()) {
            {
                std::lock_guard lock(mutex_);
            }
            not_full_.notify_one();
        }
    }

    const std::size_t capacity_;
    const BackpressurePolicy policy_;
    const std::size_t mask_;
    std::unique_ptr<std::optional<T>[]> slots_;

    // Each group sits on its own cache line so the producer and consumer do not
    // invalidate each other's line on every operation (false sharing).
    alignas(kCacheLine) std::atomic<std::size_t> head_{0};  // next slot to fill
    std::size_t cached_tail_ = 0;                           // producer's copy of tail_
    alignas(kCacheLine) std::atomic<std::size_t> tail_{0};  // next slot to empty
    std::size_t cached_head_ = 0;                           // consumer's copy of head_
    alignas(kCacheLine) std::atomic<bool> producer_waiting_{false};
    std::atomic<bool> consumer_waiting_{false};
    std::atomic<bool> closed_{false};
    std::atomic<std::size_t> dropped_{0};

    std::mutex mutex_;
    std::condition_variable not_full_;
    std::condition_variable not_empty_;
};

}  // namespace ssim::core
