// FT-FUZZ-1 / NFR-REL-1 / SM7: the HSMS layer must survive hostile input.
// 100,000 randomly mutated byte streams go through a real Session (and 50,000
// through the bare frame decoder). Nothing may crash or hang, the session may
// never emit a frame that its own decoder rejects, and its open-transaction
// table stays within its bound. Seeded, so any failure is reproducible; run
// under AddressSanitizer/UBSan in CI for the memory-safety half of the claim.

#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <random>
#include <vector>

#include "ssim/secsgem/hsms/frame.hpp"
#include "ssim/secsgem/hsms/frame_decoder.hpp"
#include "ssim/secsgem/hsms/session.hpp"

namespace ssim::secsgem::hsms {
namespace {

using Bytes = std::vector<std::uint8_t>;

constexpr std::size_t kMaxFrame = 4096;

Frame control_frame(SType stype, std::uint32_t system_bytes) {
    Frame f;
    f.header.session_id = 0xFFFF;
    f.header.stype = static_cast<std::uint8_t>(stype);
    f.header.system_bytes = system_bytes;
    return f;
}

Frame data_frame(std::uint8_t stream, std::uint8_t function, bool w, std::uint32_t system_bytes,
                 Bytes body) {
    Frame f;
    f.header.session_id = 0;
    f.header.byte2 = static_cast<std::uint8_t>(stream | (w ? 0x80U : 0U));
    f.header.byte3 = function;
    f.header.system_bytes = system_bytes;
    f.body = std::move(body);
    return f;
}

std::vector<Bytes> seed_corpus() {
    std::vector<Frame> frames = {
        control_frame(SType::kSelectReq, 1),
        control_frame(SType::kDeselectReq, 2),
        control_frame(SType::kLinktestReq, 3),
        control_frame(SType::kLinktestRsp, 4),
        control_frame(SType::kSeparateReq, 5),
        control_frame(SType::kRejectReq, 6),
        data_frame(1, 1, true, 7, {}),
        data_frame(1, 13, true, 8, {0x01, 0x00}),
        data_frame(2, 41, true, 9, {0x01, 0x02, 0x41, 0x05, 'S', 'T', 'A', 'R', 'T', 0x01, 0x00}),
        data_frame(6, 11, true, 10, Bytes(300, 0x41)),
        data_frame(9, 7, false, 11, {0x21, 0x01, 0x00}),
    };
    std::vector<Bytes> corpus;
    for (const Frame& f : frames) {
        corpus.push_back(encode_frame(f).value());
    }
    return corpus;
}

Bytes mutate(Bytes bytes, std::mt19937& rng) {
    const int operations = 1 + static_cast<int>(rng() % 4);
    for (int i = 0; i < operations; ++i) {
        switch (rng() % 7) {
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
                    bytes.begin() + static_cast<long>(bytes.empty() ? 0 : rng() % bytes.size()),
                    static_cast<std::uint8_t>(rng()));
                break;
            case 4:  // tamper with the 4-byte length prefix
                if (bytes.size() >= 4) {
                    bytes[rng() % 4] = static_cast<std::uint8_t>(rng());
                }
                break;
            case 5:  // duplicate a slice
                if (bytes.size() > 2) {
                    const std::size_t from = rng() % bytes.size();
                    const std::size_t len = 1 + rng() % (bytes.size() - from);
                    const Bytes slice(bytes.begin() + static_cast<long>(from),
                                      bytes.begin() + static_cast<long>(from + len));
                    bytes.insert(bytes.end(), slice.begin(), slice.end());
                }
                break;
            default:  // huge length prefix
                if (bytes.size() >= 4) {
                    bytes[0] = 0xFF;
                    bytes[1] = static_cast<std::uint8_t>(rng());
                }
                break;
        }
    }
    return bytes;
}

// What a peer could plausibly put on the wire: one to three mutated frames
// back to back, sometimes followed by raw noise.
Bytes hostile_stream(const std::vector<Bytes>& corpus, std::mt19937& rng) {
    Bytes stream;
    const int frames = 1 + static_cast<int>(rng() % 3);
    for (int i = 0; i < frames; ++i) {
        const Bytes m = mutate(corpus[rng() % corpus.size()], rng);
        stream.insert(stream.end(), m.begin(), m.end());
    }
    if (rng() % 8 == 0) {
        for (int i = 0; i < 16; ++i) stream.push_back(static_cast<std::uint8_t>(rng()));
    }
    return stream;
}

// Every frame the machine sends must be well formed, whatever it was fed.
void expect_outputs_are_well_formed(const SessionOutput& out) {
    for (const Bytes& sent : out.to_send) {
        FrameDecoder decoder(1 << 20);
        auto frames = decoder.feed(ByteSpan(sent.data(), sent.size()));
        ASSERT_TRUE(frames.has_value()) << "session emitted a malformed frame";
        ASSERT_EQ(frames.value().size(), 1u);
        ASSERT_FALSE(decoder.has_partial());
    }
}

TEST(HsmsFuzz, HundredThousandMutatedStreamsNeverBreakTheSession) {
    const std::vector<Bytes> corpus = seed_corpus();
    std::mt19937 rng(20260927);
    const TimePoint t0{};

    SessionConfig config;
    config.max_frame_bytes = kMaxFrame;
    config.max_open_transactions = 8;

    std::size_t closed = 0;
    std::size_t reached_selected = 0;
    for (int i = 0; i < 100000; ++i) {
        Session session(config);
        TimePoint now = t0;
        expect_outputs_are_well_formed(session.on_connect(now));

        // Half the runs start from a properly selected session, so the data and
        // control paths behind Select are exercised, not just the rejection path.
        if (i % 2 == 0) {
            const Bytes select = encode_frame(control_frame(SType::kSelectReq, 99)).value();
            expect_outputs_are_well_formed(
                session.on_bytes(now, ByteSpan(select.data(), select.size())));
        }
        if (session.state() == ConnectionState::kSelected) {
            ++reached_selected;
        }

        const Bytes stream = hostile_stream(corpus, rng);
        // Deliver in random-sized chunks, as TCP may.
        std::size_t offset = 0;
        while (offset < stream.size()) {
            const std::size_t chunk = std::min<std::size_t>(1 + rng() % 24, stream.size() - offset);
            now += std::chrono::milliseconds(rng() % 50);
            const SessionOutput out =
                session.on_bytes(now, ByteSpan(stream.data() + offset, chunk));
            expect_outputs_are_well_formed(out);
            if (::testing::Test::HasFatalFailure()) return;
            offset += chunk;
            if (out.close) {
                ++closed;
                break;
            }
        }
        // Let every timer run out: nothing may hang or blow up.
        now += std::chrono::hours(1);
        expect_outputs_are_well_formed(session.on_tick(now));
        ASSERT_LE(session.open_transactions(), config.max_open_transactions);
        session.on_disconnect();
    }
    // The corpus is hostile enough to close connections, and half the runs
    // really were selected; a test that never got there proves nothing.
    EXPECT_GT(closed, 1000u);
    EXPECT_GT(reached_selected, 40000u);
}

TEST(HsmsFuzz, FiftyThousandMutatedStreamsNeverBreakTheFrameDecoder) {
    const std::vector<Bytes> corpus = seed_corpus();
    std::mt19937 rng(424242);
    std::size_t rejected = 0;
    for (int i = 0; i < 50000; ++i) {
        FrameDecoder decoder(kMaxFrame);
        const Bytes stream = hostile_stream(corpus, rng);
        std::size_t offset = 0;
        while (offset < stream.size()) {
            const std::size_t chunk = std::min<std::size_t>(1 + rng() % 24, stream.size() - offset);
            auto frames = decoder.feed(ByteSpan(stream.data() + offset, chunk));
            offset += chunk;
            if (!frames.has_value()) {
                ++rejected;
                ASSERT_TRUE(decoder.failed());
                break;
            }
        }
    }
    EXPECT_GT(rejected, 1000u);
}

}  // namespace
}  // namespace ssim::secsgem::hsms
