#pragma once

// Thread-safety: pure functions.
//
// The bridge between an HSMS data frame and a SECS-II message. Decoding is
// strict (FR-S2-2): a body that is not exactly one well-formed item is an
// error, which the GEM layer on Day 5 answers with S9F7.

#include "ssim/core/result.hpp"
#include "ssim/secsgem/hsms/frame.hpp"
#include "ssim/secsgem/secs2/message.hpp"

namespace ssim::secsgem::hsms {

// Fails if the frame is not a data message or its body does not decode.
// An empty body means "no item".
[[nodiscard]] ssim::core::Result<secs2::Message> to_message(const Frame& frame);

}  // namespace ssim::secsgem::hsms
