#include "ssim/host_sim/script.hpp"

#include <cctype>
#include <cstdlib>
#include <sstream>

namespace ssim::host_sim {

using ssim::core::Error;
using ssim::core::Result;

namespace {

Error syntax(int line, const std::string& what) {
    return Error{kErrScriptSyntax, "line " + std::to_string(line) + ": " + what};
}

std::string trim(std::string_view s) {
    std::size_t a = 0;
    std::size_t b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return std::string(s.substr(a, b - a));
}

// Removes a trailing comment: a '#' that is not inside a string.
std::string strip_comment(const std::string& line) {
    bool in_string = false;
    for (std::size_t i = 0; i < line.size(); ++i) {
        if (line[i] == '\\' && in_string) {
            ++i;
        } else if (line[i] == '"') {
            in_string = !in_string;
        } else if (line[i] == '#' && !in_string) {
            return line.substr(0, i);
        }
    }
    return line;
}

Result<std::string> substitute(const std::string& line,
                               const std::map<std::string, std::string>& vars, int line_no) {
    std::string out;
    for (std::size_t i = 0; i < line.size(); ++i) {
        if (line[i] != '$') {
            out += line[i];
            continue;
        }
        std::size_t j = i + 1;
        while (j < line.size() &&
               (std::isalnum(static_cast<unsigned char>(line[j])) || line[j] == '_'))
            ++j;
        const std::string name = line.substr(i + 1, j - i - 1);
        const auto it = vars.find(name);
        if (name.empty() || it == vars.end()) {
            return Result<std::string>::err(syntax(line_no, "unknown variable $" + name));
        }
        out += it->second;
        i = j - 1;
    }
    return Result<std::string>::ok(std::move(out));
}

// First whitespace-delimited word, and the rest of the line after it.
std::pair<std::string, std::string> split_word(const std::string& s) {
    std::size_t i = 0;
    while (i < s.size() && !std::isspace(static_cast<unsigned char>(s[i]))) ++i;
    return {s.substr(0, i), trim(std::string_view(s).substr(i))};
}

bool parse_uint(const std::string& s, std::uint32_t& out) {
    if (s.empty() || s[0] == '-') return false;
    char* end = nullptr;
    const unsigned long long v = std::strtoull(s.c_str(), &end, 0);
    if (*end != '\0' || v > 0xFFFFFFFFull) return false;
    out = static_cast<std::uint32_t>(v);
    return true;
}

bool parse_double(const std::string& s, double& out) {
    if (s.empty()) return false;
    char* end = nullptr;
    out = std::strtod(s.c_str(), &end);
    return *end == '\0';
}

// "S6F11" -> (6, 11).
bool parse_sf(const std::string& word, int& stream, int& function) {
    if (word.size() < 4 || (word[0] != 'S' && word[0] != 's')) return false;
    const std::size_t f = word.find_first_of("Ff");
    if (f == std::string::npos || f < 2) return false;
    std::uint32_t s = 0;
    std::uint32_t fn = 0;
    if (!parse_uint(word.substr(1, f - 1), s) || !parse_uint(word.substr(f + 1), fn)) return false;
    if (s > 127 || fn > 255) return false;
    stream = static_cast<int>(s);
    function = static_cast<int>(fn);
    return true;
}

// Consumes leading "key=value" options and the flags nowait/W, leaving the rest.
struct Options {
    std::optional<std::chrono::milliseconds> timeout;
    std::optional<int> device;
    std::optional<int> status;
    std::optional<bool> w_bit;
};

Result<std::string> take_options(std::string rest, Options& opts, int line) {
    for (;;) {
        auto [word, tail] = split_word(rest);
        if (word.empty()) return Result<std::string>::ok(std::move(rest));
        if (word == "nowait") {
            opts.w_bit = false;
        } else if (word == "W") {
            opts.w_bit = true;
        } else if (word.rfind("timeout=", 0) == 0) {
            auto d = parse_duration(word.substr(8));
            if (!d) return Result<std::string>::err(syntax(line, d.error().message));
            opts.timeout = d.value();
        } else if (word.rfind("device=", 0) == 0) {
            std::uint32_t v = 0;
            if (!parse_uint(word.substr(7), v) || v > 65535) {
                return Result<std::string>::err(syntax(line, "bad device id"));
            }
            opts.device = static_cast<int>(v);
        } else if (word.rfind("status=", 0) == 0) {
            std::uint32_t v = 0;
            if (!parse_uint(word.substr(7), v) || v > 255) {
                return Result<std::string>::err(syntax(line, "bad status"));
            }
            opts.status = static_cast<int>(v);
        } else {
            return Result<std::string>::ok(std::move(rest));  // the item text starts here
        }
        rest = tail;
    }
}

}  // namespace

Result<std::chrono::milliseconds> parse_duration(std::string_view text) {
    using R = Result<std::chrono::milliseconds>;
    std::string s(text);
    double unit = 1000.0;  // a bare number is seconds
    if (s.size() > 2 && s.compare(s.size() - 2, 2, "ms") == 0) {
        unit = 1.0;
        s.resize(s.size() - 2);
    } else if (!s.empty() && s.back() == 's') {
        s.pop_back();
    }
    double v = 0.0;
    if (!parse_double(s, v) || v < 0.0 || v > 3600.0 * 1000.0) {
        return R::err(Error{kErrScriptSyntax, "bad duration '" + std::string(text) + "'"});
    }
    return R::ok(std::chrono::milliseconds(static_cast<long long>(v * unit)));
}

Result<std::vector<Command>> parse_script(std::string_view text,
                                          const std::map<std::string, std::string>& variables) {
    using R = Result<std::vector<Command>>;
    std::vector<Command> commands;
    std::istringstream in{std::string(text)};
    std::string raw_line;
    int line_no = 0;

    while (std::getline(in, raw_line)) {
        ++line_no;
        const std::string stripped = trim(strip_comment(raw_line));
        if (stripped.empty()) continue;
        auto substituted = substitute(stripped, variables, line_no);
        if (!substituted) return R::err(substituted.error());
        const std::string line = substituted.value();

        auto [verb, rest] = split_word(line);
        Command c;
        c.line = line_no;
        c.text = stripped;
        Options opts;

        if (verb == "connect") {
            c.kind = CommandKind::kConnect;
            const std::size_t colon = rest.rfind(':');
            std::uint32_t port = 0;
            if (colon == std::string::npos || !parse_uint(rest.substr(colon + 1), port) ||
                port > 65535) {
                return R::err(syntax(line_no, "connect needs HOST:PORT"));
            }
            c.host = rest.substr(0, colon);
            c.port = static_cast<std::uint16_t>(port);
        } else if (verb == "select" || verb == "deselect") {
            c.kind = verb == "select" ? CommandKind::kSelect : CommandKind::kDeselect;
            auto left = take_options(rest, opts, line_no);
            if (!left) return R::err(left.error());
            if (!trim(left.value()).empty())
                return R::err(syntax(line_no, "unexpected text after " + verb));
            c.status = opts.status;
        } else if (verb == "linktest") {
            c.kind = CommandKind::kLinktest;
        } else if (verb == "separate") {
            c.kind = CommandKind::kSeparate;
        } else if (verb == "disconnect") {
            c.kind = CommandKind::kDisconnect;
        } else if (verb == "send" || verb == "expect" || verb == "expect-no") {
            c.kind = verb == "send"     ? CommandKind::kSend
                     : verb == "expect" ? CommandKind::kExpect
                                        : CommandKind::kExpectNo;
            auto [sf, after] = split_word(rest);
            if (!parse_sf(sf, c.stream, c.function)) {
                return R::err(syntax(line_no, verb + " needs a message such as S1F13"));
            }
            auto left = take_options(after, opts, line_no);
            if (!left) return R::err(left.error());
            const std::string item_text = trim(left.value());
            c.w_bit = opts.w_bit;
            c.device = opts.device;
            if (opts.timeout) c.timeout = *opts.timeout;
            if (verb == "expect-no" && !opts.timeout) c.timeout = std::chrono::milliseconds(1000);
            if (c.kind == CommandKind::kSend) {
                if (!item_text.empty()) {
                    auto item = parse_item(item_text);
                    if (!item) return R::err(syntax(line_no, item.error().message));
                    c.body = std::move(item).value();
                }
            } else if (!item_text.empty()) {
                auto pattern = parse_pattern(item_text);
                if (!pattern) return R::err(syntax(line_no, pattern.error().message));
                c.pattern = std::move(pattern).value();
            }
        } else if (verb == "wait-event") {
            c.kind = CommandKind::kWaitEvent;
            auto [id, after] = split_word(rest);
            if (!parse_uint(id, c.id)) return R::err(syntax(line_no, "wait-event needs a CEID"));
            auto left = take_options(after, opts, line_no);
            if (!left) return R::err(left.error());
            if (opts.timeout) c.timeout = *opts.timeout;
            // What remains are FIELD=VALUE filters, for example WAFER_ID=W002.
            std::istringstream filters(left.value());
            std::string word;
            while (filters >> word) {
                const std::size_t eq = word.find('=');
                if (eq == std::string::npos || eq == 0 || eq + 1 == word.size()) {
                    return R::err(syntax(line_no, "wait-event filters look like FIELD=VALUE"));
                }
                std::string value = word.substr(eq + 1);
                if (value.size() >= 2 && value.front() == '"' && value.back() == '"') {
                    value = value.substr(1, value.size() - 2);
                }
                c.event_filters.emplace_back(word.substr(0, eq), value);
            }
        } else if (verb == "wait-alarm") {
            c.kind = CommandKind::kWaitAlarm;
            auto [id, after] = split_word(rest);
            auto [state, after2] = split_word(after);
            if (!parse_uint(id, c.id) || (state != "set" && state != "clear")) {
                return R::err(syntax(line_no, "wait-alarm needs ALID and set|clear"));
            }
            c.alarm_set = state == "set";
            auto left = take_options(after2, opts, line_no);
            if (!left || !trim(left.value()).empty())
                return R::err(syntax(line_no, "bad wait-alarm options"));
            if (opts.timeout) c.timeout = *opts.timeout;
        } else if (verb == "expect-closed") {
            c.kind = CommandKind::kExpectClosed;
            auto left = take_options(rest, opts, line_no);
            if (!left || !trim(left.value()).empty())
                return R::err(syntax(line_no, "bad expect-closed options"));
            if (opts.timeout) c.timeout = *opts.timeout;
        } else if (verb == "expect-status") {
            c.kind = CommandKind::kExpectStatus;
            auto [id, after] = split_word(rest);
            if (!parse_uint(id, c.id))
                return R::err(syntax(line_no, "expect-status needs an SVID"));
            auto left = take_options(after, opts, line_no);
            if (!left) return R::err(left.error());
            auto pattern = parse_pattern(trim(left.value()));
            if (!pattern) return R::err(syntax(line_no, pattern.error().message));
            c.pattern = std::move(pattern).value();
            if (opts.timeout) c.timeout = *opts.timeout;
        } else if (verb == "assert") {
            c.kind = CommandKind::kAssert;
            auto [field, after] = split_word(rest);
            if (field.rfind("event.", 0) != 0 && field.rfind("alarm.", 0) != 0) {
                return R::err(syntax(line_no, "assert needs event.FIELD or alarm.FIELD"));
            }
            c.field = field;
            auto [op, operand] = split_word(after);
            if (op == "within") {
                c.op = AssertOp::kWithin;
                auto [center, tol] = split_word(operand);
                std::string t = trim(tol);
                if (t.rfind("+-", 0) == 0)
                    t = trim(std::string_view(t).substr(2));
                else
                    return R::err(syntax(line_no, "within needs CENTER +-TOLERANCE"));
                if (!parse_double(center, c.number) || !parse_double(t, c.tolerance) ||
                    c.tolerance < 0.0) {
                    return R::err(syntax(line_no, "within needs numbers"));
                }
            } else if (op == "==" || op == "!=") {
                c.op = op == "==" ? AssertOp::kEqual : AssertOp::kNotEqual;
                c.value = operand;
                if (c.value.size() >= 2 && c.value.front() == '"' && c.value.back() == '"') {
                    c.value = c.value.substr(1, c.value.size() - 2);
                } else if (parse_double(operand, c.number)) {
                    c.value_is_number = true;
                }
                if (operand.empty()) return R::err(syntax(line_no, "a value is missing"));
            } else {
                return R::err(syntax(line_no, "assert supports within, == and !="));
            }
        } else if (verb == "auto-ack") {
            c.kind = CommandKind::kAutoAck;
            if (rest != "on" && rest != "off")
                return R::err(syntax(line_no, "auto-ack needs on or off"));
            c.flag = rest == "on";
        } else if (verb == "sleep") {
            c.kind = CommandKind::kSleep;
            auto d = parse_duration(rest);
            if (!d) return R::err(syntax(line_no, d.error().message));
            c.timeout = d.value();
        } else if (verb == "raw") {
            c.kind = CommandKind::kRaw;
            std::istringstream words(rest);
            std::string w;
            while (words >> w) {
                std::uint32_t v = 0;
                const std::string hex = w.rfind("0x", 0) == 0 ? w : "0x" + w;
                if (!parse_uint(hex, v) || v > 255)
                    return R::err(syntax(line_no, "raw needs hex bytes"));
                c.raw.push_back(static_cast<std::uint8_t>(v));
            }
            if (c.raw.empty()) return R::err(syntax(line_no, "raw needs at least one byte"));
        } else {
            return R::err(syntax(line_no, "unknown command '" + verb + "'"));
        }
        commands.push_back(std::move(c));
    }
    return R::ok(std::move(commands));
}

}  // namespace ssim::host_sim
