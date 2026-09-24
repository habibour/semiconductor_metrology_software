#include "ssim/secsgem/gem/gem_service.hpp"

#include <chrono>
#include <utility>

#include "ssim/core/events.hpp"
#include "ssim/secsgem/gem/identifiers.hpp"
#include "ssim/secsgem/hsms/data_message.hpp"

namespace ssim::secsgem::gem {

using secs2::Item;
using secs2::Message;
using ssim::core::CommandSource;
using ssim::core::ControlMode;
using ssim::core::LogLevel;
using ssim::core::ProcessState;

namespace {

constexpr CommandSource kHost = CommandSource::kSecsGem;

// Numeric codes for the state status variables (PRD 8.6.6).
std::uint8_t control_code(ControlMode mode) {
    switch (mode) {
        case ControlMode::kOffline:
            return 0;
        case ControlMode::kOnlineLocal:
            return 1;
        case ControlMode::kOnlineRemote:
            return 2;
    }
    return 0;
}

std::uint8_t process_code(ProcessState state) {
    switch (state) {
        case ProcessState::kIdle:
            return 0;
        case ProcessState::kScanning:
            return 1;
        case ProcessState::kProcessing:
            return 2;
        case ProcessState::kAlarm:
            return 3;
        case ProcessState::kStopping:
            return 4;
    }
    return 0;
}

// A wafer id becomes a directory name under results/, so it is validated as
// external input (NFR-SEC-1): 1 to 32 characters of letters, digits, '_' and '-'.
bool valid_wafer_id(const std::string& id) {
    if (id.empty() || id.size() > 32) {
        return false;
    }
    for (char c : id) {
        const bool ok = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                        (c >= '0' && c <= '9') || c == '_' || c == '-';
        if (!ok) {
            return false;
        }
    }
    return true;
}

bool known_alid(std::uint32_t alid) { return alid >= 1001 && alid <= 1008; }

std::optional<std::uint32_t> as_u32(const Item& item) {
    switch (item.format()) {
        case secs2::Format::kU1:
            if (item.count() == 1) return item.as_array<std::uint8_t>()[0];
            break;
        case secs2::Format::kU2:
            if (item.count() == 1) return item.as_array<std::uint16_t>()[0];
            break;
        case secs2::Format::kU4:
            if (item.count() == 1) return item.as_array<std::uint32_t>()[0];
            break;
        default:
            break;
    }
    return std::nullopt;
}

}  // namespace

GemConfig gem_config_from(const ssim::core::Config& config) {
    GemConfig g;
    g.identity = {config.machine.model, config.machine.softrev};
    g.device_id = static_cast<std::uint16_t>(config.comm.device_id);
    g.allow_host_online = config.comm.allow_host_online;
    g.num_lines = config.scan.lines;
    return g;
}

GemService::GemService(GemConfig config, ssim::core::MachineApi& api, ssim::core::EventBus& bus,
                       ssim::core::AlarmManager& alarms, const ssim::core::IClock& clock,
                       hsms::IMessageSender& sender, ssim::core::Logger* logger)
    : config_(std::move(config)),
      api_(api),
      bus_(bus),
      alarms_(alarms),
      clock_(clock),
      sender_(sender),
      logger_(logger),
      started_(clock.monotonic_now()) {
    register_equipment_catalogue(factory_);

    namespace c = ssim::core;
    subscriptions_.push_back(bus_.subscribe<c::StateChanged>([this](const c::StateChanged& e) {
        const ProcessState to = e.to;
        sender_.post_task([this, to] { on_process_state_changed(to); });
    }));
    subscriptions_.push_back(
        bus_.subscribe<c::ControlStateChanged>([this](const c::ControlStateChanged& e) {
            const ControlMode to = e.to;
            sender_.post_task([this, to] { on_control_state_changed(to); });
        }));
    subscriptions_.push_back(bus_.subscribe<c::ScanStarted>([this](const c::ScanStarted& e) {
        sender_.post_task([this, e] { on_scan_started(e.wafer_id, e.slot); });
    }));
    subscriptions_.push_back(bus_.subscribe<c::WaferResultReady>(
        [this](const c::WaferResultReady& e) { sender_.post_task([this, e] { on_result(e); }); }));
    subscriptions_.push_back(bus_.subscribe<c::RunAborted>(
        [this](const c::RunAborted&) { sender_.post_task([this] { on_run_aborted(); }); }));
    subscriptions_.push_back(bus_.subscribe<c::AlarmSet>([this](const c::AlarmSet& e) {
        sender_.post_task([this, e] { on_alarm(true, e.alid, e.reason); });
    }));
    subscriptions_.push_back(bus_.subscribe<c::AlarmCleared>([this](const c::AlarmCleared& e) {
        sender_.post_task([this, e] { on_alarm(false, e.alid, e.name); });
    }));
}

GemService::~GemService() {
    for (auto id : subscriptions_) {
        bus_.unsubscribe(id);
    }
}

void GemService::log(LogLevel level, const char* event, nlohmann::json fields) {
    if (logger_ != nullptr) {
        logger_->log(level, "gem", event, std::move(fields));
    }
}

// ---- sending ---------------------------------------------------------------

void GemService::reply(const hsms::Frame& request, const Message& message) {
    if (request.header.w_bit()) {  // a message that did not ask for a reply gets none
        sender_.post_reply(request.header.system_bytes, message);
    }
}

void GemService::send_request(Message message) {
    const bool tracked = message.w_bit;
    const std::uint8_t byte2 =
        static_cast<std::uint8_t>((message.stream & 0x7F) | (message.w_bit ? 0x80 : 0));
    const std::uint8_t function = message.function;
    sender_.post_request(std::move(message), [this, tracked, byte2, function](std::uint32_t sys) {
        if (!tracked) {
            return;
        }
        hsms::Header h;
        h.session_id = config_.device_id;
        h.byte2 = byte2;
        h.byte3 = function;
        h.system_bytes = sys;
        open_requests_[sys] = h;
    });
}

void GemService::send_s9(std::uint8_t function, const hsms::Header& offending) {
    log(LogLevel::kWarn, "s9_sent", {{"function", function}});
    send_request(make_s9(function, hsms::header_bytes(offending)));
}

void GemService::send_event(std::uint32_t ceid, std::vector<Report> reports) {
    if (comm_ != CommState::kCommunicating) {
        log(LogLevel::kInfo, "event_dropped_not_communicating", {{"ceid", ceid}});
        return;
    }
    EventReport event;
    event.dataid = next_dataid_++;
    event.ceid = ceid;
    event.reports = std::move(reports);
    send_request(make_s6f11(event));
}

void GemService::send_alarm(std::uint8_t alcd, std::uint32_t alid, const std::string& text) {
    if (comm_ != CommState::kCommunicating) {
        log(LogLevel::kInfo, "alarm_dropped_not_communicating", {{"alid", alid}});
        return;
    }
    send_request(make_s5f1({alcd, alid, text}));
}

// ---- events and alarms (tasks on the I/O thread) --------------------------

void GemService::on_process_state_changed(ProcessState to) {
    const auto control = api_.snapshot().control_mode;
    send_event(kCeidProcessStateChanged,
               {{kRptidStates, {Item::u1(control_code(control)), Item::u1(process_code(to))}}});
}

void GemService::on_control_state_changed(ControlMode to) {
    if (to != ControlMode::kOffline) {
        preferred_online_mode_ = to;
    }
    const auto process = api_.snapshot().process_state;
    send_event(kCeidControlStateChanged,
               {{kRptidStates, {Item::u1(control_code(to)), Item::u1(process_code(process))}}});
}

void GemService::on_scan_started(const std::string& wafer_id, int slot) {
    send_event(kCeidWaferScanStarted,
               {{kRptidScanStarted,
                 {Item::ascii(wafer_id), Item::u1(static_cast<std::uint8_t>(slot)),
                  Item::u1(static_cast<std::uint8_t>(config_.num_lines))}}});
}

void GemService::on_result(const ssim::core::WaferResultReady& r) {
    ++wafers_processed_;
    // Both events carry the full result report, so they are sent from the
    // result event (which has the numbers) rather than from ScanComplete.
    auto report = [&r] {
        return std::vector<Report>{
            {kRptidWaferResult,
             {Item::ascii(r.wafer_id), Item::u1(static_cast<std::uint8_t>(r.slot)),
              Item::f4(static_cast<float>(r.stress_mpa)),
              Item::f4(static_cast<float>(r.stress_unc_mpa)),
              Item::f4(static_cast<float>(r.curvature_per_m)),
              Item::f4(static_cast<float>(r.fit_rms_um)), Item::boolean(r.out_of_spec)}}};
    };
    send_event(kCeidWaferScanComplete, report());
    if (r.out_of_spec) {
        send_event(kCeidWaferOutOfSpec, report());
    }
}

void GemService::on_run_aborted() {
    const auto snap = api_.snapshot();
    send_event(kCeidRunAborted, {{kRptidStates,
                                  {Item::u1(control_code(snap.control_mode)),
                                   Item::u1(process_code(snap.process_state))}}});
}

void GemService::on_alarm(bool set, int alid, const std::string& text) {
    const std::uint8_t alcd = static_cast<std::uint8_t>((set ? kAlcdSetBit : 0) | kAlarmCategory);
    send_alarm(alcd, static_cast<std::uint32_t>(alid), text);
}

// ---- IHsmsHandler ------------------------------------------------------------

void GemService::on_session_state(hsms::ConnectionState state) {
    if (state != hsms::ConnectionState::kSelected) {
        // PRD 6.5: losing the link, a Separate or a T7 expiry ends communication.
        if (comm_ == CommState::kCommunicating) {
            log(LogLevel::kInfo, "communication_lost");
        }
        comm_ = CommState::kNotCommunicating;
        open_requests_.clear();
    }
}

void GemService::on_transaction_timeout(std::uint32_t system_bytes) {
    const auto it = open_requests_.find(system_bytes);
    if (it == open_requests_.end()) {
        return;
    }
    // Our own reply timer (T3) expired: S9F9 carries the header we sent.
    const hsms::Header sent = it->second;
    open_requests_.erase(it);
    log(LogLevel::kWarn, "t3_timeout", {{"system_bytes", system_bytes}});
    send_s9(9, sent);
}

void GemService::on_error(const std::string& what) {
    log(LogLevel::kWarn, "hsms_error", {{"what", what}});
}

void GemService::on_data(const hsms::Delivery& delivery) {
    const hsms::Frame& frame = delivery.frame;

    if (delivery.reply_to.has_value()) {
        open_requests_.erase(*delivery.reply_to);  // S6F12 or S5F2: nothing more to do
        return;
    }
    if (frame.header.session_id != config_.device_id) {
        send_s9(1, frame.header);  // S9F1: unrecognised device id
        return;
    }
    if (frame.body.size() > config_.max_body_bytes) {
        send_s9(11, frame.header);  // S9F11: data too long
        return;
    }
    auto message = hsms::to_message(frame);
    if (!message) {
        send_s9(7, frame.header);  // S9F7: the body does not decode
        return;
    }
    switch (factory_.check(message.value())) {
        case secs2::Validation::kOk:
            break;
        case secs2::Validation::kUnknownStream:
            send_s9(3, frame.header);
            return;
        case secs2::Validation::kUnknownFunction:
            send_s9(5, frame.header);
            return;
        case secs2::Validation::kIllegalData:
            send_s9(7, frame.header);
            return;
    }
    handle_message(frame, message.value());
}

// ---- host messages -----------------------------------------------------------

void GemService::handle_message(const hsms::Frame& frame, const Message& message) {
    const auto key = std::make_pair(message.stream, message.function);
    if (key == std::make_pair<std::uint8_t, std::uint8_t>(1, 1)) {
        reply(frame, make_s1f2(config_.identity));
    } else if (key == std::make_pair<std::uint8_t, std::uint8_t>(1, 13)) {
        comm_ = CommState::kCommunicating;
        log(LogLevel::kInfo, "communicating");
        reply(frame, make_s1f14(kCommackAccepted, config_.identity));
    } else if (key == std::make_pair<std::uint8_t, std::uint8_t>(1, 3)) {
        handle_status_request(frame, message);
    } else if (key == std::make_pair<std::uint8_t, std::uint8_t>(1, 15)) {
        handle_offline_request(frame);
    } else if (key == std::make_pair<std::uint8_t, std::uint8_t>(1, 17)) {
        handle_online_request(frame);
    } else if (key == std::make_pair<std::uint8_t, std::uint8_t>(2, 41)) {
        handle_remote_command(frame, message);
    }
    // S5F2 and S6F12 that match no open request are stray acknowledgements: ignored.
}

Item GemService::status_value(std::uint32_t svid) const {
    const ssim::core::MachineSnapshot snap = api_.snapshot();
    const auto stress = [&](double ssim::core::WaferResultReady::* field) {
        return snap.last_result.has_value() ? static_cast<float>((*snap.last_result).*field) : 0.0f;
    };
    switch (svid) {
        case kSvidControlState:
            return Item::u1(control_code(snap.control_mode));
        case kSvidProcessState:
            return Item::u1(process_code(snap.process_state));
        case kSvidCurrentWaferId:
            return Item::ascii(snap.wafer_id);
        case kSvidCurrentSlot:
            return Item::u1(std::uint8_t{1});
        case kSvidScanProgressPercent:
            return Item::f4(static_cast<float>(snap.progress_percent));
        case kSvidLastStressMpa:
            return Item::f4(stress(&ssim::core::WaferResultReady::stress_mpa));
        case kSvidLastCurvature:
            return Item::f4(stress(&ssim::core::WaferResultReady::curvature_per_m));
        case kSvidLastFitRmsUm:
            return Item::f4(stress(&ssim::core::WaferResultReady::fit_rms_um));
        case kSvidActiveAlarmCount:
            return Item::u2(static_cast<std::uint16_t>(alarms_.active_count()));
        case kSvidSoftwareRevision:
            return Item::ascii(config_.identity.softrev);
        case kSvidUptimeSeconds: {
            const auto up =
                std::chrono::duration_cast<std::chrono::seconds>(clock_.monotonic_now() - started_);
            return Item::u4(static_cast<std::uint32_t>(up.count() < 0 ? 0 : up.count()));
        }
        case kSvidWafersProcessed:
            return Item::u4(wafers_processed_);
        default:
            return Item::list({});  // unknown SVID: an empty item (FR-GEM-6)
    }
}

void GemService::handle_status_request(const hsms::Frame& frame, const Message& message) {
    std::vector<std::uint32_t> svids = parse_s1f3(message).value();
    if (svids.empty()) {
        for (const SvInfo& sv : status_variables()) {
            svids.push_back(sv.svid);
        }
    }
    std::vector<Item> values;
    values.reserve(svids.size());
    for (std::uint32_t svid : svids) {
        values.push_back(status_value(svid));
    }
    reply(frame, make_s1f4(std::move(values)));
}

void GemService::handle_offline_request(const hsms::Frame& frame) {
    (void)api_.set_control_mode(ControlMode::kOffline, kHost);
    reply(frame, make_s1f16(kOflackOk));
}

void GemService::handle_online_request(const hsms::Frame& frame) {
    if (!config_.allow_host_online) {
        reply(frame, make_s1f18(kOnlackNotAllowed));
        return;
    }
    if (api_.snapshot().control_mode != ControlMode::kOffline) {
        reply(frame, make_s1f18(kOnlackAlreadyOnline));
        return;
    }
    // Back to whichever online mode the operator last used: the operator, not
    // the host, decides between Local and Remote.
    const auto result = api_.set_control_mode(preferred_online_mode_, kHost);
    reply(frame, make_s1f18(result ? kOnlackAccepted : kOnlackNotAllowed));
}

// ---- remote commands ---------------------------------------------------------

void GemService::handle_remote_command(const hsms::Frame& frame, const Message& message) {
    const RemoteCommand command = parse_s2f41(message).value();
    std::vector<ParamAck> acks;
    std::uint8_t hcack = kHcackUnknownCommand;
    (void)execute_command(command, acks, hcack);
    log(LogLevel::kInfo, "remote_command", {{"rcmd", command.rcmd}, {"hcack", hcack}});
    reply(frame, make_s2f42(hcack, acks));
}

ssim::core::Result<bool> GemService::execute_command(const RemoteCommand& command,
                                                     std::vector<ParamAck>& acks,
                                                     std::uint8_t& hcack) {
    using R = ssim::core::Result<bool>;
    const std::string& name = command.rcmd;
    if (name != "START" && name != "STOP" && name != "ABORT" && name != "CLEAR_ALARM") {
        hcack = kHcackUnknownCommand;
        return R::err({1, "unknown remote command"});
    }
    // Host commands are only accepted in Online-Remote (PRD 6.5). The controller
    // enforces this as well; checking here gives the host the right HCACK.
    if (api_.snapshot().control_mode != ControlMode::kOnlineRemote) {
        hcack = kHcackCannotPerformNow;
        return R::err({2, "not in Online-Remote"});
    }

    bool params_ok = true;
    std::optional<std::string> wafer_id;
    std::optional<std::uint32_t> alid;
    for (const CommandParam& p : command.params) {
        if (name == "START" && p.name == "WAFER_ID") {
            if (wafer_id.has_value()) {
                acks.push_back({p.name, kCpackIllegalValue});  // given twice
                params_ok = false;
            } else if (p.value.format() != secs2::Format::kAscii) {
                acks.push_back({p.name, kCpackIllegalFormat});
                params_ok = false;
            } else if (!valid_wafer_id(p.value.as_ascii())) {
                acks.push_back({p.name, kCpackIllegalValue});
                params_ok = false;
            } else {
                wafer_id = p.value.as_ascii();
                acks.push_back({p.name, kCpackOk});
            }
        } else if (name == "START" && p.name == "CASSETTE_ID") {
            acks.push_back({p.name, kCpackIllegalValue});  // cassette runs are not built yet
            params_ok = false;
        } else if (name == "CLEAR_ALARM" && p.name == "ALID") {
            const auto value = as_u32(p.value);
            if (!value.has_value()) {
                acks.push_back({p.name, kCpackIllegalFormat});
                params_ok = false;
            } else if (!known_alid(*value)) {
                acks.push_back({p.name, kCpackIllegalValue});
                params_ok = false;
            } else {
                alid = value;
                acks.push_back({p.name, kCpackOk});
            }
        } else {
            acks.push_back({p.name, kCpackUnknownName});
            params_ok = false;
        }
    }
    if (name == "START" && !wafer_id.has_value()) {
        params_ok = false;  // nothing to scan
    }
    if (!params_ok) {
        hcack = kHcackInvalidParameter;
        return R::err({3, "invalid parameter"});
    }

    ssim::core::Result<ssim::core::ProcessState> result =
        name == "START"   ? api_.start(*wafer_id, 1, kHost)
        : name == "STOP"  ? api_.stop(kHost)
        : name == "ABORT" ? api_.abort(kHost)
                          : api_.clear_alarm(kHost);
    if (!result) {
        hcack = kHcackCannotPerformNow;  // e.g. an alarm is active, or the wrong state
        return R::err({2, result.error().message});
    }
    // START finishes later and is reported by events (HCACK 4); the rest are done.
    hcack = name == "START" ? kHcackAcceptedLater : kHcackDone;
    return R::ok(true);
}

}  // namespace ssim::secsgem::gem
