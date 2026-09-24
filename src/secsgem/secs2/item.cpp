#include "ssim/secsgem/secs2/item.hpp"

namespace ssim::secsgem::secs2 {

const char* to_string(Format format) {
    switch (format) {
        case Format::kList:
            return "L";
        case Format::kBinary:
            return "B";
        case Format::kBoolean:
            return "BOOLEAN";
        case Format::kAscii:
            return "A";
        case Format::kJis8:
            return "J";
        case Format::kI8:
            return "I8";
        case Format::kI1:
            return "I1";
        case Format::kI2:
            return "I2";
        case Format::kI4:
            return "I4";
        case Format::kF8:
            return "F8";
        case Format::kF4:
            return "F4";
        case Format::kU8:
            return "U8";
        case Format::kU1:
            return "U1";
        case Format::kU2:
            return "U2";
        case Format::kU4:
            return "U4";
    }
    return "?";
}

std::size_t element_size(Format format) {
    switch (format) {
        case Format::kList:
            return 0;
        case Format::kBinary:
        case Format::kBoolean:
        case Format::kAscii:
        case Format::kJis8:
        case Format::kI1:
        case Format::kU1:
            return 1;
        case Format::kI2:
        case Format::kU2:
            return 2;
        case Format::kI4:
        case Format::kU4:
        case Format::kF4:
            return 4;
        case Format::kI8:
        case Format::kU8:
        case Format::kF8:
            return 8;
    }
    return 0;
}

bool is_supported(Format format) { return format != Format::kJis8; }

Item Item::list(List items) { return Item(Format::kList, Storage(std::move(items))); }
Item Item::binary(std::vector<std::uint8_t> v) {
    return Item(Format::kBinary, Storage(std::move(v)));
}
Item Item::boolean(std::vector<std::uint8_t> v) {
    return Item(Format::kBoolean, Storage(std::move(v)));
}
Item Item::ascii(std::string text) { return Item(Format::kAscii, Storage(std::move(text))); }
Item Item::i1(std::vector<std::int8_t> v) { return Item(Format::kI1, Storage(std::move(v))); }
Item Item::i2(std::vector<std::int16_t> v) { return Item(Format::kI2, Storage(std::move(v))); }
Item Item::i4(std::vector<std::int32_t> v) { return Item(Format::kI4, Storage(std::move(v))); }
Item Item::i8(std::vector<std::int64_t> v) { return Item(Format::kI8, Storage(std::move(v))); }
Item Item::u1(std::vector<std::uint8_t> v) { return Item(Format::kU1, Storage(std::move(v))); }
Item Item::u2(std::vector<std::uint16_t> v) { return Item(Format::kU2, Storage(std::move(v))); }
Item Item::u4(std::vector<std::uint32_t> v) { return Item(Format::kU4, Storage(std::move(v))); }
Item Item::u8(std::vector<std::uint64_t> v) { return Item(Format::kU8, Storage(std::move(v))); }
Item Item::f4(std::vector<float> v) { return Item(Format::kF4, Storage(std::move(v))); }
Item Item::f8(std::vector<double> v) { return Item(Format::kF8, Storage(std::move(v))); }

std::size_t Item::count() const {
    return std::visit([](const auto& v) -> std::size_t { return v.size(); }, data_);
}

}  // namespace ssim::secsgem::secs2
