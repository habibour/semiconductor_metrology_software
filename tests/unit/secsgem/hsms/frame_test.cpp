// UT-HSMS-1 (PRD 10.2): partial frames, coalesced frames, maximum length, bad header.

#include "ssim/secsgem/hsms/frame.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <vector>

#include "ssim/secsgem/hsms/frame_decoder.hpp"

namespace ssim::secsgem::hsms {
namespace {

using Bytes = std::vector<std::uint8_t>;

Frame sample_data_frame(std::uint32_t system_bytes, std::size_t body_size = 3) {
    Bytes body(body_size);
    for (std::size_t i = 0; i < body_size; ++i) body[i] = static_cast<std::uint8_t>(i + 1);
    return make_data_frame(0x0001, 1, 13, true, system_bytes, body);
}

Bytes wire(const Frame& f) { return encode_frame(f).value(); }

TEST(Frame, EncodesTheDocumentedLayout) {
    // S1F13 W, device 1, system bytes 0x00000042, empty-list body 01 00.
    const Frame f = make_data_frame(1, 1, 13, true, 0x42, {0x01, 0x00});
    EXPECT_EQ(wire(f), (Bytes{0x00, 0x00, 0x00, 0x0C,  // length = 10 + 2
                              0x00, 0x01,              // session id
                              0x81, 0x0D,              // W-bit | stream 1, function 13
                              0x00, 0x00,              // PType 0, SType 0 (data)
                              0x00, 0x00, 0x00, 0x42,  // system bytes
                              0x01, 0x00}));
}

TEST(Frame, ControlFrameUsesTheControlSessionIdAndNoBody) {
    const Frame f = make_control_frame(SType::kLinktestReq, 7);
    EXPECT_EQ(wire(f), (Bytes{0x00, 0x00, 0x00, 0x0A, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x05, 0x00,
                              0x00, 0x00, 0x07}));
}

TEST(Frame, HeaderHelpersSplitByte2IntoWBitAndStream) {
    const Frame f = make_data_frame(1, 6, 11, true, 1, {});
    EXPECT_TRUE(f.header.w_bit());
    EXPECT_EQ(f.header.stream(), 6);
    EXPECT_EQ(f.header.function(), 11);
    EXPECT_FALSE(make_data_frame(1, 6, 12, false, 1, {}).header.w_bit());
}

TEST(Frame, StypeBytesMapBothWays) {
    for (std::uint8_t b : {0, 1, 2, 3, 4, 5, 6, 7, 9}) {
        auto s = stype_from_byte(b);
        ASSERT_TRUE(s.has_value());
        EXPECT_EQ(static_cast<std::uint8_t>(*s), b);
    }
    for (std::uint8_t b : {8, 10, 11, 100, 255}) {
        EXPECT_FALSE(stype_from_byte(b).has_value()) << int(b);
    }
}

TEST(FrameDecoder, DecodesAWholeFrameInOneFeed) {
    FrameDecoder decoder(1024);
    const Frame f = sample_data_frame(9);
    auto out = decoder.feed(ByteSpan(wire(f)));
    ASSERT_TRUE(out);
    ASSERT_EQ(out.value().size(), 1u);
    EXPECT_EQ(out.value()[0], f);
    EXPECT_FALSE(decoder.has_partial());
}

TEST(FrameDecoder, ByteAtATimeFeedGivesTheSameFrame) {
    FrameDecoder decoder(1024);
    const Frame f = sample_data_frame(9, 40);
    const Bytes bytes = wire(f);
    std::vector<Frame> got;
    for (std::size_t i = 0; i < bytes.size(); ++i) {
        auto out = decoder.feed(ByteSpan(&bytes[i], 1));
        ASSERT_TRUE(out);
        for (auto& frame : out.value()) got.push_back(std::move(frame));
        // Partial from the first byte until the last one completes it.
        EXPECT_EQ(decoder.has_partial(), i + 1 < bytes.size());
    }
    ASSERT_EQ(got.size(), 1u);
    EXPECT_EQ(got[0], f);
}

TEST(FrameDecoder, SplitAtEveryPossibleBoundary) {
    const Frame f = sample_data_frame(5, 12);
    const Bytes bytes = wire(f);
    for (std::size_t cut = 0; cut <= bytes.size(); ++cut) {
        FrameDecoder decoder(1024);
        std::vector<Frame> got;
        auto first = decoder.feed(ByteSpan(bytes.data(), cut));
        ASSERT_TRUE(first);
        for (auto& x : first.value()) got.push_back(std::move(x));
        auto second = decoder.feed(ByteSpan(bytes.data() + cut, bytes.size() - cut));
        ASSERT_TRUE(second);
        for (auto& x : second.value()) got.push_back(std::move(x));
        ASSERT_EQ(got.size(), 1u) << "cut at " << cut;
        EXPECT_EQ(got[0], f);
    }
}

TEST(FrameDecoder, SeveralFramesInOneReadAreAllReturnedInOrder) {
    FrameDecoder decoder(1024);
    Bytes stream;
    for (std::uint32_t i = 1; i <= 3; ++i) {
        const Bytes b = wire(sample_data_frame(i, i));
        stream.insert(stream.end(), b.begin(), b.end());
    }
    auto out = decoder.feed(ByteSpan(stream));
    ASSERT_TRUE(out);
    ASSERT_EQ(out.value().size(), 3u);
    for (std::uint32_t i = 0; i < 3; ++i) {
        EXPECT_EQ(out.value()[i].header.system_bytes, i + 1);
    }
}

TEST(FrameDecoder, ACompleteFramePlusTheStartOfTheNextLeavesAPartial) {
    FrameDecoder decoder(1024);
    Bytes stream = wire(sample_data_frame(1));
    const Bytes next = wire(sample_data_frame(2));
    stream.insert(stream.end(), next.begin(), next.begin() + 6);
    auto out = decoder.feed(ByteSpan(stream));
    ASSERT_TRUE(out);
    EXPECT_EQ(out.value().size(), 1u);
    EXPECT_TRUE(decoder.has_partial());
    auto rest = decoder.feed(ByteSpan(next.data() + 6, next.size() - 6));
    ASSERT_TRUE(rest);
    ASSERT_EQ(rest.value().size(), 1u);
    EXPECT_EQ(rest.value()[0].header.system_bytes, 2u);
    EXPECT_FALSE(decoder.has_partial());
}

TEST(FrameDecoder, MaximumLengthIsAcceptedAndOneMoreIsRejected) {
    const std::size_t max = 100;
    {
        FrameDecoder decoder(max);
        auto out = decoder.feed(ByteSpan(wire(sample_data_frame(1, max - kHeaderSize))));
        ASSERT_TRUE(out);
        EXPECT_EQ(out.value().size(), 1u);
    }
    {
        FrameDecoder decoder(max);
        auto out = decoder.feed(ByteSpan(wire(sample_data_frame(1, max - kHeaderSize + 1))));
        ASSERT_FALSE(out);
        EXPECT_EQ(out.error().code, kErrFrameTooLong);
    }
}

TEST(FrameDecoder, OversizeLengthIsRejectedFromTheLengthFieldAloneWithoutBufferingTheBody) {
    FrameDecoder decoder(1024);
    // Claims 4 GiB minus 1; only the four length bytes are supplied.
    auto out = decoder.feed(ByteSpan(Bytes{0xFF, 0xFF, 0xFF, 0xFF}));
    ASSERT_FALSE(out);
    EXPECT_EQ(out.error().code, kErrFrameTooLong);
}

TEST(FrameDecoder, LengthBelowTheHeaderSizeIsABadHeader) {
    for (std::uint8_t len : {0, 1, 9}) {
        FrameDecoder decoder(1024);
        auto out = decoder.feed(ByteSpan(Bytes{0x00, 0x00, 0x00, len}));
        ASSERT_FALSE(out) << int(len);
        EXPECT_EQ(out.error().code, kErrFrameTooShort);
    }
}

TEST(FrameDecoder, ARejectedConnectionStaysFailed) {
    FrameDecoder decoder(1024);
    ASSERT_FALSE(decoder.feed(ByteSpan(Bytes{0x00, 0x00, 0x00, 0x01})));
    EXPECT_TRUE(decoder.failed());
    // Even a perfectly good frame afterwards is not decoded: the peer is
    // desynchronised and the connection must be closed.
    auto again = decoder.feed(ByteSpan(wire(sample_data_frame(1))));
    ASSERT_FALSE(again);
    EXPECT_EQ(again.error().code, kErrFrameTooShort);
}

TEST(FrameDecoder, EmptyFeedIsHarmless) {
    FrameDecoder decoder(1024);
    auto out = decoder.feed(ByteSpan());
    ASSERT_TRUE(out);
    EXPECT_TRUE(out.value().empty());
    EXPECT_FALSE(decoder.has_partial());
}

TEST(FrameDecoder, UnknownStypeAndPtypePassThroughForTheSessionToJudge) {
    FrameDecoder decoder(1024);
    Frame f = make_control_frame(SType::kSelectReq, 3);
    f.header.stype = 0x55;
    f.header.ptype = 0x02;
    auto out = decoder.feed(ByteSpan(wire(f)));
    ASSERT_TRUE(out);
    ASSERT_EQ(out.value().size(), 1u);
    EXPECT_EQ(out.value()[0].header.stype, 0x55);
    EXPECT_EQ(out.value()[0].header.ptype, 0x02);
}

TEST(FrameDecoder, RandomChunkingOfManyFramesRecoversThemAll) {
    Bytes stream;
    const int n = 200;
    for (int i = 0; i < n; ++i) {
        const Bytes b = wire(sample_data_frame(static_cast<std::uint32_t>(i), 1 + i % 17));
        stream.insert(stream.end(), b.begin(), b.end());
    }
    FrameDecoder decoder(1024);
    std::vector<Frame> got;
    std::uint32_t state = 99;
    std::size_t pos = 0;
    while (pos < stream.size()) {
        state = state * 1664525u + 1013904223u;
        const std::size_t chunk =
            std::min<std::size_t>(1 + (state >> 24) % 23, stream.size() - pos);
        auto out = decoder.feed(ByteSpan(stream.data() + pos, chunk));
        ASSERT_TRUE(out);
        for (auto& f : out.value()) got.push_back(std::move(f));
        pos += chunk;
    }
    ASSERT_EQ(got.size(), static_cast<std::size_t>(n));
    for (int i = 0; i < n; ++i) {
        EXPECT_EQ(got[static_cast<std::size_t>(i)].header.system_bytes,
                  static_cast<std::uint32_t>(i));
    }
}

}  // namespace
}  // namespace ssim::secsgem::hsms
