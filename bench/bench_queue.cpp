// BM-QUEUE-1 (PRD 10.2): sample-path queue throughput, mutex queue (v1) versus
// the lock-free ring (v2). Items go from producer thread(s) to one consumer
// through a queue of capacity 1024 with the blocking back-pressure policy, and
// the metric is items per second end to end (median of several runs).
//
//   ./bench_queue            (Release build only)

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <thread>
#include <vector>

#include "bench_common.hpp"
#include "ssim/core/queue.hpp"

namespace {

using ssim::bench::Clock;

// 8-byte and 64-byte payloads: the raw cost of the queue, and a block-sized item.
struct Small {
    std::uint64_t v;
};
struct Block64 {
    std::uint64_t v[8];
};

template <typename Payload>
Payload make_payload(std::uint64_t i) {
    Payload p{};
    if constexpr (sizeof(Payload) == sizeof(std::uint64_t)) {
        p.v = i;
    } else {
        p.v[0] = i;
    }
    return p;
}

template <typename Payload>
std::uint64_t first_word(const Payload& p) {
    if constexpr (sizeof(Payload) == sizeof(std::uint64_t)) {
        return p.v;
    } else {
        return p.v[0];
    }
}

// One run: `producers` threads push total/producers items each, one consumer
// pops all of them. Returns elapsed seconds. Also checks that nothing was lost
// or duplicated, so a fast-but-wrong queue cannot post a good number.
template <typename Queue, typename Payload>
double run_once(std::size_t producers, std::uint64_t total) {
    Queue queue(1024, ssim::core::BackpressurePolicy::kBlock);
    std::atomic<bool> go{false};
    std::atomic<std::uint64_t> received{0};
    std::atomic<std::uint64_t> checksum{0};
    const std::uint64_t per_producer = total / producers;

    std::thread consumer([&] {
        while (!go.load()) {
        }
        std::uint64_t n = 0;
        std::uint64_t sum = 0;
        const std::uint64_t expected = per_producer * producers;
        while (n < expected) {
            auto item = queue.pop();
            if (!item) break;
            sum += first_word(*item);
            ++n;
        }
        received.store(n);
        checksum.store(sum);
    });
    std::vector<std::thread> threads;
    for (std::size_t p = 0; p < producers; ++p) {
        threads.emplace_back([&, p] {
            while (!go.load()) {
            }
            for (std::uint64_t i = 0; i < per_producer; ++i) {
                (void)queue.push(make_payload<Payload>(p * per_producer + i));
            }
        });
    }

    const auto start = Clock::now();
    go.store(true);
    for (auto& t : threads) t.join();
    consumer.join();
    const double seconds = ssim::bench::seconds_between(start, Clock::now());

    // Every value 0..total-1 exactly once: the sum is total*(total-1)/2.
    const std::uint64_t expected_total = per_producer * producers;
    const std::uint64_t expected_sum = expected_total * (expected_total - 1) / 2;
    if (received.load() != expected_total || checksum.load() != expected_sum) {
        std::fprintf(stderr, "INTEGRITY FAILURE: received %llu of %llu, checksum %s\n",
                     static_cast<unsigned long long>(received.load()),
                     static_cast<unsigned long long>(expected_total),
                     checksum.load() == expected_sum ? "ok" : "wrong");
        std::exit(1);
    }
    return seconds;
}

template <typename Queue, typename Payload>
void bench(const char* queue_name, const char* payload_name, std::size_t producers,
           std::uint64_t total) {
    std::vector<double> seconds;
    for (int i = 0; i < 5; ++i) {
        seconds.push_back(run_once<Queue, Payload>(producers, total));
    }
    const double items = static_cast<double>((total / producers) * producers);
    const double med = ssim::bench::median(seconds);
    std::printf("| %s | %s | %zu | %.2f | %.2f | %.2f |\n", queue_name, payload_name, producers,
                items / med / 1e6, items / ssim::bench::max_of(seconds) / 1e6,
                items / ssim::bench::min_of(seconds) / 1e6);
    std::fflush(stdout);
}

}  // namespace

int main() {
    ssim::bench::print_environment(
        "bench_queue: producer(s) -> 1 consumer, capacity 1024, blocking");
    std::printf("| queue | payload | producers | median M items/s | worst | best |\n");
    std::printf("|---|---|---|---|---|---|\n");
    constexpr std::uint64_t kTotal = 2'000'000;
    using V1 = ssim::core::BoundedQueue<Small>;
    using V1Block = ssim::core::BoundedQueue<Block64>;
    for (std::size_t producers : {std::size_t{1}, std::size_t{2}, std::size_t{4}}) {
        bench<V1, Small>("v1 mutex + condvar", "8 B", producers, kTotal);
    }
    bench<V1Block, Block64>("v1 mutex + condvar", "64 B", 1, kTotal);
    return 0;
}
