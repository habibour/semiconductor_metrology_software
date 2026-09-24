// UT-HSMS-2 (PRD 10.2): fake-clock tests of T3, T6, T7 and T8 expiry and
// Linktest behaviour, plus the session control messages and reply matching.
// Nothing here sleeps: time only moves when the test advances the FakeClock.

#include "ssim/secsgem/hsms/session.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <set>
#include <vector>

#include "ssim/core/clock.hpp"
#include "ssim/secsgem/hsms/data_message.hpp"
#include "ssim/secsgem/hsms/frame_decoder.hpp"
#include "ssim/secsgem/secs2/codec.hpp"

namespace ssim::secsgem::hsms {
namespace {

using std::chrono::milliseconds;
using Bytes = std::vector<std::uint8_t>;

std::vector<Frame> frames_of(const SessionOutput& out) {
    FrameDecoder decoder(1 << 20);
    std::vector<Frame> frames;
    for (const Bytes& bytes : out.to_send) {
        auto r = decoder.feed(ByteSpan(bytes));
        EXPECT_TRUE(r);
        for (auto& f : r.value()) frames.push_back(std::move(f));
    }
    return frames;
}

SessionConfig test_config() {
    SessionConfig c;
    c.t3 = Duration(3000);
    c.t6 = Duration(2000);
    c.t7 = Duration(4000);
    c.t8 = Duration(1000);
    c.linktest_interval = Duration(10000);
    c.max_frame_bytes = 4096;
    c.device_id = 7;
    return c;
}

struct Harness {
    ssim::core::FakeClock clock;
    Session session;

    explicit Harness(SessionConfig config = test_config()) : session(config) {}

    TimePoint now() const { return clock.monotonic_now(); }
    void advance(int ms) { clock.advance(milliseconds(ms)); }

    SessionOutput connect() { return session.on_connect(now()); }
    SessionOutput tick() { return session.on_tick(now()); }

    SessionOutput feed(const Frame& frame) {
        const Bytes bytes = encode_frame(frame).value();
        return session.on_bytes(now(), ByteSpan(bytes));
    }
    SessionOutput feed_bytes(const Bytes& bytes) {
        return session.on_bytes(now(), ByteSpan(bytes));
    }

    // Connect and complete the Select handshake.
    void select(std::uint32_t system = 100) {
        connect();
        const SessionOutput out = feed(make_control_frame(SType::kSelectReq, system));
        ASSERT_EQ(session.state(), ConnectionState::kSelected);
        ASSERT_FALSE(out.close);
    }
};

Frame data_frame(std::uint8_t stream, std::uint8_t function, bool w, std::uint32_t system,
                 std::uint16_t session_id = 7) {
    return make_data_frame(session_id, stream, function, w, system, {});
}

secs2::Message request(std::uint8_t stream, std::uint8_t function, bool w = true) {
    secs2::Message m;
    m.stream = stream;
    m.function = function;
    m.w_bit = w;
    return m;
}

// ---- connect, T7, select, deselect --------------------------------------

TEST(Session, ConnectMovesToNotSelected) {
    Harness h;
    EXPECT_EQ(h.session.state(), ConnectionState::kNotConnected);
    h.connect();
    EXPECT_EQ(h.session.state(), ConnectionState::kNotSelected);
}

TEST(Session, ConnectWhileAlreadyConnectedIsIgnored) {
    Harness h;
    h.connect();
    h.advance(3000);
    h.connect();  // must not restart T7
    h.advance(1000);
    EXPECT_TRUE(h.tick().close);
}

TEST(Session, T7ClosesAConnectionThatNeverSelects) {
    Harness h;
    h.connect();
    h.advance(3999);
    EXPECT_FALSE(h.tick().close);
    h.advance(1);
    const SessionOutput out = h.tick();
    EXPECT_TRUE(out.close);
    EXPECT_NE(out.close_reason.find("T7"), std::string::npos);
    EXPECT_EQ(h.session.state(), ConnectionState::kNotConnected);
}

TEST(Session, SelectAnswersOkAndCancelsT7) {
    Harness h;
    h.connect();
    h.advance(2000);
    const SessionOutput out = h.feed(make_control_frame(SType::kSelectReq, 0xABCD));
    const auto frames = frames_of(out);
    ASSERT_EQ(frames.size(), 1u);
    EXPECT_EQ(frames[0].header.stype, static_cast<std::uint8_t>(SType::kSelectRsp));
    EXPECT_EQ(frames[0].header.system_bytes, 0xABCDu);  // reply echoes the request
    EXPECT_EQ(frames[0].header.session_id, kControlSessionId);
    EXPECT_EQ(frames[0].header.byte3, kSelectOk);
    EXPECT_EQ(h.session.state(), ConnectionState::kSelected);

    h.advance(5000);  // past T7, before the idle linktest
    EXPECT_FALSE(h.tick().close);
}

TEST(Session, SelectWhileSelectedAnswersAlreadyActive) {
    Harness h;
    h.select();
    const auto frames = frames_of(h.feed(make_control_frame(SType::kSelectReq, 5)));
    ASSERT_EQ(frames.size(), 1u);
    EXPECT_EQ(frames[0].header.byte3, kSelectAlreadyActive);
    EXPECT_EQ(h.session.state(), ConnectionState::kSelected);
}

TEST(Session, DeselectReturnsToNotSelectedAndRestartsT7) {
    Harness h;
    h.select();
    h.advance(1000);
    const auto frames = frames_of(h.feed(make_control_frame(SType::kDeselectReq, 9)));
    ASSERT_EQ(frames.size(), 1u);
    EXPECT_EQ(frames[0].header.stype, static_cast<std::uint8_t>(SType::kDeselectRsp));
    EXPECT_EQ(frames[0].header.byte3, kDeselectOk);
    EXPECT_EQ(h.session.state(), ConnectionState::kNotSelected);

    h.advance(3999);
    EXPECT_FALSE(h.tick().close);
    h.advance(1);
    EXPECT_TRUE(h.tick().close);
}

TEST(Session, DeselectWhileNotSelectedAnswersNotSelected) {
    Harness h;
    h.connect();
    const auto frames = frames_of(h.feed(make_control_frame(SType::kDeselectReq, 9)));
    ASSERT_EQ(frames.size(), 1u);
    EXPECT_EQ(frames[0].header.byte3, kDeselectNotSelected);
}

TEST(Session, SeparateClosesTheConnection) {
    Harness h;
    h.select();
    const SessionOutput out = h.feed(make_control_frame(SType::kSeparateReq, 1));
    EXPECT_TRUE(out.close);
    EXPECT_EQ(h.session.state(), ConnectionState::kNotConnected);
}

// ---- linktest, T6 -------------------------------------------------------

TEST(Session, LinktestRequestIsAnsweredInEveryConnectedState) {
    Harness h;
    h.connect();
    auto frames = frames_of(h.feed(make_control_frame(SType::kLinktestReq, 21)));
    ASSERT_EQ(frames.size(), 1u);
    EXPECT_EQ(frames[0].header.stype, static_cast<std::uint8_t>(SType::kLinktestRsp));
    EXPECT_EQ(frames[0].header.system_bytes, 21u);

    h.feed(make_control_frame(SType::kSelectReq, 1));
    frames = frames_of(h.feed(make_control_frame(SType::kLinktestReq, 22)));
    ASSERT_EQ(frames.size(), 1u);
    EXPECT_EQ(frames[0].header.system_bytes, 22u);
}

TEST(Session, IdleSelectedSessionSendsLinktestAndClosesWhenT6ExpiresUnanswered) {
    Harness h;
    h.select();
    h.advance(9999);
    EXPECT_TRUE(h.tick().to_send.empty());
    h.advance(1);
    const SessionOutput out = h.tick();
    const auto frames = frames_of(out);
    ASSERT_EQ(frames.size(), 1u);
    EXPECT_EQ(frames[0].header.stype, static_cast<std::uint8_t>(SType::kLinktestReq));
    EXPECT_EQ(frames[0].header.session_id, kControlSessionId);
    EXPECT_FALSE(out.close);

    h.advance(1999);
    EXPECT_FALSE(h.tick().close);
    h.advance(1);
    const SessionOutput expired = h.tick();
    EXPECT_TRUE(expired.close);
    EXPECT_NE(expired.close_reason.find("T6"), std::string::npos);
}

TEST(Session, LinktestReplyWithinT6KeepsTheConnectionAndSchedulesTheNext) {
    Harness h;
    h.select();
    h.advance(10000);
    const auto sent = frames_of(h.tick());
    ASSERT_EQ(sent.size(), 1u);
    h.advance(1000);
    const SessionOutput reply =
        h.feed(make_control_frame(SType::kLinktestRsp, sent[0].header.system_bytes));
    EXPECT_TRUE(reply.to_send.empty());
    EXPECT_FALSE(reply.close);

    h.advance(5000);  // far beyond T6: nothing pending any more
    EXPECT_FALSE(h.tick().close);
    h.advance(5000);  // idle for 10 s since the reply
    EXPECT_EQ(frames_of(h.tick()).size(), 1u);
}

TEST(Session, TrafficResetsTheIdleLinktestClock) {
    Harness h;
    h.select();
    h.advance(9000);
    h.feed(data_frame(1, 1, true, 500));
    h.advance(9000);  // 18 s since select, but only 9 s since the last traffic
    EXPECT_TRUE(h.tick().to_send.empty());
    h.advance(1000);
    EXPECT_EQ(frames_of(h.tick()).size(), 1u);
}

TEST(Session, UnmatchedLinktestReplyIsRejectedWithTransactionNotOpen) {
    Harness h;
    h.select();
    const auto frames = frames_of(h.feed(make_control_frame(SType::kLinktestRsp, 999)));
    ASSERT_EQ(frames.size(), 1u);
    EXPECT_EQ(frames[0].header.stype, static_cast<std::uint8_t>(SType::kRejectReq));
    EXPECT_EQ(frames[0].header.byte2, static_cast<std::uint8_t>(SType::kLinktestRsp));
    EXPECT_EQ(frames[0].header.byte3, kRejectTransactionNotOpen);
}

// ---- T8 and framing -----------------------------------------------------

Bytes wire_of(const Frame& f) { return encode_frame(f).value(); }

TEST(Session, T8ClosesWhenAFrameStallsAndIsResetByMoreBytes) {
    Harness h;
    h.select();
    const Bytes frame = wire_of(data_frame(1, 1, true, 1));
    ASSERT_GT(frame.size(), 8u);

    h.feed_bytes(Bytes(frame.begin(), frame.begin() + 6));
    h.advance(999);
    EXPECT_FALSE(h.tick().close);

    h.feed_bytes(Bytes(frame.begin() + 6, frame.begin() + 7));  // one more byte: T8 restarts
    h.advance(999);
    EXPECT_FALSE(h.tick().close);
    h.advance(1);
    const SessionOutput out = h.tick();
    EXPECT_TRUE(out.close);
    EXPECT_NE(out.close_reason.find("T8"), std::string::npos);
}

TEST(Session, CompletingTheFrameStopsT8) {
    Harness h;
    h.select();
    const Bytes frame = wire_of(data_frame(1, 1, true, 1));
    h.feed_bytes(Bytes(frame.begin(), frame.begin() + 6));
    h.advance(500);
    const SessionOutput out = h.feed_bytes(Bytes(frame.begin() + 6, frame.end()));
    EXPECT_EQ(out.deliveries.size(), 1u);
    h.advance(5000);  // well past T8
    EXPECT_FALSE(h.tick().close);
}

TEST(Session, OversizeFrameClosesTheConnection) {
    Harness h;
    h.select();
    const SessionOutput out = h.feed_bytes(Bytes{0x00, 0x01, 0x00, 0x00});  // 65,536 > 4096
    EXPECT_TRUE(out.close);
    EXPECT_NE(out.close_reason.find("framing"), std::string::npos);
    EXPECT_EQ(h.session.state(), ConnectionState::kNotConnected);
}

TEST(Session, LengthBelowTheHeaderClosesTheConnection) {
    Harness h;
    h.connect();
    EXPECT_TRUE(h.feed_bytes(Bytes{0x00, 0x00, 0x00, 0x03}).close);
}

TEST(Session, BytesAfterCloseAreIgnoredUntilTheNextConnect) {
    Harness h;
    h.select();
    h.feed(make_control_frame(SType::kSeparateReq, 1));
    const SessionOutput out = h.feed(make_control_frame(SType::kSelectReq, 2));
    EXPECT_TRUE(out.to_send.empty());
    EXPECT_EQ(h.session.state(), ConnectionState::kNotConnected);
}

// ---- rejects ------------------------------------------------------------

TEST(Session, DataBeforeSelectIsRejectedNotDelivered) {
    Harness h;
    h.connect();
    const SessionOutput out = h.feed(data_frame(1, 13, true, 77));
    EXPECT_TRUE(out.deliveries.empty());
    const auto frames = frames_of(out);
    ASSERT_EQ(frames.size(), 1u);
    EXPECT_EQ(frames[0].header.stype, static_cast<std::uint8_t>(SType::kRejectReq));
    EXPECT_EQ(frames[0].header.system_bytes, 77u);
    EXPECT_EQ(frames[0].header.byte2, static_cast<std::uint8_t>(SType::kData));
    EXPECT_EQ(frames[0].header.byte3, kRejectEntityNotSelected);
}

TEST(Session, UnknownSTypeIsRejected) {
    Harness h;
    h.select();
    Frame f = make_control_frame(SType::kSelectReq, 8);
    f.header.stype = 0x55;
    const auto frames = frames_of(h.feed(f));
    ASSERT_EQ(frames.size(), 1u);
    EXPECT_EQ(frames[0].header.byte2, 0x55);
    EXPECT_EQ(frames[0].header.byte3, kRejectSTypeNotSupported);
}

TEST(Session, NonZeroPTypeIsRejected) {
    Harness h;
    h.select();
    Frame f = data_frame(1, 1, true, 9);
    f.header.ptype = 3;
    const SessionOutput out = h.feed(f);
    EXPECT_TRUE(out.deliveries.empty());
    const auto frames = frames_of(out);
    ASSERT_EQ(frames.size(), 1u);
    EXPECT_EQ(frames[0].header.byte2, 3);
    EXPECT_EQ(frames[0].header.byte3, kRejectPTypeNotSupported);
}

TEST(Session, SelectRspOrDeselectRspFromThePeerIsRejectedBecauseWeNeverAskedForOne) {
    Harness h;
    h.select();
    for (SType stype : {SType::kSelectRsp, SType::kDeselectRsp}) {
        const auto frames = frames_of(h.feed(make_control_frame(stype, 3)));
        ASSERT_EQ(frames.size(), 1u);
        EXPECT_EQ(frames[0].header.byte3, kRejectTransactionNotOpen);
    }
}

// ---- data, transactions and T3 ------------------------------------------

TEST(Session, DataMessageWhileSelectedIsDelivered) {
    Harness h;
    h.select();
    const SessionOutput out = h.feed(data_frame(1, 13, true, 1234));
    ASSERT_EQ(out.deliveries.size(), 1u);
    EXPECT_EQ(out.deliveries[0].frame.header.system_bytes, 1234u);
    EXPECT_FALSE(out.deliveries[0].reply_to.has_value());
    EXPECT_TRUE(out.to_send.empty());
}

TEST(Session, SelectAndDataInOneReadAreHandledInOrder) {
    Harness h;
    h.connect();
    Bytes both = wire_of(make_control_frame(SType::kSelectReq, 1));
    const Bytes data = wire_of(data_frame(1, 1, true, 2));
    both.insert(both.end(), data.begin(), data.end());
    const SessionOutput out = h.feed_bytes(both);
    EXPECT_EQ(frames_of(out).size(), 1u);  // Select.rsp
    ASSERT_EQ(out.deliveries.size(), 1u);  // and the data was accepted, not rejected
}

TEST(Session, SendRequestBuildsAWBitFrameAndOpensATransaction) {
    Harness h;
    h.select();
    SessionOutput out;
    auto sys = h.session.send_request(h.now(), request(6, 11), out);
    ASSERT_TRUE(sys);
    const auto frames = frames_of(out);
    ASSERT_EQ(frames.size(), 1u);
    EXPECT_EQ(frames[0].header.session_id, 7);  // device id from the config
    EXPECT_TRUE(frames[0].header.w_bit());
    EXPECT_EQ(frames[0].header.stream(), 6);
    EXPECT_EQ(frames[0].header.function(), 11);
    EXPECT_EQ(frames[0].header.system_bytes, sys.value());
    EXPECT_EQ(h.session.open_transactions(), 1u);
}

TEST(Session, RequestWithoutWBitOpensNoTransaction) {
    Harness h;
    h.select();
    SessionOutput out;
    ASSERT_TRUE(h.session.send_request(h.now(), request(6, 11, false), out));
    EXPECT_EQ(h.session.open_transactions(), 0u);
}

TEST(Session, T3ReportsAnUnansweredRequestOnceAndKeepsTheConnection) {
    Harness h;
    h.select();
    SessionOutput sent;
    const std::uint32_t sys = h.session.send_request(h.now(), request(6, 11), sent).value();

    h.advance(2999);
    EXPECT_TRUE(h.tick().timed_out.empty());
    h.advance(1);
    const SessionOutput out = h.tick();
    ASSERT_EQ(out.timed_out.size(), 1u);
    EXPECT_EQ(out.timed_out[0], sys);
    EXPECT_FALSE(out.close);
    EXPECT_EQ(h.session.state(), ConnectionState::kSelected);
    EXPECT_EQ(h.session.open_transactions(), 0u);
    EXPECT_TRUE(h.tick().timed_out.empty());  // reported once

    // A late reply is still delivered, but no longer matched to a request.
    const SessionOutput late = h.feed(data_frame(6, 12, false, sys));
    ASSERT_EQ(late.deliveries.size(), 1u);
    EXPECT_FALSE(late.deliveries[0].reply_to.has_value());
}

TEST(Session, ReplyBeforeT3ClosesTheTransaction) {
    Harness h;
    h.select();
    SessionOutput sent;
    const std::uint32_t sys = h.session.send_request(h.now(), request(6, 11), sent).value();
    h.advance(2000);
    const SessionOutput out = h.feed(data_frame(6, 12, false, sys));
    ASSERT_EQ(out.deliveries.size(), 1u);
    ASSERT_TRUE(out.deliveries[0].reply_to.has_value());
    EXPECT_EQ(*out.deliveries[0].reply_to, sys);
    h.advance(5000);
    EXPECT_TRUE(h.tick().timed_out.empty());
}

TEST(Session, SeveralOutstandingRequestsAreMatchedEvenWhenRepliesComeOutOfOrder) {
    Harness h;
    h.select();
    SessionOutput sent;
    const std::uint32_t a = h.session.send_request(h.now(), request(1, 1), sent).value();
    const std::uint32_t b = h.session.send_request(h.now(), request(1, 3), sent).value();
    const std::uint32_t c = h.session.send_request(h.now(), request(2, 13), sent).value();
    EXPECT_EQ(h.session.open_transactions(), 3u);

    auto reply_to = [&](std::uint8_t stream, std::uint8_t function, std::uint32_t sys) {
        const SessionOutput out = h.feed(data_frame(stream, function, false, sys));
        return out.deliveries.at(0).reply_to;
    };
    EXPECT_EQ(reply_to(2, 14, c), std::optional<std::uint32_t>(c));
    EXPECT_EQ(reply_to(1, 2, a), std::optional<std::uint32_t>(a));
    EXPECT_EQ(h.session.open_transactions(), 1u);
    EXPECT_EQ(reply_to(1, 4, b), std::optional<std::uint32_t>(b));
    EXPECT_EQ(h.session.open_transactions(), 0u);
}

TEST(Session, AHostRequestThatReusesOurSystemBytesIsNotTakenForAReply) {
    Harness h;
    h.select();
    SessionOutput sent;
    const std::uint32_t sys = h.session.send_request(h.now(), request(6, 11), sent).value();
    // Odd function = a primary message from the host, same system bytes.
    const SessionOutput out = h.feed(data_frame(1, 1, true, sys));
    ASSERT_EQ(out.deliveries.size(), 1u);
    EXPECT_FALSE(out.deliveries[0].reply_to.has_value());
    EXPECT_EQ(h.session.open_transactions(), 1u);  // still waiting for the real reply
}

TEST(Session, SystemBytesAreUniqueAndWrapAroundSkippingZero) {
    SessionConfig c = test_config();
    c.first_system_bytes = 0xFFFFFFFEu;
    Harness h(c);
    h.select();
    std::vector<std::uint32_t> got;
    for (int i = 0; i < 5; ++i) {
        SessionOutput out;
        got.push_back(h.session.send_request(h.now(), request(1, 1), out).value());
    }
    EXPECT_EQ(got, (std::vector<std::uint32_t>{0xFFFFFFFEu, 0xFFFFFFFFu, 1u, 2u, 3u}));
    EXPECT_EQ(std::set<std::uint32_t>(got.begin(), got.end()).size(), got.size());
}

TEST(Session, TooManyOpenTransactionsAreRefused) {
    SessionConfig c = test_config();
    c.max_open_transactions = 3;
    Harness h(c);
    h.select();
    SessionOutput out;
    for (int i = 0; i < 3; ++i) {
        ASSERT_TRUE(h.session.send_request(h.now(), request(1, 1), out));
    }
    auto refused = h.session.send_request(h.now(), request(1, 1), out);
    ASSERT_FALSE(refused);
    EXPECT_EQ(refused.error().code, kErrTooManyTransactions);
    // A message that wants no reply is not limited by the transaction bound.
    EXPECT_TRUE(h.session.send_request(h.now(), request(1, 1, false), out));
}

TEST(Session, SendingBeforeSelectIsAnError) {
    Harness h;
    h.connect();
    SessionOutput out;
    auto r = h.session.send_request(h.now(), request(1, 1), out);
    ASSERT_FALSE(r);
    EXPECT_EQ(r.error().code, kErrNotSelected);
    EXPECT_TRUE(out.to_send.empty());
    EXPECT_FALSE(h.session.send_reply(1, request(1, 2, false), out));
}

TEST(Session, ReplyEchoesTheRequestSystemBytesAndNeverAsksForAReply) {
    Harness h;
    h.select();
    SessionOutput out;
    ASSERT_TRUE(h.session.send_reply(0x1234, request(1, 2, true), out));
    const auto frames = frames_of(out);
    ASSERT_EQ(frames.size(), 1u);
    EXPECT_EQ(frames[0].header.system_bytes, 0x1234u);
    EXPECT_FALSE(frames[0].header.w_bit());
    EXPECT_EQ(h.session.open_transactions(), 0u);
}

TEST(Session, MessageLargerThanTheMaximumFrameIsRefused) {
    Harness h;
    h.select();
    secs2::Message big = request(6, 11);
    big.body = secs2::Item::binary(Bytes(5000, 1));  // limit is 4096
    SessionOutput out;
    auto r = h.session.send_request(h.now(), big, out);
    ASSERT_FALSE(r);
    EXPECT_EQ(r.error().code, kErrMessageTooLarge);
    EXPECT_TRUE(out.to_send.empty());
    EXPECT_EQ(h.session.open_transactions(), 0u);
}

TEST(Session, DeselectDropsOpenTransactions) {
    Harness h;
    h.select();
    SessionOutput out;
    ASSERT_TRUE(h.session.send_request(h.now(), request(6, 11), out));
    h.feed(make_control_frame(SType::kDeselectReq, 4));
    EXPECT_EQ(h.session.open_transactions(), 0u);
}

TEST(Session, DisconnectThenReconnectStartsCleanWithAFreshT7) {
    Harness h;
    h.select();
    SessionOutput out;
    ASSERT_TRUE(h.session.send_request(h.now(), request(6, 11), out));
    h.session.on_disconnect();
    EXPECT_EQ(h.session.state(), ConnectionState::kNotConnected);
    EXPECT_EQ(h.session.open_transactions(), 0u);

    h.advance(60000);
    h.connect();
    EXPECT_EQ(h.session.state(), ConnectionState::kNotSelected);
    h.advance(3999);
    EXPECT_FALSE(h.tick().close);
    h.advance(1);
    EXPECT_TRUE(h.tick().close);
}

TEST(Session, ATickBeforeAnyConnectionDoesNothing) {
    Harness h;
    h.advance(100000);
    const SessionOutput out = h.tick();
    EXPECT_FALSE(out.close);
    EXPECT_TRUE(out.to_send.empty());
}

// ---- configuration and message bridge -----------------------------------

TEST(SessionConfigFromComm, ConvertsSecondsToMillisecondsAndCopiesTheLimits) {
    ssim::core::CommConfig comm;  // defaults from the PRD
    comm.t3_s = 1.5;
    comm.device_id = 3;
    comm.max_frame_bytes = 2048;
    const SessionConfig c = session_config_from(comm);
    EXPECT_EQ(c.t3, Duration(1500));
    EXPECT_EQ(c.t6, Duration(5000));
    EXPECT_EQ(c.t7, Duration(10000));
    EXPECT_EQ(c.t8, Duration(5000));
    EXPECT_EQ(c.linktest_interval, Duration(60000));
    EXPECT_EQ(c.device_id, 3);
    EXPECT_EQ(c.max_frame_bytes, 2048u);
}

TEST(DataMessage, FrameBecomesAMessageWithItsBodyDecoded) {
    const secs2::Item body = secs2::Item::list({secs2::Item::u4(std::uint32_t{5})});
    const Frame f = make_data_frame(7, 6, 11, true, 1, secs2::encode(body).value());
    auto m = to_message(f);
    ASSERT_TRUE(m);
    EXPECT_EQ(m.value().stream, 6);
    EXPECT_EQ(m.value().function, 11);
    EXPECT_TRUE(m.value().w_bit);
    ASSERT_TRUE(m.value().body.has_value());
    EXPECT_EQ(*m.value().body, body);
}

TEST(DataMessage, EmptyBodyMeansNoItem) {
    auto m = to_message(make_data_frame(7, 1, 1, true, 1, {}));
    ASSERT_TRUE(m);
    EXPECT_FALSE(m.value().body.has_value());
}

TEST(DataMessage, MalformedBodyAndControlFramesAreErrorsNotCrashes) {
    auto bad = to_message(make_data_frame(7, 1, 1, true, 1, {0x41, 0x05, 0x61}));
    ASSERT_FALSE(bad);
    EXPECT_EQ(bad.error().code, secs2::kErrTruncated);

    auto control = to_message(make_control_frame(SType::kLinktestReq, 1));
    ASSERT_FALSE(control);
    EXPECT_EQ(control.error().code, kErrNotADataFrame);
}

}  // namespace
}  // namespace ssim::secsgem::hsms
