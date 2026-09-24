#include "ssim/host_sim/runner.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <deque>
#include <optional>
#include <thread>

#include "ssim/host_sim/host_client.hpp"
#include "ssim/secsgem/gem/identifiers.hpp"
#include "ssim/secsgem/gem/messages.hpp"
#include "ssim/secsgem/hsms/data_message.hpp"
#include "ssim/secsgem/secs2/codec.hpp"
#include "ssim/secsgem/secs2/text_dump.hpp"

namespace ssim::host_sim {

namespace {

using Clock = std::chrono::steady_clock;
using std::chrono::milliseconds;
namespace hsms = ssim::secsgem::hsms;
namespace gem = ssim::secsgem::gem;
namespace secs2 = ssim::secsgem::secs2;

struct Received {
    hsms::Frame frame;
    secs2::Message message;
};

std::string sf_name(int stream, int function) {
    return "S" + std::to_string(stream) + "F" + std::to_string(function);
}

std::optional<double> as_number(const secs2::Item& item) {
    if (item.is_list() || item.count() == 0) return std::nullopt;
    switch (item.format()) {
        case secs2::Format::kU1:
        case secs2::Format::kBinary:
        case secs2::Format::kBoolean:
            return static_cast<double>(item.as_array<std::uint8_t>()[0]);
        case secs2::Format::kU2:
            return static_cast<double>(item.as_array<std::uint16_t>()[0]);
        case secs2::Format::kU4:
            return static_cast<double>(item.as_array<std::uint32_t>()[0]);
        case secs2::Format::kU8:
            return static_cast<double>(item.as_array<std::uint64_t>()[0]);
        case secs2::Format::kI1:
            return static_cast<double>(item.as_array<std::int8_t>()[0]);
        case secs2::Format::kI2:
            return static_cast<double>(item.as_array<std::int16_t>()[0]);
        case secs2::Format::kI4:
            return static_cast<double>(item.as_array<std::int32_t>()[0]);
        case secs2::Format::kI8:
            return static_cast<double>(item.as_array<std::int64_t>()[0]);
        case secs2::Format::kF4:
            return static_cast<double>(item.as_array<float>()[0]);
        case secs2::Format::kF8:
            return item.as_array<double>()[0];
        default:
            return std::nullopt;
    }
}

// A field of an event, looked up by its name in the report layouts of PRD 8.6.5.
std::optional<secs2::Item> find_event_field(const gem::EventReport& event,
                                            const std::string& name) {
    for (const gem::Report& report : event.reports) {
        const auto& fields = gem::report_fields(report.rptid);
        const auto it = std::find(fields.begin(), fields.end(), name);
        if (it != fields.end()) {
            const auto index = static_cast<std::size_t>(it - fields.begin());
            if (index < report.values.size()) return report.values[index];
            return std::nullopt;
        }
    }
    return std::nullopt;
}

// True if the item is the given text (ASCII), number, or true/false.
bool item_equals_text(const secs2::Item& item, const std::string& text) {
    if (item.format() == secs2::Format::kAscii) return item.as_ascii() == text;
    const std::optional<double> number = as_number(item);
    if (!number) return false;
    if (item.format() == secs2::Format::kBoolean) return (*number != 0.0) == (text == "true");
    char* end = nullptr;
    const double wanted = std::strtod(text.c_str(), &end);
    return end != text.c_str() && *end == '\0' && std::fabs(*number - wanted) < 1e-9;
}

class Runner {
public:
    explicit Runner(const RunOptions& options) : options_(options) {}

    RunResult run(const std::vector<Command>& commands) {
        for (const Command& c : commands) {
            message_.clear();
            if (!execute(c)) {
                client_.close();
                return RunResult{false, c.line, c.text + ": " + message_};
            }
        }
        if (!queue_.empty()) {
            log("(" + std::to_string(queue_.size()) +
                " received message(s) were never consumed by the script)");
        }
        client_.close();
        return RunResult{true, 0, {}};
    }

private:
    // ---- plumbing ----------------------------------------------------------
    void log(const std::string& line) {
        if (options_.log) options_.log(line);
    }

    bool fail(const std::string& why) {
        message_ = why;
        return false;
    }

    bool send_frame(const hsms::Frame& frame, const std::string& what) {
        auto bytes = hsms::encode_frame(frame);
        if (!bytes) return fail(bytes.error().message);
        log(">> " + what);
        auto sent = client_.send_bytes(bytes.value());
        if (!sent) return fail(sent.error().message);
        return true;
    }

    bool send_control(hsms::SType stype, std::uint32_t sys, std::uint8_t byte3 = 0) {
        return send_frame(hsms::make_control_frame(stype, sys, 0, byte3),
                          std::string(hsms::to_string(stype)) + " sys=" + std::to_string(sys));
    }

    void on_frame(const hsms::Frame& f) {
        const auto stype = hsms::stype_from_byte(f.header.stype);
        if (f.header.stype != 0) {
            const std::string name =
                stype ? hsms::to_string(*stype) : "SType " + std::to_string(f.header.stype);
            log("<< " + name + " sys=" + std::to_string(f.header.system_bytes) + " byte2=" +
                std::to_string(f.header.byte2) + " byte3=" + std::to_string(f.header.byte3));
            if (stype == hsms::SType::kLinktestReq) {
                (void)send_control(hsms::SType::kLinktestRsp, f.header.system_bytes);
            }
            control_.push_back(f);
            return;
        }
        auto message = hsms::to_message(f);
        if (!message) {
            log("<< (undecodable data message S" + std::to_string(f.header.stream()) + "F" +
                std::to_string(f.header.function()) + ": " + message.error().message + ")");
            return;
        }
        log("<< " + secs2::to_text(message.value()));
        if (auto_ack_ && message.value().w_bit) {
            const auto& m = message.value();
            if (m.stream == 6 && m.function == 11) {
                reply(f, gem::make_s6f12(gem::kAckAccepted));
            } else if (m.stream == 5 && m.function == 1) {
                reply(f, gem::make_s5f2(gem::kAckAccepted));
            }
        }
        queue_.push_back(Received{f, std::move(message).value()});
    }

    void reply(const hsms::Frame& request, const secs2::Message& message) {
        std::vector<std::uint8_t> body;
        if (message.body) body = secs2::encode(*message.body).value();
        const hsms::Frame frame =
            hsms::make_data_frame(request.header.session_id, message.stream, message.function,
                                  false, request.header.system_bytes, std::move(body));
        (void)send_frame(frame, secs2::to_text(message) + " (auto-ack)");
    }

    HostClient::ReadStatus pump(milliseconds timeout) {
        std::vector<hsms::Frame> frames;
        const auto status = client_.read(timeout, frames);
        for (const hsms::Frame& f : frames) on_frame(f);
        if (status == HostClient::ReadStatus::kClosed) closed_ = true;
        return status;
    }

    template <typename Pred>
    bool wait_message(Pred pred, milliseconds timeout, Received& out, const std::string& what) {
        const auto deadline = Clock::now() + timeout;
        for (;;) {
            for (auto it = queue_.begin(); it != queue_.end(); ++it) {
                if (pred(*it)) {
                    out = *it;
                    queue_.erase(it);
                    return true;
                }
            }
            if (closed_) return fail("the connection closed while waiting for " + what);
            const auto left = std::chrono::duration_cast<milliseconds>(deadline - Clock::now());
            if (left.count() <= 0) {
                return fail("timed out after " + std::to_string(timeout.count()) +
                            " ms waiting for " + what);
            }
            pump(left);
        }
    }

    bool wait_control(hsms::SType stype, std::uint32_t sys, milliseconds timeout,
                      hsms::Frame& out) {
        const auto deadline = Clock::now() + timeout;
        for (;;) {
            for (auto it = control_.begin(); it != control_.end(); ++it) {
                if (it->header.stype == static_cast<std::uint8_t>(stype) &&
                    it->header.system_bytes == sys) {
                    out = *it;
                    control_.erase(it);
                    return true;
                }
            }
            if (closed_)
                return fail(std::string("the connection closed while waiting for ") +
                            hsms::to_string(stype));
            const auto left = std::chrono::duration_cast<milliseconds>(deadline - Clock::now());
            if (left.count() <= 0)
                return fail(std::string("timed out waiting for ") + hsms::to_string(stype));
            pump(left);
        }
    }

    bool matches_body(const std::optional<Pattern>& pattern, const secs2::Message& m) {
        if (!pattern) return true;
        if (!m.body) return pattern->kind == Pattern::Kind::kAnyItem;
        return matches(*pattern, *m.body);
    }

    std::string body_text(const secs2::Message& m) {
        return m.body ? secs2::to_text(*m.body) : "(no body)";
    }

    // ---- commands ----------------------------------------------------------
    bool execute(const Command& c) {
        switch (c.kind) {
            case CommandKind::kConnect:
                return do_connect(c);
            case CommandKind::kSelect:
                return do_handshake(hsms::SType::kSelectReq, hsms::SType::kSelectRsp,
                                    c.status.value_or(0));
            case CommandKind::kDeselect:
                return do_handshake(hsms::SType::kDeselectReq, hsms::SType::kDeselectRsp,
                                    c.status.value_or(0));
            case CommandKind::kLinktest:
                return do_handshake(hsms::SType::kLinktestReq, hsms::SType::kLinktestRsp, 0);
            case CommandKind::kSeparate:
                return send_control(hsms::SType::kSeparateReq, next_sys_++);
            case CommandKind::kSend:
                return do_send(c);
            case CommandKind::kExpect:
                return do_expect(c);
            case CommandKind::kExpectNo:
                return do_expect_no(c);
            case CommandKind::kWaitEvent:
                return do_wait_event(c);
            case CommandKind::kWaitAlarm:
                return do_wait_alarm(c);
            case CommandKind::kExpectClosed:
                return do_expect_closed(c);
            case CommandKind::kExpectStatus:
                return do_expect_status(c);
            case CommandKind::kAssert:
                return do_assert(c);
            case CommandKind::kAutoAck:
                auto_ack_ = c.flag;
                return true;
            case CommandKind::kSleep:
                return do_sleep(c.timeout);
            case CommandKind::kRaw:
                return do_raw(c);
            case CommandKind::kDisconnect:
                client_.close();
                closed_ = true;
                return true;
        }
        return fail("unhandled command");
    }

    bool do_connect(const Command& c) {
        auto connected = client_.connect(c.host, c.port);
        if (!connected) return fail(connected.error().message);
        closed_ = false;
        queue_.clear();
        control_.clear();
        log("connected to " + c.host + ":" + std::to_string(c.port));
        return true;
    }

    bool do_handshake(hsms::SType request, hsms::SType response, int expected_status) {
        const std::uint32_t sys = next_sys_++;
        if (!send_control(request, sys)) return false;
        hsms::Frame rsp;
        if (!wait_control(response, sys, milliseconds(5000), rsp)) return false;
        if (response != hsms::SType::kLinktestRsp && rsp.header.byte3 != expected_status) {
            return fail(std::string(hsms::to_string(response)) + " status " +
                        std::to_string(rsp.header.byte3) + ", expected " +
                        std::to_string(expected_status));
        }
        return true;
    }

    bool do_send(const Command& c) {
        secs2::Message m;
        m.stream = static_cast<std::uint8_t>(c.stream);
        m.function = static_cast<std::uint8_t>(c.function);
        m.w_bit = c.w_bit.value_or(c.function % 2 == 1);  // primaries ask for a reply
        m.body = c.body;
        std::vector<std::uint8_t> body;
        if (m.body) {
            auto encoded = secs2::encode(*m.body);
            if (!encoded) return fail(encoded.error().message);
            body = std::move(encoded).value();
        }
        const std::uint32_t sys = next_sys_++;
        const auto device = static_cast<std::uint16_t>(c.device.value_or(options_.device_id));
        return send_frame(
            hsms::make_data_frame(device, m.stream, m.function, m.w_bit, sys, std::move(body)),
            secs2::to_text(m) + " sys=" + std::to_string(sys));
    }

    bool do_expect(const Command& c) {
        Received r;
        const std::string what = sf_name(c.stream, c.function);
        if (!wait_message(
                [&](const Received& x) {
                    return x.message.stream == c.stream && x.message.function == c.function;
                },
                c.timeout, r, what)) {
            return false;
        }
        if (!matches_body(c.pattern, r.message)) {
            return fail(what + " arrived but its body " + body_text(r.message) +
                        " does not match the pattern");
        }
        return true;
    }

    bool do_expect_no(const Command& c) {
        Received r;
        const bool got = wait_message(
            [&](const Received& x) {
                return x.message.stream == c.stream && x.message.function == c.function;
            },
            c.timeout, r, sf_name(c.stream, c.function));
        message_.clear();
        if (got) return fail("unexpected " + secs2::to_text(r.message));
        return true;
    }

    bool do_wait_event(const Command& c) {
        Received r;
        std::optional<gem::EventReport> parsed;
        std::string what = "event " + std::to_string(c.id) + " " + gem::ceid_name(c.id);
        for (const auto& [field, wanted] : c.event_filters) what += " " + field + "=" + wanted;
        if (!wait_message(
                [&](const Received& x) {
                    if (x.message.stream != 6 || x.message.function != 11) return false;
                    auto e = gem::parse_s6f11(x.message);
                    if (!e || e.value().ceid != c.id) return false;
                    for (const auto& [field, wanted] : c.event_filters) {
                        const auto value = find_event_field(e.value(), field);
                        if (!value || !item_equals_text(*value, wanted)) return false;
                    }
                    parsed = e.value();
                    return true;
                },
                c.timeout, r, what)) {
            return false;
        }
        last_event_ = parsed;
        return true;
    }

    bool do_wait_alarm(const Command& c) {
        Received r;
        std::optional<gem::AlarmReport> parsed;
        const std::string what =
            std::string("alarm ") + std::to_string(c.id) + (c.alarm_set ? " set" : " clear");
        if (!wait_message(
                [&](const Received& x) {
                    if (x.message.stream != 5 || x.message.function != 1) return false;
                    auto a = gem::parse_s5f1(x.message);
                    if (!a || a.value().alid != c.id) return false;
                    if (((a.value().alcd & gem::kAlcdSetBit) != 0) != c.alarm_set) return false;
                    parsed = a.value();
                    return true;
                },
                c.timeout, r, what)) {
            return false;
        }
        last_alarm_ = parsed;
        return true;
    }

    bool do_expect_closed(const Command& c) {
        const auto deadline = Clock::now() + c.timeout;
        while (!closed_) {
            const auto left = std::chrono::duration_cast<milliseconds>(deadline - Clock::now());
            if (left.count() <= 0) return fail("the machine did not close the connection");
            pump(left);
        }
        return true;
    }

    bool do_expect_status(const Command& c) {
        const auto deadline = Clock::now() + c.timeout;
        std::string last = "(no answer)";
        for (;;) {
            const std::uint32_t sys = next_sys_++;
            const secs2::Message ask = gem::make_s1f3({c.id});
            std::vector<std::uint8_t> body = secs2::encode(*ask.body).value();
            if (!send_frame(hsms::make_data_frame(static_cast<std::uint16_t>(options_.device_id), 1,
                                                  3, true, sys, body),
                            secs2::to_text(ask) + " sys=" + std::to_string(sys))) {
                return false;
            }
            Received r;
            if (!wait_message(
                    [&](const Received& x) {
                        return x.frame.header.system_bytes == sys && x.message.stream == 1 &&
                               x.message.function == 4;
                    },
                    milliseconds(5000), r, "S1F4")) {
                return false;
            }
            if (r.message.body && r.message.body->is_list() && r.message.body->count() == 1) {
                last = secs2::to_text(r.message.body->as_list()[0]);
                if (c.pattern && matches(*c.pattern, r.message.body->as_list()[0])) return true;
            }
            if (Clock::now() >= deadline) {
                return fail("status variable " + std::to_string(c.id) + " was " + last +
                            ", never matched the pattern");
            }
            do_sleep(milliseconds(200));
        }
    }

    bool do_assert(const Command& c) {
        std::optional<secs2::Item> value;
        if (c.field.rfind("event.", 0) == 0) {
            if (!last_event_)
                return fail("no event yet: use wait-event before asserting on event.*");
            const std::string name = c.field.substr(6);
            value = find_event_field(*last_event_, name);
            if (!value) return fail("the last event has no field " + name);
        } else {
            if (!last_alarm_)
                return fail("no alarm yet: use wait-alarm before asserting on alarm.*");
            const std::string name = c.field.substr(6);
            if (name == "ALID")
                value = secs2::Item::u4(last_alarm_->alid);
            else if (name == "ALCD")
                value = secs2::Item::u1(last_alarm_->alcd);
            else if (name == "ALTX")
                value = secs2::Item::ascii(last_alarm_->altx);
            else
                return fail("unknown alarm field " + name + " (ALID, ALCD, ALTX)");
        }
        const std::optional<double> number = as_number(*value);
        const std::string shown = secs2::to_text(*value);
        if (c.op == AssertOp::kWithin) {
            if (!number) return fail(c.field + " is " + shown + ", not a number");
            if (std::fabs(*number - c.number) > c.tolerance) {
                return fail(c.field + " is " + shown + ", outside " + std::to_string(c.number) +
                            " +- " + std::to_string(c.tolerance));
            }
            return true;
        }
        bool equal = false;
        if (c.value_is_number) {
            equal = number && std::fabs(*number - c.number) < 1e-9;
        } else if (value->format() == secs2::Format::kAscii) {
            equal = value->as_ascii() == c.value;
        } else if (value->format() == secs2::Format::kBoolean) {
            equal = (number && *number != 0.0) == (c.value == "true");
        }
        const bool want_equal = c.op == AssertOp::kEqual;
        if (equal != want_equal) {
            return fail(c.field + " is " + shown +
                        (want_equal ? ", expected " : ", expected not ") +
                        (c.value_is_number ? std::to_string(c.number) : c.value));
        }
        return true;
    }

    // Sleeps while still reading, so events keep being acknowledged.
    bool do_sleep(milliseconds duration) {
        const auto end = Clock::now() + duration;
        while (Clock::now() < end) {
            const auto left = std::chrono::duration_cast<milliseconds>(end - Clock::now());
            if (client_.is_open() && !closed_) {
                pump(std::min<milliseconds>(left, milliseconds(50)));
            } else {
                std::this_thread::sleep_for(std::min<milliseconds>(left, milliseconds(50)));
            }
        }
        return true;
    }

    bool do_raw(const Command& c) {
        log(">> raw " + std::to_string(c.raw.size()) + " byte(s)");
        auto sent = client_.send_bytes(c.raw);
        if (!sent) return fail(sent.error().message);
        return true;
    }

    RunOptions options_;
    HostClient client_;
    std::deque<Received> queue_;
    std::deque<hsms::Frame> control_;
    bool auto_ack_ = true;
    bool closed_ = true;
    std::uint32_t next_sys_ = 1;
    std::optional<gem::EventReport> last_event_;
    std::optional<gem::AlarmReport> last_alarm_;
    std::string message_;
};

}  // namespace

RunResult run_script(const std::vector<Command>& commands, const RunOptions& options) {
    return Runner(options).run(commands);
}

}  // namespace ssim::host_sim
