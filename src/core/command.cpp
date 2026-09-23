#include "ssim/core/command.hpp"

namespace ssim::core {

const char* to_string(CommandSource source) {
    switch (source) {
        case CommandSource::kUi:
            return "Ui";
        case CommandSource::kCli:
            return "Cli";
        case CommandSource::kSecsGem:
            return "SecsGem";
    }
    return "Unknown";
}

const char* to_string(ControlMode mode) {
    switch (mode) {
        case ControlMode::kOffline:
            return "Offline";
        case ControlMode::kOnlineLocal:
            return "OnlineLocal";
        case ControlMode::kOnlineRemote:
            return "OnlineRemote";
    }
    return "Unknown";
}

}  // namespace ssim::core
