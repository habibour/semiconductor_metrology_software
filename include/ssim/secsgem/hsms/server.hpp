#pragma once

// Thread-safety: start(), stop() and the destructor are for one owner thread.
// local_port(), post_request() and post_reply() may be called from any thread;
// the work is handed to the I/O thread. Everything else (the socket, the
// Session, the frame decoder) lives on that one thread, so it needs no locks.
//
// FR-HSMS-1/6, NFR-SEC-1. The machine is the passive HSMS entity: it listens
// and the host connects. It binds comm.bind:comm.port (default
// 127.0.0.1:5000, loopback only) and serves one session at a time: while a
// host is connected, another connection is accepted and closed at once. When
// the host goes away the server is already listening again, so a new host
// can connect immediately (FR-HSMS-7).
//
// Time comes from the injected IClock (rule C8), read on the I/O thread, so
// the clock must be safe to call from any thread. A steady timer wakes the
// thread about every tick_interval to let the session check its timers.
//
// Shutdown: stop() closes the listener and the connection, stops the I/O
// loop and joins the thread. No detached threads; exceptions never leave the
// I/O thread (they are reported through IHsmsHandler::on_error).
//
// Asio is used only inside src/secsgem/hsms/server.cpp; nothing in this
// header includes it.

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>

#include "ssim/core/clock.hpp"
#include "ssim/core/config.hpp"
#include "ssim/core/result.hpp"
#include "ssim/secsgem/hsms/handler.hpp"
#include "ssim/secsgem/hsms/sender.hpp"
#include "ssim/secsgem/hsms/session.hpp"
#include "ssim/secsgem/secs2/message.hpp"

namespace ssim::secsgem::hsms {

constexpr int kErrBadBindAddress = 420;
constexpr int kErrListenFailed = 421;
constexpr int kErrAlreadyStarted = 422;

struct ServerConfig {
    std::string bind = "127.0.0.1";
    std::uint16_t port = 5000;  // 0 lets the OS pick; read it back with local_port()
    SessionConfig session;
    std::chrono::milliseconds tick_interval{20};
};

ServerConfig server_config_from(const ssim::core::CommConfig& comm);

class HsmsServer final : public IMessageSender {
public:
    HsmsServer(ServerConfig config, ssim::core::IClock& clock, IHsmsHandler& handler);
    ~HsmsServer() override;

    HsmsServer(const HsmsServer&) = delete;
    HsmsServer& operator=(const HsmsServer&) = delete;

    // Binds, listens and starts the I/O thread. Errors (bad address, port in
    // use) come back as values.
    [[nodiscard]] ssim::core::Result<bool> start();

    // Safe to call more than once, and if start() never succeeded.
    void stop();

    // The port actually bound (useful with port 0). Zero before start().
    std::uint16_t local_port() const;

    // Sends a message to the connected host, if there is one and it is
    // selected. Failures are reported through IHsmsHandler::on_error.
    void post_request(secs2::Message message,
                      std::function<void(std::uint32_t)> on_sent = {}) override;
    void post_reply(std::uint32_t system_bytes, secs2::Message message) override;
    void post_task(std::function<void()> task) override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace ssim::secsgem::hsms
