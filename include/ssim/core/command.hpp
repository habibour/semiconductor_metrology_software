#pragma once

// Thread-safety: Command and its variant alternatives are plain value
// types — safe to construct on one thread and push into the controller's
// command queue for another thread to pop.
//
// FR-MC-2 / PRD §6.6 (Command pattern): every command that can change
// machine state enters through one queue, carrying its source and a
// correlation id so logs/replays can tie a command to its effects
// regardless of whether it came from the UI, the CLI or (Day 5) SECS/GEM.

#include <cstdint>
#include <string>
#include <variant>

namespace ssim::core {

enum class CommandSource { kUi, kCli, kSecsGem };

const char* to_string(CommandSource source);

using CorrelationId = std::uint64_t;

struct StartCommand {
    std::string wafer_id;
    int slot = 0;
};

struct StopCommand {};
struct AbortCommand {};
struct ClearAlarmCommand {};

enum class ControlMode { kOffline, kOnlineLocal, kOnlineRemote };

const char* to_string(ControlMode mode);

struct SetControlModeCommand {
    ControlMode mode;
};

using CommandPayload =
    std::variant<StartCommand, StopCommand, AbortCommand, ClearAlarmCommand,
                SetControlModeCommand>;

struct Command {
    CommandPayload payload;
    CommandSource source;
    CorrelationId correlation_id;
};

}  // namespace ssim::core
