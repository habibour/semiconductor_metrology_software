#include "ssim/host_sim/host_client.hpp"

#include <array>
#include <asio.hpp>

#include "ssim/secsgem/hsms/frame_decoder.hpp"

namespace ssim::host_sim {

using asio::ip::tcp;

struct HostClient::Impl {
    Impl() : socket(io), decoder(kMaxFrame) {}

    // Large enough for any message the machine sends; still bounded so a
    // hostile length cannot make the simulator buffer without limit.
    static constexpr std::size_t kMaxFrame = 16 * 1024 * 1024;

    asio::io_context io;
    tcp::socket socket;
    ssim::secsgem::hsms::FrameDecoder decoder;
    bool closed = false;
};

HostClient::HostClient() : impl_(std::make_unique<Impl>()) {}
HostClient::~HostClient() { close(); }

ssim::core::Result<bool> HostClient::connect(const std::string& host, std::uint16_t port) {
    using R = ssim::core::Result<bool>;
    close();
    impl_ = std::make_unique<Impl>();
    std::error_code ec;
    const asio::ip::address address = asio::ip::make_address(host, ec);
    if (ec) {
        return R::err({kErrConnectFailed, "bad host address '" + host + "'"});
    }
    impl_->socket.connect(tcp::endpoint(address, port), ec);
    if (ec) {
        return R::err({kErrConnectFailed, "cannot connect to " + host + ":" + std::to_string(port) +
                                              ": " + ec.message()});
    }
    std::error_code ignored;
    impl_->socket.set_option(tcp::no_delay(true), ignored);
    return R::ok(true);
}

void HostClient::close() {
    if (impl_ && impl_->socket.is_open()) {
        std::error_code ignored;
        impl_->socket.shutdown(tcp::socket::shutdown_both, ignored);
        impl_->socket.close(ignored);
    }
}

bool HostClient::is_open() const { return impl_ && impl_->socket.is_open() && !impl_->closed; }

ssim::core::Result<bool> HostClient::send_bytes(const std::vector<std::uint8_t>& bytes) {
    using R = ssim::core::Result<bool>;
    if (!impl_->socket.is_open()) {
        return R::err({kErrSendFailed, "not connected"});
    }
    std::error_code ec;
    asio::write(impl_->socket, asio::buffer(bytes), ec);
    if (ec) {
        return R::err({kErrSendFailed, "send failed: " + ec.message()});
    }
    return R::ok(true);
}

HostClient::ReadStatus HostClient::read(std::chrono::milliseconds timeout,
                                        std::vector<ssim::secsgem::hsms::Frame>& out) {
    Impl& d = *impl_;
    if (!d.socket.is_open() || d.closed) {
        return ReadStatus::kClosed;
    }
    std::array<std::uint8_t, 4096> buffer{};
    std::error_code result;
    std::size_t got = 0;
    bool done = false;
    d.socket.async_read_some(asio::buffer(buffer), [&](std::error_code ec, std::size_t n) {
        result = ec;
        got = n;
        done = true;
    });
    d.io.restart();
    d.io.run_for(timeout);
    if (!done) {
        d.socket.cancel();  // the handler runs with "aborted" before poll() returns
        d.io.restart();
        d.io.poll();
        return ReadStatus::kTimeout;
    }
    if (result) {
        d.closed = true;
        return ReadStatus::kClosed;
    }
    auto frames = d.decoder.feed(ssim::secsgem::ByteSpan(buffer.data(), got));
    if (!frames) {
        d.closed = true;
        return ReadStatus::kClosed;
    }
    for (auto& f : frames.value()) {
        out.push_back(std::move(f));
    }
    return ReadStatus::kFrames;
}

}  // namespace ssim::host_sim
