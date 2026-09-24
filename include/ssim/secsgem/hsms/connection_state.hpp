#pragma once

// Thread-safety: Not thread-safe; owned by the session.
//
// PRD 6.5, HSMS connection state. Same style as the process state machine in
// ssim_core (CLAUDE.md 6.3): an illegal trigger leaves the state unchanged and
// returns a stable reason code, it never throws. The table matches the
// open-source secsgem 0.3.0 model (docs/protocol-notes.md).
//
//   NotConnected --Connect--> NotSelected --Select--> Selected
//   Selected --Deselect--> NotSelected
//   NotSelected --T7Timeout--> NotConnected
//   NotSelected | Selected --Disconnect--> NotConnected

#include "ssim/core/result.hpp"

namespace ssim::secsgem::hsms {

enum class ConnectionState { kNotConnected, kNotSelected, kSelected };

const char* to_string(ConnectionState state);

enum class ConnectionTrigger { kConnect, kSelect, kDeselect, kDisconnect, kT7Timeout };

const char* to_string(ConnectionTrigger trigger);

constexpr int kReasonIllegalConnectionTransition = 450;

class ConnectionStateMachine {
public:
    ConnectionState state() const { return state_; }

    // On success returns the new state; on failure the state is unchanged.
    [[nodiscard]] ssim::core::Result<ConnectionState> apply(ConnectionTrigger trigger);

private:
    ConnectionState state_ = ConnectionState::kNotConnected;
};

}  // namespace ssim::secsgem::hsms
