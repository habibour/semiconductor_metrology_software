#include "ssim/secsgem/secs2/codec.hpp"

#include <cstring>
#include <string>
#include <type_traits>
#include <utility>

namespace ssim::secsgem::secs2 {

namespace {

using ssim::core::Error;
using ssim::core::Result;

// ---- big-endian helpers --------------------------------------------------
// Bytes are assembled with shifts, never by casting a buffer to a wider type
// (CLAUDE.md 6.1: no pointer casts of wire data, explicit endianness).

template <typename UInt>
UInt load_be(const std::uint8_t* p) {
    UInt v = 0;
    for (std::size_t i = 0; i < sizeof(UInt); ++i) {
        v = static_cast<UInt>((v << 8) | p[i]);
    }
    return v;
}

template <typename UInt>
void store_be(UInt v, std::vector<std::uint8_t>& out) {
    for (std::size_t i = sizeof(UInt); i > 0; --i) {
        out.push_back(static_cast<std::uint8_t>((v >> ((i - 1) * 8)) & 0xFF));
    }
}

// Signed and unsigned integers of the same width share one code path through
// their unsigned twin; floats go through memcpy into an integer of equal size.
template <typename T>
T element_from_bytes(const std::uint8_t* p) {
    if constexpr (std::is_same_v<T, float>) {
        const std::uint32_t bits = load_be<std::uint32_t>(p);
        float f;
        std::memcpy(&f, &bits, sizeof f);
        return f;
    } else if constexpr (std::is_same_v<T, double>) {
        const std::uint64_t bits = load_be<std::uint64_t>(p);
        double d;
        std::memcpy(&d, &bits, sizeof d);
        return d;
    } else {
        using U = std::make_unsigned_t<T>;
        return static_cast<T>(load_be<U>(p));
    }
}

template <typename T>
void element_to_bytes(T v, std::vector<std::uint8_t>& out) {
    if constexpr (std::is_same_v<T, float>) {
        std::uint32_t bits;
        std::memcpy(&bits, &v, sizeof bits);
        store_be(bits, out);
    } else if constexpr (std::is_same_v<T, double>) {
        std::uint64_t bits;
        std::memcpy(&bits, &v, sizeof bits);
        store_be(bits, out);
    } else {
        store_be(static_cast<std::make_unsigned_t<T>>(v), out);
    }
}

// ---- bounds-checked reader -----------------------------------------------

class Reader {
public:
    explicit Reader(ByteSpan bytes) : bytes_(bytes) {}

    std::size_t position() const { return pos_; }
    std::size_t remaining() const { return bytes_.size - pos_; }

    // Returns a pointer to the next n bytes and advances, or nullptr if fewer
    // than n remain. The only way this file reads input.
    const std::uint8_t* take(std::size_t n) {
        if (n > remaining()) {
            return nullptr;
        }
        const std::uint8_t* p = bytes_.data + pos_;
        pos_ += n;
        return p;
    }

private:
    ByteSpan bytes_;
    std::size_t pos_ = 0;
};

Result<Format> parse_format(std::uint8_t code) {
    switch (code) {
        case 0x00:
            return Result<Format>::ok(Format::kList);
        case 0x08:
            return Result<Format>::ok(Format::kBinary);
        case 0x09:
            return Result<Format>::ok(Format::kBoolean);
        case 0x10:
            return Result<Format>::ok(Format::kAscii);
        case 0x18:
            return Result<Format>::ok(Format::kI8);
        case 0x19:
            return Result<Format>::ok(Format::kI1);
        case 0x1A:
            return Result<Format>::ok(Format::kI2);
        case 0x1C:
            return Result<Format>::ok(Format::kI4);
        case 0x20:
            return Result<Format>::ok(Format::kF8);
        case 0x24:
            return Result<Format>::ok(Format::kF4);
        case 0x28:
            return Result<Format>::ok(Format::kU8);
        case 0x29:
            return Result<Format>::ok(Format::kU1);
        case 0x2A:
            return Result<Format>::ok(Format::kU2);
        case 0x2C:
            return Result<Format>::ok(Format::kU4);
        default:
            break;
    }
    if (code == 0x11) {
        return Result<Format>::err(Error{kErrUnsupportedFormat, "JIS-8 items are not supported"});
    }
    return Result<Format>::err(Error{kErrUnsupportedFormat, "unknown SECS-II format code"});
}

// ---- decode --------------------------------------------------------------

Item build_array(Format format, const std::uint8_t* p, std::size_t byte_len) {
    const std::size_t n = byte_len / element_size(format);
    switch (format) {
        case Format::kBinary:
            return Item::binary(std::vector<std::uint8_t>(p, p + byte_len));
        case Format::kBoolean:
            return Item::boolean(std::vector<std::uint8_t>(p, p + byte_len));
        case Format::kAscii:
            return Item::ascii(std::string(p, p + byte_len));
        case Format::kU1:
            return Item::u1(std::vector<std::uint8_t>(p, p + byte_len));
        default:
            break;
    }
    auto collect = [&](auto tag) {
        using T = decltype(tag);
        std::vector<T> values;
        values.reserve(n);
        for (std::size_t i = 0; i < n; ++i) {
            values.push_back(element_from_bytes<T>(p + i * sizeof(T)));
        }
        return values;
    };
    switch (format) {
        case Format::kI1:
            return Item::i1(collect(std::int8_t{}));
        case Format::kI2:
            return Item::i2(collect(std::int16_t{}));
        case Format::kI4:
            return Item::i4(collect(std::int32_t{}));
        case Format::kI8:
            return Item::i8(collect(std::int64_t{}));
        case Format::kU2:
            return Item::u2(collect(std::uint16_t{}));
        case Format::kU4:
            return Item::u4(collect(std::uint32_t{}));
        case Format::kU8:
            return Item::u8(collect(std::uint64_t{}));
        case Format::kF4:
            return Item::f4(collect(float{}));
        case Format::kF8:
            return Item::f8(collect(double{}));
        default:
            break;
    }
    return Item();
}

Result<Item> decode_at_depth(Reader& reader, std::size_t depth) {
    const std::uint8_t* header = reader.take(1);
    if (header == nullptr) {
        return Result<Item>::err(Error{kErrTruncated, "no item header"});
    }
    const std::uint8_t format_byte = header[0];
    const std::size_t length_bytes = format_byte & 0x03U;
    if (length_bytes == 0) {
        return Result<Item>::err(Error{kErrBadLengthBytes, "item header has zero length bytes"});
    }
    auto format = parse_format(static_cast<std::uint8_t>(format_byte >> 2));
    if (!format) {
        return Result<Item>::err(format.error());
    }
    const std::uint8_t* length_field = reader.take(length_bytes);
    if (length_field == nullptr) {
        return Result<Item>::err(Error{kErrTruncated, "item length is cut off"});
    }
    std::size_t length = 0;
    for (std::size_t i = 0; i < length_bytes; ++i) {
        length = (length << 8) | length_field[i];
    }

    if (format.value() == Format::kList) {
        if (depth >= kMaxNestingDepth) {
            return Result<Item>::err(Error{kErrTooDeep, "list nesting deeper than 32"});
        }
        // Every item needs at least two bytes (format byte and one length
        // byte), so a count above remaining/2 cannot be satisfied. Checked
        // before any allocation.
        if (length > reader.remaining() / 2) {
            return Result<Item>::err(
                Error{kErrListCountTooLarge, "list count exceeds the bytes that remain"});
        }
        Item::List items;
        items.reserve(length);
        for (std::size_t i = 0; i < length; ++i) {
            auto child = decode_at_depth(reader, depth + 1);
            if (!child) {
                return child;
            }
            items.push_back(std::move(child).value());
        }
        return Result<Item>::ok(Item::list(std::move(items)));
    }

    if (length > reader.remaining()) {
        return Result<Item>::err(Error{kErrTruncated, "item data is cut off"});
    }
    const std::size_t elem = element_size(format.value());
    if (length % elem != 0) {
        return Result<Item>::err(
            Error{kErrBadLength, "item length is not a multiple of its element size"});
    }
    const std::uint8_t* data = reader.take(length);
    // take() cannot fail here (length <= remaining), but the pointer is never
    // used unchecked: a zero-length item does not touch it.
    return Result<Item>::ok(build_array(format.value(), data, length));
}

// ---- encode --------------------------------------------------------------

Result<bool> encode_header(Format format, std::size_t length, std::vector<std::uint8_t>& out) {
    if (length > kMaxItemLength) {
        return Result<bool>::err(Error{kErrTooLong, "item too long for three length bytes"});
    }
    const std::size_t length_bytes = length > 0xFFFF ? 3 : (length > 0xFF ? 2 : 1);
    out.push_back(static_cast<std::uint8_t>((static_cast<unsigned>(format) << 2) | length_bytes));
    for (std::size_t i = length_bytes; i > 0; --i) {
        out.push_back(static_cast<std::uint8_t>((length >> ((i - 1) * 8)) & 0xFF));
    }
    return Result<bool>::ok(true);
}

template <typename T>
void append_array(const std::vector<T>& values, std::vector<std::uint8_t>& out) {
    for (T v : values) {
        element_to_bytes(v, out);
    }
}

Result<bool> encode_at_depth(const Item& item, std::size_t depth, std::vector<std::uint8_t>& out) {
    if (item.is_list()) {
        if (depth >= kMaxNestingDepth) {
            return Result<bool>::err(Error{kErrTooDeep, "list nesting deeper than 32"});
        }
        auto header = encode_header(Format::kList, item.count(), out);
        if (!header) {
            return header;
        }
        for (const Item& child : item.as_list()) {
            auto r = encode_at_depth(child, depth + 1, out);
            if (!r) {
                return r;
            }
        }
        return Result<bool>::ok(true);
    }

    const Format format = item.format();
    if (!is_supported(format)) {
        return Result<bool>::err(Error{kErrUnsupportedFormat, "cannot encode this format"});
    }
    auto header = encode_header(format, item.count() * element_size(format), out);
    if (!header) {
        return header;
    }
    switch (format) {
        case Format::kAscii: {
            const std::string& s = item.as_ascii();
            out.insert(out.end(), s.begin(), s.end());
            break;
        }
        case Format::kBinary:
        case Format::kU1: {
            const auto& v = item.as_array<std::uint8_t>();
            out.insert(out.end(), v.begin(), v.end());
            break;
        }
        case Format::kBoolean:
            for (std::uint8_t b : item.as_array<std::uint8_t>()) {
                out.push_back(b != 0 ? 1 : 0);  // emit only 0 or 1
            }
            break;
        case Format::kI1:
            append_array(item.as_array<std::int8_t>(), out);
            break;
        case Format::kI2:
            append_array(item.as_array<std::int16_t>(), out);
            break;
        case Format::kI4:
            append_array(item.as_array<std::int32_t>(), out);
            break;
        case Format::kI8:
            append_array(item.as_array<std::int64_t>(), out);
            break;
        case Format::kU2:
            append_array(item.as_array<std::uint16_t>(), out);
            break;
        case Format::kU4:
            append_array(item.as_array<std::uint32_t>(), out);
            break;
        case Format::kU8:
            append_array(item.as_array<std::uint64_t>(), out);
            break;
        case Format::kF4:
            append_array(item.as_array<float>(), out);
            break;
        case Format::kF8:
            append_array(item.as_array<double>(), out);
            break;
        case Format::kList:
        case Format::kJis8:
            break;  // handled above
    }
    return Result<bool>::ok(true);
}

}  // namespace

Result<std::vector<std::uint8_t>> encode(const Item& item) {
    std::vector<std::uint8_t> out;
    auto r = encode_at_depth(item, 0, out);
    if (!r) {
        return Result<std::vector<std::uint8_t>>::err(r.error());
    }
    return Result<std::vector<std::uint8_t>>::ok(std::move(out));
}

Result<Decoded> decode_item(ByteSpan bytes) {
    Reader reader(bytes);
    auto item = decode_at_depth(reader, 0);
    if (!item) {
        return Result<Decoded>::err(item.error());
    }
    return Result<Decoded>::ok(Decoded{std::move(item).value(), reader.position()});
}

Result<Item> decode_body(ByteSpan bytes) {
    auto decoded = decode_item(bytes);
    if (!decoded) {
        return Result<Item>::err(decoded.error());
    }
    if (decoded.value().consumed != bytes.size) {
        return Result<Item>::err(Error{kErrTrailingBytes, "bytes left after the item"});
    }
    return Result<Item>::ok(std::move(decoded).value().item);
}

}  // namespace ssim::secsgem::secs2
