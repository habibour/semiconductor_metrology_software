#include "ssim/secsgem/hsms/frame.hpp"

#include <limits>

namespace ssim::secsgem::hsms {

namespace {

void put_be16(std::uint16_t v, std::vector<std::uint8_t>& out) {
    out.push_back(static_cast<std::uint8_t>(v >> 8));
    out.push_back(static_cast<std::uint8_t>(v & 0xFF));
}

void put_be32(std::uint32_t v, std::vector<std::uint8_t>& out) {
    for (int shift = 24; shift >= 0; shift -= 8) {
        out.push_back(static_cast<std::uint8_t>((v >> shift) & 0xFF));
    }
}

}  // namespace

const char* to_string(SType stype) {
    switch (stype) {
        case SType::kData:
            return "Data";
        case SType::kSelectReq:
            return "Select.req";
        case SType::kSelectRsp:
            return "Select.rsp";
        case SType::kDeselectReq:
            return "Deselect.req";
        case SType::kDeselectRsp:
            return "Deselect.rsp";
        case SType::kLinktestReq:
            return "Linktest.req";
        case SType::kLinktestRsp:
            return "Linktest.rsp";
        case SType::kRejectReq:
            return "Reject.req";
        case SType::kSeparateReq:
            return "Separate.req";
    }
    return "?";
}

std::optional<SType> stype_from_byte(std::uint8_t byte) {
    switch (byte) {
        case 0:
            return SType::kData;
        case 1:
            return SType::kSelectReq;
        case 2:
            return SType::kSelectRsp;
        case 3:
            return SType::kDeselectReq;
        case 4:
            return SType::kDeselectRsp;
        case 5:
            return SType::kLinktestReq;
        case 6:
            return SType::kLinktestRsp;
        case 7:
            return SType::kRejectReq;
        case 9:
            return SType::kSeparateReq;
        default:
            return std::nullopt;
    }
}

Frame make_control_frame(SType stype, std::uint32_t system_bytes, std::uint8_t byte2,
                         std::uint8_t byte3) {
    Frame frame;
    frame.header.session_id = kControlSessionId;
    frame.header.byte2 = byte2;
    frame.header.byte3 = byte3;
    frame.header.ptype = 0;
    frame.header.stype = static_cast<std::uint8_t>(stype);
    frame.header.system_bytes = system_bytes;
    return frame;
}

Frame make_data_frame(std::uint16_t session_id, std::uint8_t stream, std::uint8_t function,
                      bool w_bit, std::uint32_t system_bytes, std::vector<std::uint8_t> body) {
    Frame frame;
    frame.header.session_id = session_id;
    frame.header.byte2 = static_cast<std::uint8_t>((stream & 0x7FU) | (w_bit ? 0x80U : 0U));
    frame.header.byte3 = function;
    frame.header.ptype = 0;
    frame.header.stype = static_cast<std::uint8_t>(SType::kData);
    frame.header.system_bytes = system_bytes;
    frame.body = std::move(body);
    return frame;
}

ssim::core::Result<std::vector<std::uint8_t>> encode_frame(const Frame& frame) {
    using R = ssim::core::Result<std::vector<std::uint8_t>>;
    const std::size_t length = kHeaderSize + frame.body.size();
    if (length > std::numeric_limits<std::uint32_t>::max()) {
        return R::err({kErrFrameTooBig, "frame does not fit a 32-bit length"});
    }
    std::vector<std::uint8_t> out;
    out.reserve(kLengthFieldSize + length);
    put_be32(static_cast<std::uint32_t>(length), out);
    put_be16(frame.header.session_id, out);
    out.push_back(frame.header.byte2);
    out.push_back(frame.header.byte3);
    out.push_back(frame.header.ptype);
    out.push_back(frame.header.stype);
    put_be32(frame.header.system_bytes, out);
    out.insert(out.end(), frame.body.begin(), frame.body.end());
    return R::ok(std::move(out));
}

}  // namespace ssim::secsgem::hsms
