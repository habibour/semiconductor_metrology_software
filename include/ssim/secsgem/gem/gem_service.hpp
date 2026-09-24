#pragma once

// Thread-safety: on_data(), on_session_state(), on_transaction_timeout() and
// on_error() run on the HSMS I/O thread, and so does every task the service
// posts to itself. All GEM state (communication state, open requests, counters)
// lives on that one thread and needs no lock. The only cross-thread entry is
// the set of EventBus subscriptions made in the constructor: they run on the
// controller or processing thread, copy what they need and hand a task to the
// I/O thread through IMessageSender::post_task. The destructor unsubscribes;
// destroy the service only after the server has stopped and the controller
// has stopped publishing.
//
// FR-GEM-1..6, FR-GEM-9, FR-MC-2 (SECS/GEM as a command source). PRD 6.5, 8.6.
// The service turns host messages into MachineApi calls tagged with the
// SECS/GEM source, and machine events and alarms into S6F11 and S5F1. It does
// not know about sockets.
//
// Behaviour summary:
//   S1F1   answered in every state (S1F2).
//   S1F13  answered in every state; enters Communicating.
//   S1F3   status variables (empty list = all; unknown SVID = empty item).
//   S1F15  operator-independent: goes Offline.  S1F17: back Online if allowed.
//   S2F41  START / STOP / ABORT / CLEAR_ALARM with HCACK and per-parameter CPACK.
//   Events (S6F11) and alarms (S5F1) are sent only while Communicating; the
//   ones raised while not communicating are logged and dropped (FR-HSMS-7).
//   Errors: S9F1 wrong device id, S9F3 unknown stream, S9F5 unknown function,
//   S9F7 illegal data, S9F9 our own request timed out (T3), S9F11 body too long.
//
// Deviations from a full GEM implementation, stated plainly: communication
// state is simplified (no WaitCRA/WaitDelay), host events are always enabled
// (no S2F37), reports are fixed (no S2F33/35), no spooling.

#include <cstddef>
#include <cstdint>
#include <map>
#include <vector>

#include "ssim/core/alarms.hpp"
#include "ssim/core/clock.hpp"
#include "ssim/core/config.hpp"
#include "ssim/core/event_bus.hpp"
#include "ssim/core/logger.hpp"
#include "ssim/core/machine_api.hpp"
#include "ssim/secsgem/gem/messages.hpp"
#include "ssim/secsgem/hsms/handler.hpp"
#include "ssim/secsgem/hsms/sender.hpp"
#include "ssim/secsgem/secs2/message_factory.hpp"

namespace ssim::secsgem::gem {

enum class CommState { kNotCommunicating, kCommunicating };

struct GemConfig {
    Identity identity{"SSIM-128", "0.1.0"};
    std::uint16_t device_id = 0;
    bool allow_host_online = true;
    // Larger data messages are answered with S9F11 (frames above
    // comm.max_frame_bytes close the connection instead, FR-HSMS-2).
    std::size_t max_body_bytes = 65536;
    int num_lines = 6;  // reported in the scan-started report
};

GemConfig gem_config_from(const ssim::core::Config& config);

class GemService final : public hsms::IHsmsHandler {
public:
    // logger may be null. All references must outlive the service.
    GemService(GemConfig config, ssim::core::MachineApi& api, ssim::core::EventBus& bus,
               ssim::core::AlarmManager& alarms, const ssim::core::IClock& clock,
               hsms::IMessageSender& sender, ssim::core::Logger* logger = nullptr);
    ~GemService() override;

    GemService(const GemService&) = delete;
    GemService& operator=(const GemService&) = delete;

    // hsms::IHsmsHandler
    void on_data(const hsms::Delivery& delivery) override;
    void on_session_state(hsms::ConnectionState state) override;
    void on_transaction_timeout(std::uint32_t system_bytes) override;
    void on_error(const std::string& what) override;

    // For tests and status displays; I/O thread state.
    CommState comm_state() const { return comm_; }

private:
    void handle_message(const hsms::Frame& frame, const secs2::Message& message);
    void handle_status_request(const hsms::Frame& frame, const secs2::Message& message);
    void handle_offline_request(const hsms::Frame& frame);
    void handle_online_request(const hsms::Frame& frame);
    void handle_remote_command(const hsms::Frame& frame, const secs2::Message& message);

    ssim::core::Result<bool> execute_command(const RemoteCommand& command,
                                             std::vector<ParamAck>& acks, std::uint8_t& hcack);

    secs2::Item status_value(std::uint32_t svid) const;

    void reply(const hsms::Frame& request, const secs2::Message& message);
    void send_request(secs2::Message message);
    void send_s9(std::uint8_t function, const hsms::Header& offending);
    void send_event(std::uint32_t ceid, std::vector<Report> reports);
    void send_alarm(std::uint8_t alcd, std::uint32_t alid, const std::string& text);
    void log(ssim::core::LogLevel level, const char* event,
             nlohmann::json fields = nlohmann::json::object());

    // Tasks posted from the bus subscribers (run on the I/O thread).
    void on_process_state_changed(ssim::core::ProcessState to);
    void on_control_state_changed(ssim::core::ControlMode to);
    void on_scan_started(const std::string& wafer_id, int slot);
    void on_result(const ssim::core::WaferResultReady& result);
    void on_run_aborted();
    void on_alarm(bool set, int alid, const std::string& text);

    GemConfig config_;
    ssim::core::MachineApi& api_;
    ssim::core::EventBus& bus_;
    ssim::core::AlarmManager& alarms_;
    const ssim::core::IClock& clock_;
    hsms::IMessageSender& sender_;
    ssim::core::Logger* logger_;

    secs2::MessageFactory factory_;
    std::vector<ssim::core::EventBus::SubscriptionId> subscriptions_;
    std::chrono::steady_clock::time_point started_;

    // I/O-thread state.
    CommState comm_ = CommState::kNotCommunicating;
    std::uint32_t next_dataid_ = 1;
    std::uint32_t wafers_processed_ = 0;
    ssim::core::ControlMode preferred_online_mode_ = ssim::core::ControlMode::kOnlineLocal;
    std::map<std::uint32_t, hsms::Header> open_requests_;  // system bytes -> what we sent
};

}  // namespace ssim::secsgem::gem
