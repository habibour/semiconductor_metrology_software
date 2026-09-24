#pragma once

// Thread-safety: a non-owning view; safe to copy. The viewed memory must stay
// alive and unchanged while the view is used.
//
// C++17 has no std::span (CLAUDE.md §6.1 asks for a small own type), and every
// wire parser in ssim_secsgem reads through this so that a read can never
// run past the end of a buffer.

#include <cstddef>
#include <cstdint>
#include <vector>

namespace ssim::secsgem {

struct ByteSpan {
    const std::uint8_t* data = nullptr;
    std::size_t size = 0;

    ByteSpan() = default;
    ByteSpan(const std::uint8_t* d, std::size_t n) : data(d), size(n) {}
    ByteSpan(const std::vector<std::uint8_t>& v) : data(v.data()), size(v.size()) {}  // NOLINT

    bool empty() const { return size == 0; }
};

}  // namespace ssim::secsgem
