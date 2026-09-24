#pragma once

// Thread-safety: Not thread-safe; owned by the session, which is driven from
// one thread only.
//
// FR-HSMS-2: turns a stream of bytes into frames. TCP gives no message
// boundaries, so bytes arrive in arbitrary pieces: a frame may be split over
// several reads and one read may hold several frames. The length field is
// checked as soon as its four bytes are in, so a hostile length can never make
// the decoder buffer more than one maximum-size frame (NFR-SEC-1).
//
// A framing error is fatal for the connection: the decoder stays failed and
// reports the same error, and the caller must close the connection.

#include <cstddef>
#include <cstdint>
#include <vector>

#include "ssim/core/result.hpp"
#include "ssim/secsgem/bytes.hpp"
#include "ssim/secsgem/hsms/frame.hpp"

namespace ssim::secsgem::hsms {

class FrameDecoder {
public:
    // max_frame_bytes limits the length field (header plus body).
    explicit FrameDecoder(std::size_t max_frame_bytes) : max_frame_bytes_(max_frame_bytes) {}

    // Adds bytes and returns every frame they completed, in order (possibly
    // none). On error nothing further is decoded.
    [[nodiscard]] ssim::core::Result<std::vector<Frame>> feed(ByteSpan bytes);

    // True while part of an unfinished frame is buffered. The T8 timer runs
    // exactly while this is true.
    bool has_partial() const { return !buffer_.empty(); }

    bool failed() const { return failed_; }

private:
    std::size_t max_frame_bytes_;
    std::vector<std::uint8_t> buffer_;
    bool failed_ = false;
    ssim::core::Error error_{0, {}};
};

}  // namespace ssim::secsgem::hsms
