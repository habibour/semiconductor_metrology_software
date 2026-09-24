#pragma once

// Thread-safety: Thread-safe; pure functions.
//
// FR-S2-4: human-readable text for logs and the message trace. The output is
// deterministic and bounded (long arrays, strings and lists are cut with a
// note), because logs must never contain unbounded data dumps (CLAUDE.md 6.7).
//
// Example: <L [3] <U4 2005> <A "W001"> <L [1] <F4 -179.4>>>

#include <string>

#include "ssim/secsgem/secs2/item.hpp"
#include "ssim/secsgem/secs2/message.hpp"

namespace ssim::secsgem::secs2 {

constexpr std::size_t kDumpMaxElements = 16;  // array elements or list entries shown
constexpr std::size_t kDumpMaxChars = 64;     // ASCII characters shown

std::string to_text(const Item& item);

// "S6F11 W <L [3] ...>", or just "S1F1 W" for a message without a body.
std::string to_text(const Message& message);

}  // namespace ssim::secsgem::secs2
