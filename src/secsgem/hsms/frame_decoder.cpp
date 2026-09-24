#include "ssim/secsgem/hsms/frame_decoder.hpp"

#include <utility>

namespace ssim::secsgem::hsms {

namespace {

std::uint32_t load_be32(const std::uint8_t* p) {
    return (static_cast<std::uint32_t>(p[0]) << 24) | (static_cast<std::uint32_t>(p[1]) << 16) |
           (static_cast<std::uint32_t>(p[2]) << 8) | static_cast<std::uint32_t>(p[3]);
}

std::uint16_t load_be16(const std::uint8_t* p) {
    return static_cast<std::uint16_t>((static_cast<unsigned>(p[0]) << 8) | p[1]);
}

}  // namespace

ssim::core::Result<std::vector<Frame>> FrameDecoder::feed(ByteSpan bytes) {
    using R = ssim::core::Result<std::vector<Frame>>;
    if (failed_) {
        return R::err(error_);
    }
    if (bytes.size > 0) {
        buffer_.insert(buffer_.end(), bytes.data, bytes.data + bytes.size);
    }

    std::vector<Frame> frames;
    std::size_t offset = 0;
    while (buffer_.size() - offset >= kLengthFieldSize) {
        const std::uint8_t* p = buffer_.data() + offset;
        const std::uint32_t length = load_be32(p);
        // Validate the moment the length is known, before waiting for the body.
        if (length < kHeaderSize) {
            failed_ = true;
            error_ = {kErrFrameTooShort, "frame length is below the 10-byte header"};
            return R::err(error_);
        }
        if (length > max_frame_bytes_) {
            failed_ = true;
            error_ = {kErrFrameTooLong, "frame length is above the configured maximum"};
            return R::err(error_);
        }
        if (buffer_.size() - offset < kLengthFieldSize + length) {
            break;  // wait for the rest
        }
        const std::uint8_t* h = p + kLengthFieldSize;
        Frame frame;
        frame.header.session_id = load_be16(h);
        frame.header.byte2 = h[2];
        frame.header.byte3 = h[3];
        frame.header.ptype = h[4];
        frame.header.stype = h[5];
        frame.header.system_bytes = load_be32(h + 6);
        frame.body.assign(h + kHeaderSize, h + length);
        frames.push_back(std::move(frame));
        offset += kLengthFieldSize + length;
    }
    buffer_.erase(buffer_.begin(), buffer_.begin() + static_cast<std::ptrdiff_t>(offset));
    return R::ok(std::move(frames));
}

}  // namespace ssim::secsgem::hsms
