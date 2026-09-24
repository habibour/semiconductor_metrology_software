#pragma once

// Thread-safety: every callback runs on the HSMS I/O thread ("hsms_io"). They
// must return quickly and must not block or throw; anything slow should be
// handed to another thread through a queue (rule C2).
//
// The seam between the HSMS layer and whatever sits above it (Adapter
// pattern, PRD 6.6). Today the only implementer is a test handler; on Day 5
// the GEM module implements it and forwards into the core command queue.

#include <cstdint>
#include <string>

#include "ssim/secsgem/hsms/connection_state.hpp"
#include "ssim/secsgem/hsms/session.hpp"

namespace ssim::secsgem::hsms {

class IHsmsHandler {
public:
    virtual ~IHsmsHandler() = default;

    // A data message arrived while selected. delivery.reply_to is set when it
    // answers one of the machine's own requests.
    virtual void on_data(const Delivery& delivery) = 0;

    // The connection state changed (a host connected, selected, deselected or
    // went away).
    virtual void on_session_state(ConnectionState) {}

    // A request sent with the W-bit got no reply within T3.
    virtual void on_transaction_timeout(std::uint32_t /*system_bytes*/) {}

    // Something the server could not do (a send that failed, an exception on
    // the I/O thread). Diagnostic only.
    virtual void on_error(const std::string& /*what*/) {}
};

}  // namespace ssim::secsgem::hsms
