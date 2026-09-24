// UT-CODEC-1 (PRD 10.2): round-trip every item type at length boundaries.

#include <gtest/gtest.h>

#include <cstdint>
#include <string>
#include <vector>

#include "ssim/secsgem/secs2/codec.hpp"

namespace ssim::secsgem::secs2 {
namespace {

using Bytes = std::vector<std::uint8_t>;

// Hand-computed encodings, independent of the encoder under test.
// Format byte = (format code << 2) | number of length bytes.
TEST(CodecKnownAnswer, AsciiAbc) {
    auto out = encode(Item::ascii("abc"));
    ASSERT_TRUE(out);
    EXPECT_EQ(out.value(), (Bytes{0x41, 0x03, 0x61, 0x62, 0x63}));  // A = octal 20 -> 0x10<<2|1
}

TEST(CodecKnownAnswer, U4SingleValue) {
    auto out = encode(Item::u4(1u));
    ASSERT_TRUE(out);
    EXPECT_EQ(out.value(), (Bytes{0xB1, 0x04, 0x00, 0x00, 0x00, 0x01}));  // U4 = octal 54 -> 0x2C
}

TEST(CodecKnownAnswer, EmptyList) {
    auto out = encode(Item::list({}));
    ASSERT_TRUE(out);
    EXPECT_EQ(out.value(), (Bytes{0x01, 0x00}));
}

TEST(CodecKnownAnswer, BinaryAndBoolean) {
    EXPECT_EQ(encode(Item::binary({0x0A})).value(), (Bytes{0x21, 0x01, 0x0A}));
    EXPECT_EQ(encode(Item::boolean(true)).value(), (Bytes{0x25, 0x01, 0x01}));
}

TEST(CodecKnownAnswer, NestedList) {
    // L[2]{ A"a", U1 7 }
    auto out = encode(Item::list({Item::ascii("a"), Item::u1(std::uint8_t{7})}));
    ASSERT_TRUE(out);
    EXPECT_EQ(out.value(), (Bytes{0x01, 0x02, 0x41, 0x01, 0x61, 0xA5, 0x01, 0x07}));
}

TEST(CodecKnownAnswer, F4OneAndI2MinusTwo) {
    EXPECT_EQ(encode(Item::f4(1.0f)).value(), (Bytes{0x91, 0x04, 0x3F, 0x80, 0x00, 0x00}));
    EXPECT_EQ(encode(Item::i2({-2})).value(), (Bytes{0x69, 0x02, 0xFF, 0xFE}));
}

// ---- boundary round trips ------------------------------------------------

struct Deterministic {
    std::uint32_t state = 12345;
    std::uint32_t next() {
        state = state * 1664525u + 1013904223u;
        return state >> 8;
    }
};

// Builds an item of `format` holding exactly `bytes` data bytes (a multiple of
// the element size).
Item make_item(Format format, std::size_t bytes) {
    Deterministic rng;
    const std::size_t n = bytes / element_size(format);
    switch (format) {
        case Format::kBinary: {
            Bytes v(n);
            for (auto& b : v) b = static_cast<std::uint8_t>(rng.next());
            return Item::binary(v);
        }
        case Format::kBoolean: {
            Bytes v(n);
            for (auto& b : v) b = static_cast<std::uint8_t>(rng.next() & 1u);
            return Item::boolean(v);
        }
        case Format::kAscii: {
            std::string s(n, 'x');
            for (auto& c : s) c = static_cast<char>('a' + rng.next() % 26);
            return Item::ascii(s);
        }
        case Format::kI1: {
            std::vector<std::int8_t> v(n);
            for (auto& x : v) x = static_cast<std::int8_t>(rng.next());
            return Item::i1(v);
        }
        case Format::kI2: {
            std::vector<std::int16_t> v(n);
            for (auto& x : v) x = static_cast<std::int16_t>(rng.next());
            return Item::i2(v);
        }
        case Format::kI4: {
            std::vector<std::int32_t> v(n);
            for (auto& x : v) x = static_cast<std::int32_t>(rng.next());
            return Item::i4(v);
        }
        case Format::kI8: {
            std::vector<std::int64_t> v(n);
            for (auto& x : v) x = (static_cast<std::int64_t>(rng.next()) << 32) ^ rng.next();
            return Item::i8(v);
        }
        case Format::kU1: {
            Bytes v(n);
            for (auto& x : v) x = static_cast<std::uint8_t>(rng.next());
            return Item::u1(v);
        }
        case Format::kU2: {
            std::vector<std::uint16_t> v(n);
            for (auto& x : v) x = static_cast<std::uint16_t>(rng.next());
            return Item::u2(v);
        }
        case Format::kU4: {
            std::vector<std::uint32_t> v(n);
            for (auto& x : v) x = rng.next() * 257u;
            return Item::u4(v);
        }
        case Format::kU8: {
            std::vector<std::uint64_t> v(n);
            for (auto& x : v) x = (static_cast<std::uint64_t>(rng.next()) << 32) | rng.next();
            return Item::u8(v);
        }
        case Format::kF4: {
            std::vector<float> v(n);
            for (std::size_t i = 0; i < n; ++i) v[i] = static_cast<float>(i) * 0.5f - 3.0f;
            return Item::f4(v);
        }
        case Format::kF8: {
            std::vector<double> v(n);
            for (std::size_t i = 0; i < n; ++i) v[i] = static_cast<double>(i) * 0.25 - 7.0;
            return Item::f8(v);
        }
        case Format::kList:
        case Format::kJis8:
            break;
    }
    return Item();
}

std::size_t expected_length_bytes(std::size_t length) {
    return length > 0xFFFF ? 3 : (length > 0xFF ? 2 : 1);
}

void expect_round_trip(const Item& item, std::size_t length_field_value) {
    auto encoded = encode(item);
    ASSERT_TRUE(encoded);
    const Bytes& bytes = encoded.value();
    ASSERT_FALSE(bytes.empty());

    // The header says how many length bytes were used; it must be the minimum.
    EXPECT_EQ(bytes[0] & 0x03u, expected_length_bytes(length_field_value));
    EXPECT_EQ(bytes[0] >> 2, static_cast<unsigned>(item.format()));

    auto decoded = decode_item(ByteSpan(bytes));
    ASSERT_TRUE(decoded) << decoded.error().message;
    EXPECT_EQ(decoded.value().consumed, bytes.size());
    EXPECT_EQ(decoded.value().item, item);

    // Canonical form: re-encoding gives the same bytes.
    auto again = encode(decoded.value().item);
    ASSERT_TRUE(again);
    EXPECT_EQ(again.value(), bytes);
}

TEST(CodecRoundTrip, EveryTypeAtEveryLengthBoundary) {
    const Format formats[] = {Format::kBinary, Format::kBoolean, Format::kAscii, Format::kI1,
                              Format::kI2,     Format::kI4,      Format::kI8,    Format::kU1,
                              Format::kU2,     Format::kU4,      Format::kU8,    Format::kF4,
                              Format::kF8};
    for (Format format : formats) {
        const std::size_t s = element_size(format);
        // 0, 1, 255, 256, 65535, 65536 bytes; for wider elements the nearest
        // whole number of elements at or below the boundary is used (an item
        // of 255 bytes cannot exist for 4-byte elements).
        const std::size_t targets[] = {0, 1, 255, 256, 65535, 65536};
        for (std::size_t target : targets) {
            const std::size_t bytes = target / s * s;
            SCOPED_TRACE(std::string(to_string(format)) + " with " + std::to_string(bytes) +
                         " data bytes");
            expect_round_trip(make_item(format, bytes), bytes);
        }
    }
}

TEST(CodecRoundTrip, ListsAtCountBoundaries) {
    for (std::size_t count : {std::size_t{0}, std::size_t{1}, std::size_t{255}, std::size_t{256},
                              std::size_t{65535}, std::size_t{65536}}) {
        SCOPED_TRACE("list of " + std::to_string(count));
        Item::List items;
        items.reserve(count);
        for (std::size_t i = 0; i < count; ++i) {
            items.push_back(Item::u1(static_cast<std::uint8_t>(i)));
        }
        expect_round_trip(Item::list(std::move(items)), count);
    }
}

TEST(CodecRoundTrip, MixedNestedMessageBody) {
    const Item body =
        Item::list({Item::u4(std::uint32_t{2005}), Item::ascii("W001"),
                    Item::list({Item::f4(-179.4f), Item::boolean(false), Item::binary({1, 2, 3})}),
                    Item::list({})});
    expect_round_trip(body, 4);
}

TEST(CodecRoundTrip, ExtremeNumericValuesSurvive) {
    expect_round_trip(Item::i8({INT64_MIN, -1, 0, 1, INT64_MAX}), 40);
    expect_round_trip(Item::u8({0, UINT64_MAX}), 16);
    expect_round_trip(Item::i1({-128, 127}), 2);
    expect_round_trip(Item::f8({1e300, -1e-300, 0.0}), 24);
}

TEST(CodecRoundTrip, BooleanIsEmittedAsZeroOrOne) {
    auto out = encode(Item::boolean({0, 1, 7, 255}));
    ASSERT_TRUE(out);
    EXPECT_EQ(out.value(), (Bytes{0x25, 0x04, 0x00, 0x01, 0x01, 0x01}));
}

TEST(CodecRoundTrip, LengthTwoFiftySixUsesTwoLengthBytes) {
    auto out = encode(Item::ascii(std::string(256, 'q')));
    ASSERT_TRUE(out);
    EXPECT_EQ(out.value()[0], 0x42);  // A, 2 length bytes
    EXPECT_EQ(out.value()[1], 0x01);
    EXPECT_EQ(out.value()[2], 0x00);
}

TEST(CodecRoundTrip, DecodeItemLeavesTrailingBytesAlone) {
    Bytes bytes = encode(Item::ascii("hi")).value();
    const std::size_t item_size = bytes.size();
    bytes.push_back(0xEE);
    auto decoded = decode_item(ByteSpan(bytes));
    ASSERT_TRUE(decoded);
    EXPECT_EQ(decoded.value().consumed, item_size);
}

}  // namespace
}  // namespace ssim::secsgem::secs2
