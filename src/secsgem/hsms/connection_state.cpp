#include "ssim/secsgem/hsms/connection_state.hpp"

#include <string>

namespace ssim::secsgem::hsms {

const char* to_string(ConnectionState state) {
    switch (state) {
        case ConnectionState::kNotConnected:
            return "NOT_CONNECTED";
        case ConnectionState::kNotSelected:
            return "NOT_SELECTED";
        case ConnectionState::kSelected:
            return "SELECTED";
    }
    return "?";
}

const char* to_string(ConnectionTrigger trigger) {
    switch (trigger) {
        case ConnectionTrigger::kConnect:
            return "Connect";
        case ConnectionTrigger::kSelect:
            return "Select";
        case ConnectionTrigger::kDeselect:
            return "Deselect";
        case ConnectionTrigger::kDisconnect:
            return "Disconnect";
        case ConnectionTrigger::kT7Timeout:
            return "T7Timeout";
    }
    return "?";
}

ssim::core::Result<ConnectionState> ConnectionStateMachine::apply(ConnectionTrigger trigger) {
    using R = ssim::core::Result<ConnectionState>;
    ConnectionState next = state_;
    bool legal = false;
    switch (trigger) {
        case ConnectionTrigger::kConnect:
            legal = state_ == ConnectionState::kNotConnected;
            next = ConnectionState::kNotSelected;
            break;
        case ConnectionTrigger::kSelect:
            legal = state_ == ConnectionState::kNotSelected;
            next = ConnectionState::kSelected;
            break;
        case ConnectionTrigger::kDeselect:
            legal = state_ == ConnectionState::kSelected;
            next = ConnectionState::kNotSelected;
            break;
        case ConnectionTrigger::kDisconnect:
            legal = state_ != ConnectionState::kNotConnected;
            next = ConnectionState::kNotConnected;
            break;
        case ConnectionTrigger::kT7Timeout:
            legal = state_ == ConnectionState::kNotSelected;
            next = ConnectionState::kNotConnected;
            break;
    }
    if (!legal) {
        return R::err(
            {kReasonIllegalConnectionTransition,
             std::string(to_string(trigger)) + " is not allowed in " + to_string(state_)});
    }
    state_ = next;
    return R::ok(state_);
}

}  // namespace ssim::secsgem::hsms
