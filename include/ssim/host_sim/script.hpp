#pragma once

// Thread-safety: pure functions over value types.
//
// FR-HOST-1 / PRD 8.7. The host simulator's script language, one command per
// line; the full grammar with examples is docs/scenario-format.md.
//
//   # comment                          (a '#' outside a string starts a comment)
//   connect HOST:PORT
//   select [status=N]                  deselect [status=N]      linktest      separate
//   send S1F13 [nowait|W] [device=N] [ITEM]
//   expect S1F14 [timeout=5s] [PATTERN]
//   expect-no S6F11 [timeout=1s]
//   wait-event CEID [timeout=5s]
//   wait-alarm ALID set|clear [timeout=5s]
//   expect-closed [timeout=5s]
//   expect-status SVID [timeout=5s] PATTERN
//   assert event.FIELD within CENTER +-TOLERANCE   |   == VALUE   |   != VALUE
//   assert alarm.ALID|ALCD|ALTX  == VALUE
//   auto-ack on|off        sleep 500ms|2s        raw HEXBYTES        disconnect
//
// $NAME in a line is replaced by a value given to the parser (for example
// $PORT), so one script serves any port.

#include <chrono>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "ssim/core/result.hpp"
#include "ssim/host_sim/pattern.hpp"
#include "ssim/secsgem/secs2/item.hpp"

namespace ssim::host_sim {

constexpr int kErrScriptSyntax = 601;

enum class CommandKind {
    kConnect,
    kSelect,
    kDeselect,
    kLinktest,
    kSeparate,
    kSend,
    kExpect,
    kExpectNo,
    kWaitEvent,
    kWaitAlarm,
    kExpectClosed,
    kExpectStatus,
    kAssert,
    kAutoAck,
    kSleep,
    kRaw,
    kDisconnect,
};

enum class AssertOp { kWithin, kEqual, kNotEqual };

struct Command {
    CommandKind kind = CommandKind::kDisconnect;
    int line = 0;
    std::string text;  // the line as written, for messages

    std::string host;           // connect
    std::uint16_t port = 0;     // connect
    std::optional<int> status;  // select / deselect: expected status
    int stream = 0;             // send / expect / expect-no
    int function = 0;
    std::optional<bool> w_bit;                       // send: explicit W or nowait
    std::optional<int> device;                       // send: device id override
    std::optional<ssim::secsgem::secs2::Item> body;  // send
    std::optional<Pattern> pattern;                  // expect / expect-status
    std::chrono::milliseconds timeout{5000};
    std::uint32_t id = 0;   // wait-event CEID, wait-alarm ALID, expect-status SVID
    bool alarm_set = true;  // wait-alarm
    std::string field;      // assert: "event.STRESS_MPA", "alarm.ALID"
    AssertOp op = AssertOp::kEqual;
    double number = 0.0;     // assert within: center; == with a number
    double tolerance = 0.0;  // assert within
    std::string value;       // assert ==/!= with text
    bool value_is_number = false;
    bool flag = true;  // auto-ack
    std::vector<std::uint8_t> raw;
};

[[nodiscard]] ssim::core::Result<std::vector<Command>> parse_script(
    std::string_view text, const std::map<std::string, std::string>& variables = {});

// "500ms", "2s", "1.5s". A bare number is seconds.
[[nodiscard]] ssim::core::Result<std::chrono::milliseconds> parse_duration(std::string_view text);

}  // namespace ssim::host_sim
