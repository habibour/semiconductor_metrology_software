#pragma once

// Thread-safety: Thread-safe; pure functions over value types.
//
// FR-S2-3 / PRD 8.6.3. Typed builders and parsers for the GEM subset's
// messages, and the two catalogues that tell a MessageFactory which messages
// each side understands. The layouts were written from memory of public
// descriptions and are cross-checked against the secsgem library by the
// interop test (XT-SECSGEM-1); anything not yet confirmed is marked
// TODO(verify).
//
// Parsers take the message body and return errors as values, so a bad body
// becomes S9F7 (illegal data) rather than a crash. The catalogue's body checks
// use these same parsers, so "legal" and "parseable" cannot disagree.

#include <cstdint>
#include <string>
#include <vector>

#include "ssim/core/result.hpp"
#include "ssim/secsgem/secs2/item.hpp"
#include "ssim/secsgem/secs2/message.hpp"
#include "ssim/secsgem/secs2/message_factory.hpp"

namespace ssim::secsgem::gem {

constexpr int kErrIllegalData = 500;

struct Identity {
    std::string model;    // MDLN
    std::string softrev;  // SOFTREV
};

struct CommandParam {
    std::string name;
    secs2::Item value;
};

struct RemoteCommand {
    std::string rcmd;
    std::vector<CommandParam> params;
};

struct ParamAck {
    std::string name;
    std::uint8_t cpack = 0;
};

struct CommandAck {
    std::uint8_t hcack = 0;
    std::vector<ParamAck> params;
};

struct Report {
    std::uint32_t rptid = 0;
    std::vector<secs2::Item> values;
};

struct EventReport {
    std::uint32_t dataid = 0;
    std::uint32_t ceid = 0;
    std::vector<Report> reports;
};

struct AlarmReport {
    std::uint8_t alcd = 0;
    std::uint32_t alid = 0;
    std::string altx;
};

// ---- builders (message from data) ----------------------------------------
secs2::Message make_s1f1();
secs2::Message make_s1f2(const Identity& id);
secs2::Message make_s1f13();
secs2::Message make_s1f14(std::uint8_t commack, const Identity& id);
secs2::Message make_s1f3(const std::vector<std::uint32_t>& svids);
secs2::Message make_s1f4(std::vector<secs2::Item> values);
secs2::Message make_s1f15();
secs2::Message make_s1f16(std::uint8_t oflack);
secs2::Message make_s1f17();
secs2::Message make_s1f18(std::uint8_t onlack);
secs2::Message make_s2f41(const RemoteCommand& command);
secs2::Message make_s2f42(std::uint8_t hcack, const std::vector<ParamAck>& params);
secs2::Message make_s5f1(const AlarmReport& alarm);
secs2::Message make_s5f2(std::uint8_t ackc5);
secs2::Message make_s6f11(const EventReport& event);
secs2::Message make_s6f12(std::uint8_t ackc6);

// S9F1, S9F3, S9F5, S9F7, S9F9 or S9F11: body is the 10 header bytes of the
// message being complained about (MHEAD, or SHEAD for S9F9). Never asks for a
// reply.
secs2::Message make_s9(std::uint8_t function, const std::vector<std::uint8_t>& header_bytes);

// ---- parsers (data from message) -----------------------------------------
[[nodiscard]] ssim::core::Result<RemoteCommand> parse_s2f41(const secs2::Message& message);
[[nodiscard]] ssim::core::Result<CommandAck> parse_s2f42(const secs2::Message& message);
// An empty list means "all status variables".
[[nodiscard]] ssim::core::Result<std::vector<std::uint32_t>> parse_s1f3(
    const secs2::Message& message);
[[nodiscard]] ssim::core::Result<EventReport> parse_s6f11(const secs2::Message& message);
[[nodiscard]] ssim::core::Result<AlarmReport> parse_s5f1(const secs2::Message& message);
// A single-byte acknowledge (COMMACK, OFLACK, ONLACK, ACKC5, ACKC6).
[[nodiscard]] ssim::core::Result<std::uint8_t> parse_ack(const secs2::Message& message);

// ---- catalogues ------------------------------------------------------------
// What the machine accepts from a host: S1F1, S1F3, S1F13, S1F15, S1F17,
// S2F41, S5F2, S6F12. Anything else in a known stream is S9F5, an unknown
// stream is S9F3, a known message with a bad body is S9F7. FR-GEM-7 and 8
// (S2F13-16, S2F33-38) are not registered, so they answer S9F5 honestly.
void register_equipment_catalogue(secs2::MessageFactory& factory);

// What a host accepts from the machine: S1F2, S1F4, S1F14, S1F16, S1F18,
// S2F42, S5F1, S6F11 and the S9 messages.
void register_host_catalogue(secs2::MessageFactory& factory);

}  // namespace ssim::secsgem::gem
