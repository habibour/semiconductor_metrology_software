#include "ssim/secsgem/hsms/server.hpp"

#include <array>
#include <asio.hpp>
#include <atomic>
#include <deque>
#include <exception>
#include <optional>
#include <thread>
#include <utility>
#include <vector>

#include "ssim/core/logger.hpp"

namespace ssim::secsgem::hsms {

using asio::ip::tcp;

ServerConfig server_config_from(const ssim::core::CommConfig& comm) {
    ServerConfig config;
    config.bind = comm.bind;
    config.port = static_cast<std::uint16_t>(comm.port);
    config.session = session_config_from(comm);
    return config;
}

struct HsmsServer::Impl {
    Impl(ServerConfig c, ssim::core::IClock& clk, IHsmsHandler& h)
        : config(std::move(c)),
          clock(clk),
          handler(h),
          acceptor(io),
          tick_timer(io),
          session(config.session) {}

    ServerConfig config;
    ssim::core::IClock& clock;
    IHsmsHandler& handler;

    asio::io_context io;
    tcp::acceptor acceptor;
    asio::steady_timer tick_timer;
    std::optional<tcp::socket> socket;  // the one active connection, if any
    Session session;
    std::array<std::uint8_t, 4096> read_buffer{};
    std::deque<std::vector<std::uint8_t>> write_queue;
    bool writing = false;
    // Bumped whenever a connection ends. A completion handler that belongs to
    // an older connection sees a different number and does nothing, so a late
    // read or write result can never touch a newer connection.
    std::uint64_t generation = 0;
    ConnectionState notified_state = ConnectionState::kNotConnected;

    std::thread thread;
    std::atomic<std::uint16_t> port{0};
    bool started = false;

    void accept_next();
    void schedule_tick();
    void read_next();
    void write_next();
    void apply(SessionOutput& out);
    void notify_state();
    void close_socket();
    void run_thread();
};

void HsmsServer::Impl::notify_state() {
    if (session.state() != notified_state) {
        notified_state = session.state();
        handler.on_session_state(notified_state);
    }
}

void HsmsServer::Impl::close_socket() {
    if (socket.has_value()) {
        std::error_code ignored;
        socket->shutdown(tcp::socket::shutdown_both, ignored);
        socket->close(ignored);
        socket.reset();
    }
    ++generation;
    write_queue.clear();
    writing = false;
    session.on_disconnect();
    notify_state();
}

void HsmsServer::Impl::apply(SessionOutput& out) {
    for (auto& bytes : out.to_send) {
        write_queue.push_back(std::move(bytes));
    }
    if (!out.close && !write_queue.empty() && !writing) {
        write_next();
    }
    for (const Delivery& delivery : out.deliveries) {
        handler.on_data(delivery);
    }
    for (std::uint32_t system_bytes : out.timed_out) {
        handler.on_transaction_timeout(system_bytes);
    }
    notify_state();
    if (out.close) {
        close_socket();
    }
}

void HsmsServer::Impl::write_next() {
    if (!socket.has_value() || write_queue.empty()) {
        writing = false;
        return;
    }
    writing = true;
    const std::uint64_t gen = generation;
    // The front element stays in the queue while the write is in flight, so
    // its memory is valid until the completion handler pops it.
    asio::async_write(*socket, asio::buffer(write_queue.front()),
                      [this, gen](std::error_code ec, std::size_t) {
                          if (gen != generation) {
                              return;
                          }
                          if (ec) {
                              handler.on_error("write failed: " + ec.message());
                              close_socket();
                              return;
                          }
                          write_queue.pop_front();
                          write_next();
                      });
}

void HsmsServer::Impl::read_next() {
    if (!socket.has_value()) {
        return;
    }
    const std::uint64_t gen = generation;
    socket->async_read_some(
        asio::buffer(read_buffer), [this, gen](std::error_code ec, std::size_t n) {
            if (gen != generation) {
                return;
            }
            if (ec) {
                close_socket();  // the host closed the connection or it failed
                return;
            }
            SessionOutput out =
                session.on_bytes(clock.monotonic_now(), ByteSpan(read_buffer.data(), n));
            apply(out);
            if (socket.has_value() && gen == generation) {
                read_next();
            }
        });
}

void HsmsServer::Impl::accept_next() {
    acceptor.async_accept([this](std::error_code ec, tcp::socket accepted) {
        if (ec == asio::error::operation_aborted) {
            return;  // shutting down
        }
        if (!ec) {
            if (socket.has_value()) {
                // FR-HSMS-6: one host session at a time; refuse the second.
                std::error_code ignored;
                accepted.close(ignored);
            } else {
                std::error_code ignored;
                accepted.set_option(tcp::no_delay(true), ignored);
                socket.emplace(std::move(accepted));
                ++generation;
                SessionOutput out = session.on_connect(clock.monotonic_now());
                apply(out);
                read_next();
            }
        }
        accept_next();  // keep listening, also after a transient accept error
    });
}

void HsmsServer::Impl::schedule_tick() {
    tick_timer.expires_after(config.tick_interval);
    tick_timer.async_wait([this](std::error_code ec) {
        if (ec) {
            return;  // cancelled on shutdown
        }
        if (socket.has_value()) {
            SessionOutput out = session.on_tick(clock.monotonic_now());
            apply(out);
        }
        schedule_tick();
    });
}

void HsmsServer::Impl::run_thread() {
    ssim::core::set_current_thread_name("hsms_io");
    try {
        io.run();
    } catch (const std::exception& e) {
        handler.on_error(std::string("I/O thread failed: ") + e.what());
    } catch (...) {
        handler.on_error("I/O thread failed with an unknown exception");
    }
}

HsmsServer::HsmsServer(ServerConfig config, ssim::core::IClock& clock, IHsmsHandler& handler)
    : impl_(std::make_unique<Impl>(std::move(config), clock, handler)) {}

HsmsServer::~HsmsServer() { stop(); }

ssim::core::Result<bool> HsmsServer::start() {
    using R = ssim::core::Result<bool>;
    Impl& d = *impl_;
    if (d.started) {
        return R::err({kErrAlreadyStarted, "server already started"});
    }
    std::error_code ec;
    const asio::ip::address address = asio::ip::make_address(d.config.bind, ec);
    if (ec) {
        return R::err({kErrBadBindAddress, "bad bind address '" + d.config.bind + "'"});
    }
    const tcp::endpoint endpoint(address, d.config.port);
    d.acceptor.open(endpoint.protocol(), ec);
    if (!ec) d.acceptor.set_option(tcp::acceptor::reuse_address(true), ec);
    if (!ec) d.acceptor.bind(endpoint, ec);
    if (!ec) d.acceptor.listen(asio::socket_base::max_listen_connections, ec);
    if (ec) {
        std::error_code ignored;
        d.acceptor.close(ignored);
        return R::err({kErrListenFailed, "cannot listen on " + d.config.bind + ":" +
                                             std::to_string(d.config.port) + ": " + ec.message()});
    }
    d.port.store(d.acceptor.local_endpoint().port());

    d.accept_next();
    d.schedule_tick();
    d.started = true;
    d.thread = std::thread([&d] { d.run_thread(); });
    return R::ok(true);
}

void HsmsServer::stop() {
    Impl& d = *impl_;
    if (!d.started) {
        return;
    }
    asio::post(d.io, [&d] {
        std::error_code ignored;
        d.acceptor.close(ignored);
        d.tick_timer.cancel();
        d.close_socket();
        d.io.stop();
    });
    if (d.thread.joinable()) {
        d.thread.join();
    }
    d.started = false;
}

std::uint16_t HsmsServer::local_port() const { return impl_->port.load(); }

void HsmsServer::post_request(secs2::Message message) {
    Impl& d = *impl_;
    asio::post(d.io, [&d, message = std::move(message)] {
        if (!d.socket.has_value()) {
            d.handler.on_error("request dropped: no host connected");
            return;
        }
        SessionOutput out;
        auto sent = d.session.send_request(d.clock.monotonic_now(), message, out);
        if (!sent) {
            d.handler.on_error("request not sent: " + sent.error().message);
        }
        d.apply(out);
    });
}

void HsmsServer::post_reply(std::uint32_t system_bytes, secs2::Message message) {
    Impl& d = *impl_;
    asio::post(d.io, [&d, system_bytes, message = std::move(message)] {
        if (!d.socket.has_value()) {
            d.handler.on_error("reply dropped: no host connected");
            return;
        }
        SessionOutput out;
        auto sent = d.session.send_reply(system_bytes, message, out);
        if (!sent) {
            d.handler.on_error("reply not sent: " + sent.error().message);
        }
        d.apply(out);
    });
}

}  // namespace ssim::secsgem::hsms
