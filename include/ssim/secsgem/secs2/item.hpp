#pragma once

// Thread-safety: a plain value type. Safe to copy or move across threads; not
// safe to mutate the same instance concurrently.
//
// FR-S2-1 / PRD 8.6.2: one SECS-II data item, a list or a typed array. SECS-II
// numeric items hold arrays (a U4 item can carry several values), so every
// non-list item stores a vector. Binary, BOOLEAN and U1 all use one byte per
// element, so the Format tag (not the C++ type) tells them apart.
// float appears only for F4, the one place the wire format is single
// precision (CLAUDE.md 5.1).

#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace ssim::secsgem::secs2 {

// Format codes are the octal values from PRD 8.6.2, written in hex with the
// octal in the comment. Verified against secsgem 0.3.0 (docs/protocol-notes.md).
enum class Format : std::uint8_t {
    kList = 0x00,     // octal 00
    kBinary = 0x08,   // octal 10
    kBoolean = 0x09,  // octal 11
    kAscii = 0x10,    // octal 20
    kJis8 = 0x11,     // octal 21, recognised but unsupported
    kI8 = 0x18,       // octal 30
    kI1 = 0x19,       // octal 31
    kI2 = 0x1A,       // octal 32
    kI4 = 0x1C,       // octal 34
    kF8 = 0x20,       // octal 40
    kF4 = 0x24,       // octal 44
    kU8 = 0x28,       // octal 50
    kU1 = 0x29,       // octal 51
    kU2 = 0x2A,       // octal 52
    kU4 = 0x2C,       // octal 54
};

const char* to_string(Format format);

// Bytes per element, or 0 for a list.
std::size_t element_size(Format format);

// True for the formats this project can encode and decode (everything except
// JIS-8).
bool is_supported(Format format);

class Item {
public:
    using List = std::vector<Item>;

    Item() : Item(list({})) {}

    static Item list(List items);
    static Item binary(std::vector<std::uint8_t> values);
    static Item boolean(std::vector<std::uint8_t> values);  // each 0 or 1 when emitted
    static Item ascii(std::string text);
    static Item i1(std::vector<std::int8_t> values);
    static Item i2(std::vector<std::int16_t> values);
    static Item i4(std::vector<std::int32_t> values);
    static Item i8(std::vector<std::int64_t> values);
    static Item u1(std::vector<std::uint8_t> values);
    static Item u2(std::vector<std::uint16_t> values);
    static Item u4(std::vector<std::uint32_t> values);
    static Item u8(std::vector<std::uint64_t> values);
    static Item f4(std::vector<float> values);
    static Item f8(std::vector<double> values);

    // Single-value conveniences.
    static Item u1(std::uint8_t v) { return u1(std::vector<std::uint8_t>{v}); }
    static Item u2(std::uint16_t v) { return u2(std::vector<std::uint16_t>{v}); }
    static Item u4(std::uint32_t v) { return u4(std::vector<std::uint32_t>{v}); }
    static Item f4(float v) { return f4(std::vector<float>{v}); }
    static Item boolean(bool v) {
        return boolean(std::vector<std::uint8_t>{static_cast<std::uint8_t>(v ? 1 : 0)});
    }

    Format format() const { return format_; }
    bool is_list() const { return format_ == Format::kList; }

    // Number of list entries or array elements (ASCII: characters).
    std::size_t count() const;

    // Accessors. Calling the wrong one is a programming error (std::get
    // throws); check format() first when the type is not known.
    const List& as_list() const { return std::get<List>(data_); }
    const std::string& as_ascii() const { return std::get<std::string>(data_); }
    template <typename T>
    const std::vector<T>& as_array() const {
        return std::get<std::vector<T>>(data_);
    }

    friend bool operator==(const Item& a, const Item& b) {
        return a.format_ == b.format_ && a.data_ == b.data_;
    }
    friend bool operator!=(const Item& a, const Item& b) { return !(a == b); }

private:
    using Storage =
        std::variant<List, std::vector<std::uint8_t>, std::string, std::vector<std::int8_t>,
                     std::vector<std::int16_t>, std::vector<std::int32_t>,
                     std::vector<std::int64_t>, std::vector<std::uint16_t>,
                     std::vector<std::uint32_t>, std::vector<std::uint64_t>, std::vector<float>,
                     std::vector<double>>;

    Item(Format format, Storage data) : format_(format), data_(std::move(data)) {}

    Format format_;
    Storage data_;
};

}  // namespace ssim::secsgem::secs2
