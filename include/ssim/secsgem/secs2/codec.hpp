#pragma once

// Thread-safety: Thread-safe; stateless free functions.
//
// FR-S2-1 / FR-S2-2, PRD 8.6.2. Item header: format byte = (format code << 2)
// OR (number of length bytes, 1 to 3), then the big-endian length, then the
// data. For a list the length is the number of items, otherwise the number of
// data bytes. Failures are returned as values (CLAUDE.md 6.3); nothing here
// throws or reads past the end of the input.
//
// The decoder never trusts a length field: a length larger than the bytes that
// remain is rejected before anything is allocated, and list nesting deeper
// than kMaxNestingDepth is rejected, so untrusted input cannot force a large
// allocation or a deep recursion.

#include <cstddef>
#include <cstdint>
#include <vector>

#include "ssim/core/result.hpp"
#include "ssim/secsgem/bytes.hpp"
#include "ssim/secsgem/secs2/item.hpp"

namespace ssim::secsgem::secs2 {

// Stable reason codes (tests assert on these). Distinct from the process and
// config codes in ssim_core.
constexpr int kErrTruncated = 300;          // ran out of bytes
constexpr int kErrBadLengthBytes = 301;     // format byte says 0 length bytes
constexpr int kErrUnsupportedFormat = 302;  // unknown format code, or JIS-8
constexpr int kErrBadLength = 303;          // byte length not a multiple of the element size
constexpr int kErrListCountTooLarge = 304;  // more entries than the remaining bytes can hold
constexpr int kErrTooDeep = 305;            // list nesting beyond kMaxNestingDepth
constexpr int kErrTrailingBytes = 306;      // decode_body: bytes left after the item
constexpr int kErrTooLong = 307;            // encode: length does not fit in 3 length bytes

// "Nesting deeper than 32 is rejected" (FR-S2-2): at most 32 lists inside one another.
constexpr std::size_t kMaxNestingDepth = 32;

// Largest value three length bytes can hold.
constexpr std::size_t kMaxItemLength = 0xFFFFFF;

// Encodes with the fewest length bytes that fit.
[[nodiscard]] ssim::core::Result<std::vector<std::uint8_t>> encode(const Item& item);

struct Decoded {
    Item item;
    std::size_t consumed = 0;  // bytes used from the front of the input
};

// Decodes the first item and reports how many bytes it used; trailing bytes
// are left alone (the caller decides what they mean).
[[nodiscard]] ssim::core::Result<Decoded> decode_item(ByteSpan bytes);

// Decodes a whole message body: exactly one item, no bytes left over.
[[nodiscard]] ssim::core::Result<Item> decode_body(ByteSpan bytes);

}  // namespace ssim::secsgem::secs2
