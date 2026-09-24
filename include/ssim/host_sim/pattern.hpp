#pragma once

// Thread-safety: pure functions over value types.
//
// FR-S2-5 / PRD 8.7. Readable text for SECS-II items, used by host scripts to
// write messages to send and patterns to expect:
//
//   L[ A"START" L[ L[ A"WAFER_ID" A"W042" ] ] ]     nested lists, ASCII strings
//   U4[2005]   I2[-3 4]   F4[1.5]   B[0 0x0A]   BOOLEAN[true]   numbers in arrays
//   A[*]   U4[*]   L[*]   *                          wildcards (patterns only)
//
// A list holds its items separated by white space; an array holds numbers
// separated by white space or commas; a string is A"..." with \" and \\
// escapes. `L[*]` is any list, `T[*]` any item of type T (any count), and a
// bare `*` any item at all. Lists otherwise match by exact length and item by
// item. Wildcards are only legal in patterns; parse_item() rejects them.

#include <string_view>
#include <vector>

#include "ssim/core/result.hpp"
#include "ssim/secsgem/secs2/item.hpp"

namespace ssim::host_sim {

constexpr int kErrPatternSyntax = 600;

struct Pattern {
    enum class Kind {
        kAnyItem,      // *
        kAnyOfFormat,  // U4[*], A[*], ...
        kAnyList,      // L[*]
        kList,         // L[ p1 p2 ... ]: exact length, each element a pattern
        kExact,        // a typed array or string that must be equal
    };
    Kind kind = Kind::kAnyItem;
    ssim::secsgem::secs2::Format format = ssim::secsgem::secs2::Format::kList;  // kAnyOfFormat
    std::vector<Pattern> children;                                              // kList
    ssim::secsgem::secs2::Item exact;                                           // kExact
};

// Parses an item to send. Wildcards are a syntax error here.
[[nodiscard]] ssim::core::Result<ssim::secsgem::secs2::Item> parse_item(std::string_view text);

// Parses a pattern to match against a received item.
[[nodiscard]] ssim::core::Result<Pattern> parse_pattern(std::string_view text);

[[nodiscard]] bool matches(const Pattern& pattern, const ssim::secsgem::secs2::Item& item);

}  // namespace ssim::host_sim
