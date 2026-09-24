#include "ssim/secsgem/secs2/text_dump.hpp"

#include <cstdio>
#include <type_traits>

namespace ssim::secsgem::secs2 {

namespace {

void append_ascii(const std::string& text, std::string& out) {
    out += '"';
    const std::size_t shown = text.size() < kDumpMaxChars ? text.size() : kDumpMaxChars;
    for (std::size_t i = 0; i < shown; ++i) {
        const unsigned char c = static_cast<unsigned char>(text[i]);
        if (c == '"' || c == '\\') {
            out += '\\';
            out += static_cast<char>(c);
        } else if (c >= 0x20 && c <= 0x7E) {
            out += static_cast<char>(c);
        } else {
            char buf[8];
            std::snprintf(buf, sizeof buf, "\\x%02X", c);
            out += buf;
        }
    }
    if (text.size() > shown) {
        out += "...";
    }
    out += '"';
}

template <typename T>
void append_number(T value, std::string& out) {
    char buf[40];
    if constexpr (std::is_same_v<T, float>) {
        std::snprintf(buf, sizeof buf, "%.9g", static_cast<double>(value));
    } else if constexpr (std::is_same_v<T, double>) {
        std::snprintf(buf, sizeof buf, "%.17g", value);
    } else if constexpr (std::is_signed_v<T>) {
        std::snprintf(buf, sizeof buf, "%lld", static_cast<long long>(value));
    } else {
        std::snprintf(buf, sizeof buf, "%llu", static_cast<unsigned long long>(value));
    }
    out += buf;
}

template <typename T>
void append_array(const std::vector<T>& values, std::string& out) {
    const std::size_t shown = values.size() < kDumpMaxElements ? values.size() : kDumpMaxElements;
    for (std::size_t i = 0; i < shown; ++i) {
        out += ' ';
        append_number(values[i], out);
    }
    if (values.size() > shown) {
        out += " ... (" + std::to_string(values.size()) + " total)";
    }
}

void append_item(const Item& item, std::string& out) {
    out += '<';
    out += to_string(item.format());

    switch (item.format()) {
        case Format::kList: {
            out += " [" + std::to_string(item.count()) + "]";
            const auto& children = item.as_list();
            const std::size_t shown =
                children.size() < kDumpMaxElements ? children.size() : kDumpMaxElements;
            for (std::size_t i = 0; i < shown; ++i) {
                out += ' ';
                append_item(children[i], out);
            }
            if (children.size() > shown) {
                out += " ... (" + std::to_string(children.size()) + " entries)";
            }
            break;
        }
        case Format::kAscii:
            out += ' ';
            append_ascii(item.as_ascii(), out);
            break;
        case Format::kBinary: {
            const auto& v = item.as_array<std::uint8_t>();
            const std::size_t shown = v.size() < kDumpMaxElements ? v.size() : kDumpMaxElements;
            for (std::size_t i = 0; i < shown; ++i) {
                char buf[8];
                std::snprintf(buf, sizeof buf, " 0x%02X", v[i]);
                out += buf;
            }
            if (v.size() > shown) {
                out += " ... (" + std::to_string(v.size()) + " total)";
            }
            break;
        }
        case Format::kBoolean:
            for (std::uint8_t b : item.as_array<std::uint8_t>()) {
                out += b != 0 ? " true" : " false";
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
        case Format::kU1:
            append_array(item.as_array<std::uint8_t>(), out);
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
        case Format::kJis8:
            break;  // never held by an Item
    }
    out += '>';
}

}  // namespace

std::string to_text(const Item& item) {
    std::string out;
    append_item(item, out);
    return out;
}

std::string to_text(const Message& message) {
    std::string out = "S" + std::to_string(message.stream) + "F" + std::to_string(message.function);
    if (message.w_bit) {
        out += " W";
    }
    if (message.body.has_value()) {
        out += ' ';
        append_item(*message.body, out);
    }
    return out;
}

}  // namespace ssim::secsgem::secs2
