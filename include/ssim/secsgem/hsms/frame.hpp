#pragma once

// Thread-safety: plain value types and pure functions; safe anywhere.
//
// FR-HSMS-2 / PRD 8.6.1. A frame is a 4-byte big-endian length, a 10-byte
// header, then the body; the length counts the header and body. Header, in
// order: session id (2), byte 2, byte 3, PType (1), SType (1), system bytes
// (4). For a data message byte 2 is the W-bit (top bit) plus the stream and
// byte 3 is the function; for Reject.req byte 2 is the SType of the rejected
// message and byte 3 the reason. Layout and SType values were cross-checked
// against secsgem 0.3.0 (docs/protocol-notes.md).

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include "ssim/core/result.hpp"

namespace ssim::secsgem::hsms {

constexpr std::size_t kLengthFieldSize = 4;
constexpr std::size_t kHeaderSize = 10;
constexpr std::uint16_t kControlSessionId = 0xFFFF;  // Select, Deselect, Linktest, Reject

enum class SType : std::uint8_t {
    kData = 0,
    kSelectReq = 1,
    kSelectRsp = 2,
    kDeselectReq = 3,
    kDeselectRsp = 4,
    kLinktestReq = 5,
    kLinktestRsp = 6,
    kRejectReq = 7,
    kSeparateReq = 9,
};

const char* to_string(SType stype);

// The SType a raw header byte stands for, or nullopt for a value HSMS does not
// define (the session answers those with Reject.req).
std::optional<SType> stype_from_byte(std::uint8_t byte);

struct Header {
    std::uint16_t session_id = 0;
    std::uint8_t byte2 = 0;
    std::uint8_t byte3 = 0;
    std::uint8_t ptype = 0;  // 0 = SECS-II
    std::uint8_t stype = 0;  // raw; see stype_from_byte()
    std::uint32_t system_bytes = 0;

    // Data-message views of byte 2 and byte 3.
    bool w_bit() const { return (byte2 & 0x80U) != 0; }
    std::uint8_t stream() const { return static_cast<std::uint8_t>(byte2 & 0x7FU); }
    std::uint8_t function() const { return byte3; }

    friend bool operator==(const Header& a, const Header& b) {
        return a.session_id == b.session_id && a.byte2 == b.byte2 && a.byte3 == b.byte3 &&
               a.ptype == b.ptype && a.stype == b.stype && a.system_bytes == b.system_bytes;
    }
};

struct Frame {
    Header header;
    std::vector<std::uint8_t> body;

    friend bool operator==(const Frame& a, const Frame& b) {
        return a.header == b.header && a.body == b.body;
    }
};

// Control message (Select, Deselect, Linktest, Reject, Separate): session id
// 0xFFFF, no body. byte2 and byte3 carry a status or a reject SType/reason.
Frame make_control_frame(SType stype, std::uint32_t system_bytes, std::uint8_t byte2 = 0,
                         std::uint8_t byte3 = 0);

Frame make_data_frame(std::uint16_t session_id, std::uint8_t stream, std::uint8_t function,
                      bool w_bit, std::uint32_t system_bytes, std::vector<std::uint8_t> body);

// Stable reason codes (tests assert on them).
constexpr int kErrFrameTooShort = 400;  // length field below the 10-byte header
constexpr int kErrFrameTooLong = 401;   // length field above max_frame_bytes
constexpr int kErrFrameTooBig = 402;    // encode: does not fit the 32-bit length
constexpr int kErrNotADataFrame = 403;  // to_message() on a control frame

[[nodiscard]] ssim::core::Result<std::vector<std::uint8_t>> encode_frame(const Frame& frame);

// The 10 header bytes on their own, as carried in the S9 error messages
// (MHEAD/SHEAD, PRD 8.6.3).
std::vector<std::uint8_t> header_bytes(const Header& header);

}  // namespace ssim::secsgem::hsms
