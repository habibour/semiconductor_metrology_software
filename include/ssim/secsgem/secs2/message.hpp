#pragma once

// Thread-safety: a plain value type.
//
// A SECS-II message as the layers above HSMS see it: stream, function, the
// reply-wanted bit, and an optional body (many messages, such as S1F1, have
// none). Turning it into an HSMS data frame is the HSMS layer's job.

#include <cstdint>
#include <optional>

#include "ssim/secsgem/secs2/item.hpp"

namespace ssim::secsgem::secs2 {

struct Message {
    std::uint8_t stream = 0;    // 0..127
    std::uint8_t function = 0;  // 0..255
    bool w_bit = false;         // a reply is expected
    std::optional<Item> body;
};

}  // namespace ssim::secsgem::secs2
