#pragma once

// Thread-safety: Not thread-safe. A Session is driven from one thread only
// (the HSMS I/O thread) and touches no clock, socket or lock: the caller
// passes the current time into every call and acts on the returned
// SessionOutput. That is what makes every timer testable on a FakeClock with
// no sleeps (C8, NFR-TST-1).
//
// FR-HSMS-3/4/5, PRD 6.5 and 8.6.1. The machine is the passive entity: the
// host connects and selects. Timers (defaults in SessionConfig):
//   T7  connected but not selected for too long      -> close
//   T8  a frame stalls part-way through              -> close
//   T6  a control transaction (our Linktest.req) got no reply -> close
//   T3  a data message we sent with the W-bit got no reply     -> reported in
//       SessionOutput::timed_out; the connection stays up (Day 5 turns it
//       into S9F9 and event handling)
//
// Reply matching (FR-HSMS-5): a data message sent with the W-bit opens a
// transaction keyed by its system bytes; several can be open at once, and an
// incoming even-function message with the same system bytes closes it.
//
// Values marked TODO(verify) are not confirmed by the sources available
// (docs/protocol-notes.md).

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "ssim/core/config.hpp"
#include "ssim/core/result.hpp"
#include "ssim/secsgem/bytes.hpp"
#include "ssim/secsgem/hsms/connection_state.hpp"
#include "ssim/secsgem/hsms/frame.hpp"
#include "ssim/secsgem/hsms/frame_decoder.hpp"
#include "ssim/secsgem/hsms/timers.hpp"
#include "ssim/secsgem/secs2/message.hpp"

namespace ssim::secsgem::hsms {

// Reject.req reason codes. Only 4 is confirmed against secsgem 0.3.0.
constexpr std::uint8_t kRejectSTypeNotSupported = 1;   // TODO(verify)
constexpr std::uint8_t kRejectPTypeNotSupported = 2;   // TODO(verify)
constexpr std::uint8_t kRejectTransactionNotOpen = 3;  // TODO(verify)
constexpr std::uint8_t kRejectEntityNotSelected = 4;

// Select.rsp / Deselect.rsp status (header byte 3). TODO(verify): secsgem
// always sends 0, so these values are not confirmed by it.
constexpr std::uint8_t kSelectOk = 0;
constexpr std::uint8_t kSelectAlreadyActive = 1;
constexpr std::uint8_t kDeselectOk = 0;
constexpr std::uint8_t kDeselectNotSelected = 1;

// Stable reason codes for send_request()/send_reply().
constexpr int kErrNotSelected = 410;
constexpr int kErrTooManyTransactions = 411;
constexpr int kErrMessageTooLarge = 412;

struct SessionConfig {
    Duration t3{45000};
    Duration t6{5000};
    Duration t7{10000};
    Duration t8{5000};
    Duration linktest_interval{60000};  // idle time before we send Linktest.req
    std::size_t max_frame_bytes = 1048576;
    std::size_t max_open_transactions = 256;  // bound (rule C5)
    std::uint16_t device_id = 0;              // session id used in data messages we send
    std::uint32_t first_system_bytes =
        1;  // where the generator starts (tests use it to hit wrap-around)
};

// Builds a SessionConfig from the comm section of the machine configuration.
SessionConfig session_config_from(const ssim::core::CommConfig& comm);

struct Delivery {
    Frame frame;                            // a data message for the layer above
    std::optional<std::uint32_t> reply_to;  // set if it answers one of our open requests
};

struct SessionOutput {
    std::vector<std::vector<std::uint8_t>> to_send;  // encoded frames, in order
    std::vector<Delivery> deliveries;
    std::vector<std::uint32_t> timed_out;  // system bytes of requests whose T3 expired
    bool close = false;                    // the connection must be closed now
    std::string close_reason;
};

class Session {
public:
    explicit Session(SessionConfig config);

    ConnectionState state() const { return fsm_.state(); }
    std::size_t open_transactions() const { return transactions_.size(); }

    // A TCP connection was accepted: NOT_CONNECTED -> NOT_SELECTED, T7 starts.
    // Ignored (empty output) if already connected.
    SessionOutput on_connect(TimePoint now);

    // Bytes arrived from the peer.
    SessionOutput on_bytes(TimePoint now, ByteSpan bytes);

    // Time passed. Call regularly; expiry does not depend on how often.
    SessionOutput on_tick(TimePoint now);

    // The connection ended (peer closed, socket error, or we closed it).
    void on_disconnect();

    // Sends a data message. If msg.w_bit is set, a transaction is opened (T3
    // starts) and its system bytes are returned; otherwise the system bytes
    // are just the ones used. Only allowed while SELECTED.
    [[nodiscard]] ssim::core::Result<std::uint32_t> send_request(TimePoint now,
                                                                 const secs2::Message& message,
                                                                 SessionOutput& out);

    // Sends the reply to a received request, echoing its system bytes.
    [[nodiscard]] ssim::core::Result<bool> send_reply(std::uint32_t system_bytes,
                                                      const secs2::Message& message,
                                                      SessionOutput& out);

private:
    void handle_frame(TimePoint now, const Frame& frame, SessionOutput& out);
    void reject(const Frame& offending, std::uint8_t reason, SessionOutput& out);
    void send_control(SType stype, std::uint32_t system_bytes, std::uint8_t byte2,
                      std::uint8_t byte3, SessionOutput& out);
    void close_connection(std::string reason, SessionOutput& out);
    void reset_link_state();
    std::uint32_t allocate_system_bytes();
    ssim::core::Result<bool> queue_data(const secs2::Message& message, std::uint32_t system_bytes,
                                        SessionOutput& out);

    SessionConfig config_;
    ConnectionStateMachine fsm_;
    FrameDecoder decoder_;
    Timer t7_;
    Timer t8_;
    Timer t6_;
    TimePoint last_activity_{};
    std::optional<std::uint32_t> pending_linktest_;
    std::map<std::uint32_t, TimePoint> transactions_;  // system bytes -> T3 deadline
    std::uint32_t next_system_bytes_;
};

}  // namespace ssim::secsgem::hsms
