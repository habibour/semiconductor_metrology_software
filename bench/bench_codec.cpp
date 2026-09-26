// NFR-PERF-3 (PRD 9): SECS-II encode and decode rate on small messages, one
// thread. The message is the largest the machine sends often: a wafer-result
// event (S6F11 body) with a report of seven values.
//
//   ./bench_codec            (Release build only)

#include <cstdint>
#include <cstdio>
#include <vector>

#include "bench_common.hpp"
#include "ssim/secsgem/secs2/codec.hpp"

namespace {

using ssim::secsgem::ByteSpan;
using ssim::secsgem::secs2::Item;

Item event_body() {
    return Item::list(
        {Item::u4(std::uint32_t{7}), Item::u4(std::uint32_t{2005}),
         Item::list({Item::list({Item::u4(std::uint32_t{3001}),
                                 Item::list({Item::ascii("W042"), Item::u1(std::uint8_t{1}),
                                             Item::f4(-179.4f), Item::f4(0.6f), Item::f4(-0.008f),
                                             Item::f4(0.5f), Item::boolean(false)})})})});
}

}  // namespace

int main() {
    ssim::bench::print_environment("bench_codec: S6F11-style body, one thread");
    const Item body = event_body();
    const auto bytes = ssim::secsgem::secs2::encode(body).value();
    std::printf("# body: %zu bytes encoded\n\n", bytes.size());

    constexpr int kIterations = 400'000;
    volatile std::size_t sink = 0;  // keeps the optimiser from removing the work

    const auto encode_runs = ssim::bench::time_runs(9, [&] {
        for (int i = 0; i < kIterations; ++i) {
            sink = sink + ssim::secsgem::secs2::encode(body).value().size();
        }
    });
    const auto decode_runs = ssim::bench::time_runs(9, [&] {
        for (int i = 0; i < kIterations; ++i) {
            sink = sink + ssim::secsgem::secs2::decode_body(ByteSpan(bytes)).value().count();
        }
    });
    const auto round_trip_runs = ssim::bench::time_runs(9, [&] {
        for (int i = 0; i < kIterations; ++i) {
            const auto encoded = ssim::secsgem::secs2::encode(body).value();
            sink = sink + ssim::secsgem::secs2::decode_body(ByteSpan(encoded)).value().count();
        }
    });

    std::printf("| operation | median k ops/s | worst | best |\n|---|---|---|---|\n");
    auto row = [&](const char* name, const std::vector<double>& runs) {
        std::printf("| %s | %.0f | %.0f | %.0f |\n", name,
                    kIterations / ssim::bench::median(runs) / 1e3,
                    kIterations / ssim::bench::max_of(runs) / 1e3,
                    kIterations / ssim::bench::min_of(runs) / 1e3);
    };
    row("encode", encode_runs);
    row("decode", decode_runs);
    row("encode + decode", round_trip_runs);
    return 0;
}
