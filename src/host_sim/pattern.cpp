#include "ssim/host_sim/pattern.hpp"

#include <cctype>
#include <cerrno>
#include <cstdlib>
#include <limits>
#include <optional>
#include <string>

namespace ssim::host_sim {

using ssim::core::Error;
using ssim::core::Result;
using ssim::secsgem::secs2::Format;
using ssim::secsgem::secs2::Item;

namespace {

struct ParseError {
    std::string message;
};

class Parser {
public:
    Parser(std::string_view text, bool allow_wildcards) : text_(text), wild_(allow_wildcards) {}

    Result<Pattern> parse_all() {
        auto node = parse_node();
        if (!node) return node;
        skip_space();
        if (pos_ != text_.size()) {
            return fail("unexpected text after the item");
        }
        return node;
    }

private:
    Result<Pattern> fail(const std::string& what) const {
        return Result<Pattern>::err(
            Error{kErrPatternSyntax, what + " (at character " + std::to_string(pos_ + 1) + ")"});
    }

    void skip_space() {
        while (pos_ < text_.size() && std::isspace(static_cast<unsigned char>(text_[pos_]))) ++pos_;
    }
    bool at(char c) const { return pos_ < text_.size() && text_[pos_] == c; }

    Result<Pattern> parse_node() {
        skip_space();
        if (pos_ >= text_.size()) return fail("an item is missing");
        Pattern p;
        if (at('*')) {
            if (!wild_) return fail("wildcards are only allowed in patterns");
            ++pos_;
            p.kind = Pattern::Kind::kAnyItem;
            return Result<Pattern>::ok(std::move(p));
        }
        // Type name: letters and digits.
        std::string name;
        while (pos_ < text_.size() && std::isalnum(static_cast<unsigned char>(text_[pos_]))) {
            name += text_[pos_++];
        }
        if (name.empty()) return fail("an item type is expected");
        const auto format = format_from_name(name);
        if (!format) return fail("unknown item type '" + name + "'");

        if (*format == Format::kAscii && at('"')) {
            return parse_string();
        }
        if (!at('[')) return fail("'[' is expected after " + name);
        ++pos_;
        skip_space();

        if (at('*')) {
            if (!wild_) return fail("wildcards are only allowed in patterns");
            ++pos_;
            skip_space();
            if (!at(']')) return fail("']' is expected after '*'");
            ++pos_;
            p.kind =
                *format == Format::kList ? Pattern::Kind::kAnyList : Pattern::Kind::kAnyOfFormat;
            p.format = *format;
            return Result<Pattern>::ok(std::move(p));
        }
        if (*format == Format::kList) return parse_list();
        return parse_array(*format);
    }

    static std::optional<Format> format_from_name(const std::string& n) {
        if (n == "L") return Format::kList;
        if (n == "B") return Format::kBinary;
        if (n == "BOOLEAN" || n == "BOOL") return Format::kBoolean;
        if (n == "A") return Format::kAscii;
        if (n == "I1") return Format::kI1;
        if (n == "I2") return Format::kI2;
        if (n == "I4") return Format::kI4;
        if (n == "I8") return Format::kI8;
        if (n == "U1") return Format::kU1;
        if (n == "U2") return Format::kU2;
        if (n == "U4") return Format::kU4;
        if (n == "U8") return Format::kU8;
        if (n == "F4") return Format::kF4;
        if (n == "F8") return Format::kF8;
        return std::nullopt;
    }

    Result<Pattern> parse_string() {
        ++pos_;  // opening quote
        std::string value;
        for (;;) {
            if (pos_ >= text_.size()) return fail("a string is not closed");
            const char c = text_[pos_++];
            if (c == '"') break;
            if (c == '\\') {
                if (pos_ >= text_.size()) return fail("a string ends in a backslash");
                const char e = text_[pos_++];
                if (e != '"' && e != '\\') return fail("only \\\" and \\\\ are escapes");
                value += e;
            } else {
                value += c;
            }
        }
        Pattern p;
        p.kind = Pattern::Kind::kExact;
        p.exact = Item::ascii(std::move(value));
        return Result<Pattern>::ok(std::move(p));
    }

    Result<Pattern> parse_list() {
        Pattern p;
        p.kind = Pattern::Kind::kList;
        for (;;) {
            skip_space();
            if (pos_ >= text_.size()) return fail("a list is not closed");
            if (at(']')) {
                ++pos_;
                break;
            }
            auto child = parse_node();
            if (!child) return child;
            p.children.push_back(std::move(child).value());
        }
        return Result<Pattern>::ok(std::move(p));
    }

    // Reads the words of an array up to ']'.
    bool read_words(std::vector<std::string>& words) {
        for (;;) {
            while (pos_ < text_.size() &&
                   (std::isspace(static_cast<unsigned char>(text_[pos_])) || text_[pos_] == ',')) {
                ++pos_;
            }
            if (pos_ >= text_.size()) return false;
            if (at(']')) {
                ++pos_;
                return true;
            }
            std::string w;
            while (pos_ < text_.size() && text_[pos_] != ']' && text_[pos_] != ',' &&
                   !std::isspace(static_cast<unsigned char>(text_[pos_]))) {
                w += text_[pos_++];
            }
            words.push_back(std::move(w));
        }
    }

    template <typename T>
    static bool parse_signed(const std::string& w, T& out) {
        errno = 0;
        char* end = nullptr;
        const long long v = std::strtoll(w.c_str(), &end, 0);
        if (errno != 0 || end == w.c_str() || *end != '\0') return false;
        if (v < static_cast<long long>(std::numeric_limits<T>::min()) ||
            v > static_cast<long long>(std::numeric_limits<T>::max())) {
            return false;
        }
        out = static_cast<T>(v);
        return true;
    }

    template <typename T>
    static bool parse_unsigned(const std::string& w, T& out) {
        if (!w.empty() && w[0] == '-') return false;
        errno = 0;
        char* end = nullptr;
        const unsigned long long v = std::strtoull(w.c_str(), &end, 0);
        if (errno != 0 || end == w.c_str() || *end != '\0') return false;
        if (v > static_cast<unsigned long long>(std::numeric_limits<T>::max())) return false;
        out = static_cast<T>(v);
        return true;
    }

    template <typename T, typename Parse>
    bool collect(const std::vector<std::string>& words, std::vector<T>& out, Parse parse) {
        for (const std::string& w : words) {
            T v{};
            if (!parse(w, v)) {
                bad_word_ = w;
                return false;
            }
            out.push_back(v);
        }
        return true;
    }

    static bool parse_float(const std::string& w, double& out) {
        errno = 0;
        char* end = nullptr;
        out = std::strtod(w.c_str(), &end);
        return errno == 0 && end != w.c_str() && *end == '\0';
    }

    Result<Pattern> parse_array(Format format) {
        std::vector<std::string> words;
        if (!read_words(words)) return fail("an array is not closed");
        Pattern p;
        p.kind = Pattern::Kind::kExact;
        bool ok = true;
        switch (format) {
            case Format::kBinary: {
                std::vector<std::uint8_t> v;
                ok = collect(words, v, parse_unsigned<std::uint8_t>);
                if (ok) p.exact = Item::binary(std::move(v));
                break;
            }
            case Format::kBoolean: {
                std::vector<std::uint8_t> v;
                ok = collect(words, v, [](const std::string& w, std::uint8_t& o) {
                    if (w == "true" || w == "1") {
                        o = 1;
                        return true;
                    }
                    if (w == "false" || w == "0") {
                        o = 0;
                        return true;
                    }
                    return false;
                });
                if (ok) p.exact = Item::boolean(std::move(v));
                break;
            }
            case Format::kAscii:
                if (!words.empty()) return fail("write a string as A\"text\"");
                p.exact = Item::ascii("");
                break;
            case Format::kI1: {
                std::vector<std::int8_t> v;
                ok = collect(words, v, parse_signed<std::int8_t>);
                if (ok) p.exact = Item::i1(std::move(v));
                break;
            }
            case Format::kI2: {
                std::vector<std::int16_t> v;
                ok = collect(words, v, parse_signed<std::int16_t>);
                if (ok) p.exact = Item::i2(std::move(v));
                break;
            }
            case Format::kI4: {
                std::vector<std::int32_t> v;
                ok = collect(words, v, parse_signed<std::int32_t>);
                if (ok) p.exact = Item::i4(std::move(v));
                break;
            }
            case Format::kI8: {
                std::vector<std::int64_t> v;
                ok = collect(words, v, parse_signed<std::int64_t>);
                if (ok) p.exact = Item::i8(std::move(v));
                break;
            }
            case Format::kU1: {
                std::vector<std::uint8_t> v;
                ok = collect(words, v, parse_unsigned<std::uint8_t>);
                if (ok) p.exact = Item::u1(std::move(v));
                break;
            }
            case Format::kU2: {
                std::vector<std::uint16_t> v;
                ok = collect(words, v, parse_unsigned<std::uint16_t>);
                if (ok) p.exact = Item::u2(std::move(v));
                break;
            }
            case Format::kU4: {
                std::vector<std::uint32_t> v;
                ok = collect(words, v, parse_unsigned<std::uint32_t>);
                if (ok) p.exact = Item::u4(std::move(v));
                break;
            }
            case Format::kU8: {
                std::vector<std::uint64_t> v;
                ok = collect(words, v, parse_unsigned<std::uint64_t>);
                if (ok) p.exact = Item::u8(std::move(v));
                break;
            }
            case Format::kF4: {
                std::vector<double> d;
                ok = collect(words, d, parse_float);
                if (ok) {
                    std::vector<float> v(d.begin(), d.end());
                    p.exact = Item::f4(std::move(v));
                }
                break;
            }
            case Format::kF8: {
                std::vector<double> v;
                ok = collect(words, v, parse_float);
                if (ok) p.exact = Item::f8(std::move(v));
                break;
            }
            case Format::kList:
            case Format::kJis8:
                return fail("unsupported item type");
        }
        if (!ok) return fail("'" + bad_word_ + "' is not a valid value for this type");
        return Result<Pattern>::ok(std::move(p));
    }

    std::string_view text_;
    bool wild_;
    std::size_t pos_ = 0;
    std::string bad_word_;
};

Item to_item(const Pattern& p) {
    if (p.kind == Pattern::Kind::kExact) return p.exact;
    Item::List items;
    for (const Pattern& c : p.children) items.push_back(to_item(c));
    return Item::list(std::move(items));
}

}  // namespace

Result<Item> parse_item(std::string_view text) {
    auto p = Parser(text, /*allow_wildcards=*/false).parse_all();
    if (!p) return Result<Item>::err(p.error());
    return Result<Item>::ok(to_item(p.value()));
}

Result<Pattern> parse_pattern(std::string_view text) {
    return Parser(text, /*allow_wildcards=*/true).parse_all();
}

bool matches(const Pattern& pattern, const Item& item) {
    switch (pattern.kind) {
        case Pattern::Kind::kAnyItem:
            return true;
        case Pattern::Kind::kAnyOfFormat:
            return item.format() == pattern.format;
        case Pattern::Kind::kAnyList:
            return item.is_list();
        case Pattern::Kind::kExact:
            return item == pattern.exact;
        case Pattern::Kind::kList: {
            if (!item.is_list() || item.count() != pattern.children.size()) return false;
            const auto& items = item.as_list();
            for (std::size_t i = 0; i < items.size(); ++i) {
                if (!matches(pattern.children[i], items[i])) return false;
            }
            return true;
        }
    }
    return false;
}

}  // namespace ssim::host_sim
