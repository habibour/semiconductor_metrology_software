// UT-CODEC-2 (PRD 10.2): truncated, oversized, inconsistent-length, too-deep
// and wrong-count inputs are rejected without crashing, with stable codes.

#include <gtest/gtest.h>

#include <cstdint>
#include <vector>

#include "ssim/secsgem/secs2/codec.hpp"

namespace ssim::secsgem::secs2 {
namespace {

using Bytes = std::vector<std::uint8_t>;

int code_of(const Bytes& bytes) {
    auto r = decode_body(ByteSpan(bytes));
    return r ? 0 : r.error().code;
}

TEST(CodecReject, EmptyInputIsTruncated) { EXPECT_EQ(code_of({}), kErrTruncated); }

TEST(CodecReject, HeaderWithoutItsLengthByteIsTruncated) {
    EXPECT_EQ(code_of({0x41}), kErrTruncated);        // A, wants 1 length byte
    EXPECT_EQ(code_of({0x43, 0x00}), kErrTruncated);  // A, wants 3 length bytes, has 1
}

TEST(CodecReject, ZeroLengthBytesIsRejected) {
    EXPECT_EQ(code_of({0x40}), kErrBadLengthBytes);  // A with low bits 00
    EXPECT_EQ(code_of({0x00}), kErrBadLengthBytes);  // L with low bits 00
}

TEST(CodecReject, UnknownAndUnsupportedFormatsAreRejected) {
    EXPECT_EQ(code_of({0x05, 0x01, 0x00}), kErrUnsupportedFormat);  // octal 01
    EXPECT_EQ(code_of({0xC1, 0x01, 0x00}), kErrUnsupportedFormat);  // octal 60
    EXPECT_EQ(code_of({0x45, 0x01, 0x41}), kErrUnsupportedFormat);  // octal 21 = JIS-8
    EXPECT_EQ(code_of({0xFD, 0x01, 0x00}), kErrUnsupportedFormat);  // octal 77
}

TEST(CodecReject, DataLongerThanTheInputIsTruncated) {
    EXPECT_EQ(code_of({0x41, 0x05, 0x61, 0x62}), kErrTruncated);
    EXPECT_EQ(code_of({0x41, 0xFF}), kErrTruncated);
    EXPECT_EQ(code_of({0x43, 0xFF, 0xFF, 0xFF, 0x61}), kErrTruncated);  // 16 MiB claimed, 1 present
}

TEST(CodecReject, LengthNotAMultipleOfTheElementSize) {
    EXPECT_EQ(code_of({0xB1, 0x05, 0, 0, 0, 1, 2}), kErrBadLength);        // U4, 5 bytes
    EXPECT_EQ(code_of({0x69, 0x03, 0, 1, 2}), kErrBadLength);              // I2, 3 bytes
    EXPECT_EQ(code_of({0x81, 0x07, 0, 0, 0, 0, 0, 0, 0}), kErrBadLength);  // F8, 7 bytes
    EXPECT_EQ(code_of({0x91, 0x02, 0, 0}), kErrBadLength);                 // F4, 2 bytes
}

TEST(CodecReject, ListCountLargerThanTheBytesThatRemain) {
    // L claiming 16,777,215 entries with nothing after it: rejected before
    // any allocation, not after trying to build them.
    EXPECT_EQ(code_of({0x03, 0xFF, 0xFF, 0xFF}), kErrListCountTooLarge);
    EXPECT_EQ(code_of({0x01, 0x03, 0x41, 0x00}), kErrListCountTooLarge);  // 3 entries, room for 1
}

TEST(CodecReject, ListWithFewerItemsThanItsCountIsTruncated) {
    // Count 2 needs at least 4 bytes and 4 are present, so the count check
    // passes; the second item (A, length 5) then has no data.
    EXPECT_EQ(code_of({0x01, 0x02, 0x41, 0x00, 0x41, 0x05}), kErrTruncated);
    // With only 3 bytes after the header the count itself is impossible.
    EXPECT_EQ(code_of({0x01, 0x02, 0x41, 0x00, 0x41}), kErrListCountTooLarge);
}

Bytes nested_lists(std::size_t levels) {
    // levels lists, each holding the next; the innermost is empty.
    Bytes bytes;
    for (std::size_t i = 0; i + 1 < levels; ++i) {
        bytes.push_back(0x01);
        bytes.push_back(0x01);
    }
    bytes.push_back(0x01);
    bytes.push_back(0x00);
    return bytes;
}

TEST(CodecReject, NestingOfThirtyTwoIsAcceptedAndThirtyThreeIsRejected) {
    EXPECT_EQ(code_of(nested_lists(32)), 0);
    EXPECT_EQ(code_of(nested_lists(33)), kErrTooDeep);
    EXPECT_EQ(code_of(nested_lists(5000)), kErrTooDeep);  // no stack blow-up either
}

TEST(CodecReject, EncoderRefusesWhatTheDecoderWouldRefuse) {
    Item deep = Item::list({});
    for (int i = 0; i < 40; ++i) {
        deep = Item::list({deep});
    }
    auto out = encode(deep);
    ASSERT_FALSE(out);
    EXPECT_EQ(out.error().code, kErrTooDeep);
}

TEST(CodecReject, BodyWithTrailingBytesIsRejectedByDecodeBody) {
    EXPECT_EQ(code_of({0x41, 0x01, 0x61, 0x00}), kErrTrailingBytes);
}

TEST(CodecReject, NonMinimalLengthBytesAreAcceptedButReEncodedMinimally) {
    // A "a" with a 2-byte length field holding 1. TODO(verify): the standard's
    // rule on non-minimal lengths (docs/protocol-notes.md).
    const Bytes odd = {0x42, 0x00, 0x01, 0x61};
    auto decoded = decode_body(ByteSpan(odd));
    ASSERT_TRUE(decoded);
    EXPECT_EQ(decoded.value(), Item::ascii("a"));
    EXPECT_EQ(encode(decoded.value()).value(), (Bytes{0x41, 0x01, 0x61}));
}

TEST(CodecReject, EveryPrefixOfAValidItemIsRejectedNotCrashed) {
    const Item item = Item::list({Item::u4(std::uint32_t{7}), Item::ascii("hello"),
                                  Item::list({Item::f4(1.5f), Item::binary({1, 2, 3, 4})})});
    const Bytes full = encode(item).value();
    for (std::size_t n = 0; n < full.size(); ++n) {
        const Bytes prefix(full.begin(), full.begin() + static_cast<std::ptrdiff_t>(n));
        EXPECT_NE(code_of(prefix), 0) << "prefix of " << n << " bytes was accepted";
    }
    EXPECT_EQ(code_of(full), 0);
}

}  // namespace
}  // namespace ssim::secsgem::secs2
