// Mutation test (FT-FUZZ-1 groundwork, NFR-REL-1): 100,000 randomly mutated
// encodings must each return a value, never crash or hang. Seeded, so a
// failure is reproducible. Run under the sanitizer jobs in CI for the memory
// safety half of the claim.

#include <gtest/gtest.h>

#include <cstdint>
#include <random>
#include <vector>

#include "ssim/secsgem/secs2/codec.hpp"

namespace ssim::secsgem::secs2 {
namespace {

using Bytes = std::vector<std::uint8_t>;

std::vector<Bytes> seed_corpus() {
    std::vector<Item> items;
    items.push_back(Item::ascii("hello"));
    items.push_back(Item::u4({1, 2, 3}));
    items.push_back(Item::list({}));
    items.push_back(Item::list(
        {Item::u4(std::uint32_t{2005}), Item::ascii("W001"),
         Item::list({Item::f4(-179.4f), Item::boolean(false), Item::binary({1, 2, 3})})}));
    items.push_back(Item::i8({-1, 0, 1}));
    items.push_back(Item::f8({1.5, -2.5}));
    std::vector<Bytes> corpus;
    for (const Item& item : items) {
        corpus.push_back(encode(item).value());
    }
    return corpus;
}

Bytes mutate(Bytes bytes, std::mt19937& rng) {
    const int operations = 1 + static_cast<int>(rng() % 4);
    for (int i = 0; i < operations; ++i) {
        switch (rng() % 5) {
            case 0:  // flip a bit
                if (!bytes.empty())
                    bytes[rng() % bytes.size()] ^= static_cast<std::uint8_t>(1u << (rng() % 8));
                break;
            case 1:  // overwrite a byte
                if (!bytes.empty()) bytes[rng() % bytes.size()] = static_cast<std::uint8_t>(rng());
                break;
            case 2:  // truncate
                if (!bytes.empty()) bytes.resize(rng() % bytes.size());
                break;
            case 3:  // insert a byte
                bytes.insert(
                    bytes.begin() + static_cast<std::ptrdiff_t>(rng() % (bytes.size() + 1)),
                    static_cast<std::uint8_t>(rng()));
                break;
            default:  // tamper with a length-looking byte near the front
                if (bytes.size() > 1)
                    bytes[1 + rng() % std::min<std::size_t>(3, bytes.size() - 1)] =
                        static_cast<std::uint8_t>(rng() % 3 == 0 ? 0xFF : rng());
                break;
        }
    }
    return bytes;
}

TEST(CodecFuzz, HundredThousandMutatedInputsNeverCrash) {
    const std::vector<Bytes> corpus = seed_corpus();
    std::mt19937 rng(20260924);  // fixed seed

    std::size_t accepted = 0;
    for (int i = 0; i < 100000; ++i) {
        const Bytes input = mutate(corpus[rng() % corpus.size()], rng);
        auto decoded = decode_item(ByteSpan(input));
        if (!decoded) {
            continue;
        }
        ++accepted;
        // Anything the decoder accepts must re-encode, and the canonical bytes
        // must be a fixed point (compared as bytes so NaN payloads are fine).
        auto once = encode(decoded.value().item);
        ASSERT_TRUE(once) << "accepted item could not be re-encoded";
        auto reparsed = decode_body(ByteSpan(once.value()));
        ASSERT_TRUE(reparsed);
        auto twice = encode(reparsed.value());
        ASSERT_TRUE(twice);
        ASSERT_EQ(once.value(), twice.value());
    }
    // A mutation test that rejects everything (or accepts everything) proves nothing.
    EXPECT_GT(accepted, 1000u);
    EXPECT_LT(accepted, 100000u);
}

TEST(CodecFuzz, RandomBytesNeverCrash) {
    std::mt19937 rng(7);
    for (int i = 0; i < 20000; ++i) {
        Bytes input(rng() % 64);
        for (auto& b : input) b = static_cast<std::uint8_t>(rng());
        (void)decode_item(ByteSpan(input));
    }
}

}  // namespace
}  // namespace ssim::secsgem::secs2
