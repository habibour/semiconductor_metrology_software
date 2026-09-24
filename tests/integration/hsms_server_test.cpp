// Loopback integration tests for the passive HSMS server (FR-HSMS-1, FR-HSMS-6,
// FR-HSMS-2, FR-HSMS-4). Real sockets on port 0 (no hard-coded ports); time is
// an atomic test clock the test advances, so no test sleeps. Waits are bounded
// only as a safety net so a bug fails instead of hanging.

#include <gtest/gtest.h>

#include <asio.hpp>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <optional>
#include <vector>

#include "ssim/core/clock.hpp"
#include "ssim/secsgem/hsms/data_message.hpp"
#include "ssim/secsgem/hsms/frame_decoder.hpp"
#include "ssim/secsgem/hsms/server.hpp"
#include "ssim/secsgem/secs2/codec.hpp"

namespace ssim::secsgem::hsms {
namespace {

using asio::ip::tcp;
using std::chrono::milliseconds;
using std::chrono::seconds;
using Bytes = std::vector<std::uint8_t>;

// FakeClock is documented as not thread-safe; the server reads its clock on
// its own thread, so the tests use this one.
class AtomicClock final : public ssim::core::IClock {
public:
    std::chrono::steady_clock::time_point monotonic_now() const override {
        return std::chrono::steady_clock::time_point(
            std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                std::chrono::nanoseconds(ns_.load())));
    }
    std::chrono::system_clock::time_point wall_now() const override { return {}; }
    void advance(milliseconds d) {
        ns_.fetch_add(std::chrono::duration_cast<std::chrono::nanoseconds>(d).count());
    }

private:
    std::atomic<std::int64_t> ns_{0};
};

class RecordingHandler final : public IHsmsHandler {
public:
    void on_data(const Delivery& d) override {
        std::lock_guard lock(mutex_);
        deliveries.push_back(d);
        cv_.notify_all();
    }
    void on_session_state(ConnectionState s) override {
        std::lock_guard lock(mutex_);
        states.push_back(s);
        cv_.notify_all();
    }
    void on_error(const std::string& what) override {
        std::lock_guard lock(mutex_);
        errors.push_back(what);
        cv_.notify_all();
    }

    template <typename Pred>
    bool wait(Pred pred) {
        std::unique_lock lock(mutex_);
        return cv_.wait_for(lock, seconds(10), pred);
    }
    bool wait_state(ConnectionState s, std::size_t count = 1) {
        return wait([&] {
            std::size_t n = 0;
            for (auto x : states) n += x == s ? 1 : 0;
            return n >= count;
        });
    }

    std::vector<Delivery> deliveries;
    std::vector<ConnectionState> states;
    std::vector<std::string> errors;

private:
    std::mutex mutex_;
    std::condition_variable cv_;
};

// A blocking-style test client built on Asio, with bounded waits.
class TestClient {
public:
    explicit TestClient(std::uint16_t port) : socket_(io_), decoder_(1 << 20) {
        socket_.connect(tcp::endpoint(asio::ip::address_v4::loopback(), port));
    }

    void send(const Frame& f) { send_bytes(encode_frame(f).value()); }
    void send_bytes(const Bytes& b) { asio::write(socket_, asio::buffer(b)); }

    // Next frame, or nullopt on timeout or when the server closed the connection.
    std::optional<Frame> read_frame(milliseconds timeout = seconds(10)) {
        const auto end = std::chrono::steady_clock::now() + timeout;
        while (pending_.empty()) {
            const auto left =
                std::chrono::duration_cast<milliseconds>(end - std::chrono::steady_clock::now());
            if (left.count() <= 0 || !read_more(left)) return std::nullopt;
        }
        Frame f = std::move(pending_.front());
        pending_.pop_front();
        return f;
    }

    // True once the server has closed the connection.
    bool wait_closed(milliseconds timeout = seconds(10)) {
        const auto end = std::chrono::steady_clock::now() + timeout;
        while (!closed_) {
            const auto left =
                std::chrono::duration_cast<milliseconds>(end - std::chrono::steady_clock::now());
            if (left.count() <= 0) return false;
            read_more(left);
        }
        return true;
    }

private:
    // Reads what is available; returns false on timeout or close.
    bool read_more(milliseconds timeout) {
        std::array<std::uint8_t, 2048> buf{};
        std::error_code result;
        std::size_t got = 0;
        bool done = false;
        socket_.async_read_some(asio::buffer(buf), [&](std::error_code ec, std::size_t n) {
            result = ec;
            got = n;
            done = true;
        });
        io_.restart();
        io_.run_for(timeout);
        if (!done) {
            socket_.cancel();
            io_.restart();
            io_.poll();
            return false;
        }
        if (result) {
            closed_ = true;
            return false;
        }
        auto frames = decoder_.feed(ByteSpan(buf.data(), got));
        if (frames) {
            for (auto& f : frames.value()) pending_.push_back(std::move(f));
        }
        return true;
    }

    asio::io_context io_;
    tcp::socket socket_;
    FrameDecoder decoder_;
    std::deque<Frame> pending_;
    bool closed_ = false;
};

ServerConfig test_server_config() {
    ServerConfig c;
    c.port = 0;  // let the OS choose
    c.tick_interval = milliseconds(5);
    c.session.t7 = Duration(4000);
    c.session.t8 = Duration(1000);
    c.session.linktest_interval = Duration(600000);  // out of the way
    c.session.max_frame_bytes = 4096;
    c.session.device_id = 7;
    return c;
}

struct Fixture {
    AtomicClock clock;
    RecordingHandler handler;
    HsmsServer server;

    Fixture() : server(test_server_config(), clock, handler) {
        auto r = server.start();
        EXPECT_TRUE(r) << (r ? "" : r.error().message);
    }
};

// Connects a client and completes the Select handshake.
void select(TestClient& client, std::uint32_t system = 1) {
    client.send(make_control_frame(SType::kSelectReq, system));
    auto rsp = client.read_frame();
    ASSERT_TRUE(rsp.has_value());
    ASSERT_EQ(rsp->header.stype, static_cast<std::uint8_t>(SType::kSelectRsp));
    ASSERT_EQ(rsp->header.byte3, kSelectOk);
}

TEST(HsmsServer, ListensOnLoopbackByDefault) {
    EXPECT_EQ(ServerConfig().bind, "127.0.0.1");
    EXPECT_EQ(ServerConfig().port, 5000);
    EXPECT_EQ(server_config_from(ssim::core::CommConfig()).bind, "127.0.0.1");
}

TEST(HsmsServer, SelectThenLinktestRoundTrip) {
    Fixture f;
    ASSERT_NE(f.server.local_port(), 0);
    TestClient client(f.server.local_port());
    ASSERT_TRUE(f.handler.wait_state(ConnectionState::kNotSelected));

    select(client, 11);
    ASSERT_TRUE(f.handler.wait_state(ConnectionState::kSelected));

    client.send(make_control_frame(SType::kLinktestReq, 12));
    auto rsp = client.read_frame();
    ASSERT_TRUE(rsp.has_value());
    EXPECT_EQ(rsp->header.stype, static_cast<std::uint8_t>(SType::kLinktestRsp));
    EXPECT_EQ(rsp->header.system_bytes, 12u);
}

TEST(HsmsServer, DataMessageIsDeliveredAndTheReplyComesBackWithTheSameSystemBytes) {
    Fixture f;
    TestClient client(f.server.local_port());
    select(client);

    const secs2::Item body = secs2::Item::list({});
    client.send(make_data_frame(7, 1, 13, true, 0xCAFE, secs2::encode(body).value()));
    ASSERT_TRUE(f.handler.wait([&] { return !f.handler.deliveries.empty(); }));
    auto message = to_message(f.handler.deliveries[0].frame);
    ASSERT_TRUE(message);
    EXPECT_EQ(message.value().stream, 1);
    EXPECT_EQ(message.value().function, 13);
    EXPECT_EQ(f.handler.deliveries[0].frame.header.system_bytes, 0xCAFEu);

    secs2::Message reply;
    reply.stream = 1;
    reply.function = 14;
    reply.body = secs2::Item::list({secs2::Item::binary({0})});
    f.server.post_reply(0xCAFE, reply);
    auto got = client.read_frame();
    ASSERT_TRUE(got.has_value());
    EXPECT_EQ(got->header.system_bytes, 0xCAFEu);
    EXPECT_EQ(got->header.stream(), 1);
    EXPECT_EQ(got->header.function(), 14);
    EXPECT_FALSE(got->header.w_bit());
}

TEST(HsmsServer, MachineRequestIsMatchedWithTheHostsReply) {
    Fixture f;
    TestClient client(f.server.local_port());
    select(client);
    ASSERT_TRUE(f.handler.wait_state(ConnectionState::kSelected));

    secs2::Message request;
    request.stream = 6;
    request.function = 11;
    request.w_bit = true;
    request.body = secs2::Item::list({secs2::Item::u4(std::uint32_t{2005})});
    f.server.post_request(request);

    auto sent = client.read_frame();
    ASSERT_TRUE(sent.has_value());
    EXPECT_TRUE(sent->header.w_bit());
    EXPECT_EQ(sent->header.session_id, 7);

    client.send(make_data_frame(7, 6, 12, false, sent->header.system_bytes, {}));
    ASSERT_TRUE(f.handler.wait([&] { return !f.handler.deliveries.empty(); }));
    ASSERT_TRUE(f.handler.deliveries[0].reply_to.has_value());
    EXPECT_EQ(*f.handler.deliveries[0].reply_to, sent->header.system_bytes);
}

TEST(HsmsServer, SecondConnectionIsRefusedWhileASessionIsActiveAndTheFirstKeepsWorking) {
    Fixture f;
    TestClient first(f.server.local_port());
    select(first);

    TestClient second(f.server.local_port());  // TCP connects, then the server closes it
    EXPECT_TRUE(second.wait_closed());

    first.send(make_control_frame(SType::kLinktestReq, 5));
    auto rsp = first.read_frame();
    ASSERT_TRUE(rsp.has_value());
    EXPECT_EQ(rsp->header.stype, static_cast<std::uint8_t>(SType::kLinktestRsp));
}

TEST(HsmsServer, ANewHostCanConnectAfterTheFirstOneLeaves) {
    Fixture f;
    {
        TestClient first(f.server.local_port());
        select(first);
        ASSERT_TRUE(f.handler.wait_state(ConnectionState::kSelected));
    }  // client socket closed
    ASSERT_TRUE(f.handler.wait_state(ConnectionState::kNotConnected));

    TestClient again(f.server.local_port());
    select(again, 2);
}

TEST(HsmsServer, OversizedFrameClosesTheConnection) {
    Fixture f;
    TestClient client(f.server.local_port());
    select(client);
    client.send_bytes(Bytes{0x00, 0x01, 0x00, 0x00});  // 65,536 > 4096
    EXPECT_TRUE(client.wait_closed());
    ASSERT_TRUE(f.handler.wait_state(ConnectionState::kNotConnected));
}

TEST(HsmsServer, T7ClosesAConnectionThatNeverSelectsWhenTheClockAdvances) {
    Fixture f;
    TestClient client(f.server.local_port());
    ASSERT_TRUE(f.handler.wait_state(ConnectionState::kNotSelected));

    f.clock.advance(milliseconds(3999));
    EXPECT_FALSE(client.wait_closed(milliseconds(150)));  // a few ticks pass, still open
    f.clock.advance(milliseconds(1));
    EXPECT_TRUE(client.wait_closed());
    ASSERT_TRUE(f.handler.wait_state(ConnectionState::kNotConnected));
}

TEST(HsmsServer, SeparateReqClosesTheConnection) {
    Fixture f;
    TestClient client(f.server.local_port());
    select(client);
    client.send(make_control_frame(SType::kSeparateReq, 3));
    EXPECT_TRUE(client.wait_closed());
}

TEST(HsmsServer, StopWhileAHostIsConnectedFinishesQuicklyAndClosesIt) {
    Fixture f;
    TestClient client(f.server.local_port());
    select(client);

    const auto begin = std::chrono::steady_clock::now();
    f.server.stop();
    EXPECT_LT(std::chrono::steady_clock::now() - begin, seconds(2));
    EXPECT_TRUE(client.wait_closed());
    f.server.stop();  // calling it again is harmless
}

TEST(HsmsServer, SendWithoutAHostIsReportedNotCrashed) {
    Fixture f;
    secs2::Message m;
    m.stream = 1;
    m.function = 1;
    f.server.post_request(m);
    ASSERT_TRUE(f.handler.wait([&] { return !f.handler.errors.empty(); }));
}

TEST(HsmsServer, BadBindAddressAndDoubleStartAreValues) {
    AtomicClock clock;
    RecordingHandler handler;
    ServerConfig bad = test_server_config();
    bad.bind = "not-an-address";
    HsmsServer broken(bad, clock, handler);
    auto r = broken.start();
    ASSERT_FALSE(r);
    EXPECT_EQ(r.error().code, kErrBadBindAddress);

    HsmsServer ok(test_server_config(), clock, handler);
    ASSERT_TRUE(ok.start());
    auto again = ok.start();
    ASSERT_FALSE(again);
    EXPECT_EQ(again.error().code, kErrAlreadyStarted);
}

}  // namespace
}  // namespace ssim::secsgem::hsms
