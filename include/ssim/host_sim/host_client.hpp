#pragma once

// Thread-safety: Not thread-safe; used from the script runner's thread only.
//
// A minimal active HSMS endpoint for the host simulator: it connects, sends
// raw bytes and reads frames with a bounded wait. All protocol decisions
// (select, acknowledging events) belong to the runner, so a script can also do
// deliberately wrong things. Asio is used only in host_client.cpp.

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "ssim/core/result.hpp"
#include "ssim/secsgem/hsms/frame.hpp"

namespace ssim::host_sim {

constexpr int kErrConnectFailed = 602;
constexpr int kErrSendFailed = 603;

class HostClient {
public:
    enum class ReadStatus { kFrames, kTimeout, kClosed };

    HostClient();
    ~HostClient();
    HostClient(const HostClient&) = delete;
    HostClient& operator=(const HostClient&) = delete;

    [[nodiscard]] ssim::core::Result<bool> connect(const std::string& host, std::uint16_t port);
    void close();
    bool is_open() const;

    [[nodiscard]] ssim::core::Result<bool> send_bytes(const std::vector<std::uint8_t>& bytes);

    // Waits up to `timeout` for more data and appends every frame it
    // completes. kClosed means the peer closed the connection or sent bytes
    // that are not a valid frame stream.
    ReadStatus read(std::chrono::milliseconds timeout,
                    std::vector<ssim::secsgem::hsms::Frame>& out);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace ssim::host_sim
