#include "ssim/core/controller.hpp"

#include <future>
#include <type_traits>
#include <variant>

#include "ssim/core/events.hpp"
#include "ssim/core/logger.hpp"

namespace ssim::core {

namespace {
constexpr std::size_t kInboxCapacity = 256;

// FR-MC-4: Stop/Abort/ClearAlarm are safety commands and are always
// accepted regardless of control mode or source (PRD §6.5: "Stop and Abort
// still accepted for safety" even in Online-Remote, where operator Start is
// disabled). Start and SetControlMode are the two commands the control
// state model actually gates.
bool control_allows_start(ControlMode mode, CommandSource source) {
    if (mode == ControlMode::kOnlineRemote) {
        return source == CommandSource::kSecsGem;
    }
    return source != CommandSource::kSecsGem;
}

bool control_allows_set_mode(CommandSource source) {
    // Day 5 scope: the host requests Offline/Online via S1F15/S1F17, a
    // distinct request/ack flow from the operator's direct SetControlMode.
    // Today only the operator (UI/CLI) may set the mode directly.
    return source != CommandSource::kSecsGem;
}
}  // namespace

Controller::Controller(EventBus& bus, IScanDriver& scan_driver, AlarmManager& alarms)
    : bus_(bus),
      scan_driver_(scan_driver),
      alarms_(alarms),
      inbox_(kInboxCapacity, BackpressurePolicy::kBlock) {}

Controller::~Controller() { stop(); }

void Controller::start() {
    thread_ = std::thread([this] { run(); });
}

void Controller::stop() {
    if (!thread_.joinable()) {
        return;
    }
    // FR-MC-6: abort whatever is in flight before tearing down the inbox,
    // so a scan thread mid-run doesn't outlive the controller that owns its
    // completion signal.
    scan_driver_.request_abort();
    scan_driver_.join();
    inbox_.close();
    thread_.join();
}

void Controller::run() {
    set_current_thread_name("controller");
    while (auto item = inbox_.pop()) {
        (*item)();
    }
}

Result<ProcessState> Controller::run_on_controller_thread(std::function<Result<ProcessState>()> fn) {
    auto promise = std::make_shared<std::promise<Result<ProcessState>>>();
    std::future<Result<ProcessState>> future = promise->get_future();
    inbox_.push([fn = std::move(fn), promise] { promise->set_value(fn()); });
    return future.get();
}

Result<ProcessState> Controller::submit_command(Command command) {
    return run_on_controller_thread(
        [this, command = std::move(command)] { return handle_command(command); });
}

Result<ProcessState> Controller::handle_command(const Command& command) {
    return std::visit(
        [this, &command](const auto& payload) -> Result<ProcessState> {
            using T = std::decay_t<decltype(payload)>;
            if constexpr (std::is_same_v<T, StartCommand>) {
                TransitionGuards guards;
                guards.control_allows_start =
                    control_allows_start(control_mode_.load(), command.source);
                guards.has_active_alarm = has_active_alarm_.load();
                guards.wafer_valid = !payload.wafer_id.empty();
                auto result = transition(ProcessTrigger::kStart, guards);
                if (result) {
                    {
                        std::lock_guard lock(wafer_id_mutex_);
                        current_wafer_id_ = payload.wafer_id;
                    }
                    bus_.publish(ScanStarted{payload.wafer_id, payload.slot});
                    scan_driver_.start(payload.wafer_id);
                }
                return result;
            } else if constexpr (std::is_same_v<T, StopCommand>) {
                return transition(ProcessTrigger::kStop, {});
            } else if constexpr (std::is_same_v<T, AbortCommand>) {
                auto result = transition(ProcessTrigger::kAbort, {});
                if (result) {
                    scan_driver_.request_abort();
                }
                return result;
            } else if constexpr (std::is_same_v<T, ClearAlarmCommand>) {
                // Day 2 simplification: an operator ClearAlarm always
                // succeeds (no per-cause "is the sensor still stuck"
                // tracking yet) and clears every active alarm, since the
                // FSM has one aggregate Alarm state rather than one per
                // ALID.
                TransitionGuards guards;
                guards.alarm_cause_cleared = true;
                auto result = transition(ProcessTrigger::kClearAlarm, guards);
                if (result) {
                    has_active_alarm_.store(false);
                    alarms_.clear_all();
                }
                return result;
            } else if constexpr (std::is_same_v<T, SetControlModeCommand>) {
                if (!control_allows_set_mode(command.source)) {
                    return Result<ProcessState>::err(
                        Error{kReasonControlStateDenied, "SetControlMode denied for this source"});
                }
                const ControlMode from = control_mode_.load();
                control_mode_.store(payload.mode);
                bus_.publish(ControlStateChanged{from, payload.mode});
                return Result<ProcessState>::ok(state_.load());
            } else {
                static_assert(!sizeof(T), "unhandled Command payload alternative");
            }
        },
        command.payload);
}

Result<ProcessState> Controller::transition(ProcessTrigger trigger, const TransitionGuards& guards) {
    const ProcessState from = fsm_.state();
    Result<ProcessState> result = fsm_.apply(trigger, guards);
    if (result && fsm_.state() != from) {
        state_.store(fsm_.state());
        bus_.publish(StateChanged{from, fsm_.state()});
    }
    return result;
}

void Controller::notify_scan_complete(std::string wafer_id) {
    (void)run_on_controller_thread([this, wafer_id = std::move(wafer_id)]() -> Result<ProcessState> {
        bus_.publish(ScanLinesComplete{wafer_id});
        return transition(ProcessTrigger::kLinesComplete, {});
    });
}

void Controller::notify_processing_result(bool out_of_spec, double stress_mpa,
                                          std::optional<AlarmId> raised_alarm_id,
                                          std::string alarm_reason, bool more_wafers_pending) {
    (void)run_on_controller_thread([this, out_of_spec, stress_mpa, raised_alarm_id,
                              alarm_reason = std::move(alarm_reason),
                              more_wafers_pending]() -> Result<ProcessState> {
        if (raised_alarm_id.has_value()) {
            auto result = transition(ProcessTrigger::kFault, {});
            if (result) {
                has_active_alarm_.store(true);
                alarms_.set(*raised_alarm_id, alarm_reason);
            }
            return result;
        }
        std::string wafer_id;
        {
            std::lock_guard lock(wafer_id_mutex_);
            wafer_id = current_wafer_id_;
        }
        if (out_of_spec) {
            bus_.publish(WaferOutOfSpec{wafer_id, stress_mpa});
        }
        TransitionGuards guards;
        guards.more_wafers_pending = more_wafers_pending;
        auto result = transition(ProcessTrigger::kResultReady, guards);
        if (result) {
            bus_.publish(ScanComplete{wafer_id, 0, out_of_spec});
        }
        return result;
    });
}

void Controller::notify_fault(AlarmId id, std::string reason) {
    (void)run_on_controller_thread([this, id, reason = std::move(reason)]() -> Result<ProcessState> {
        auto result = transition(ProcessTrigger::kFault, {});
        if (result) {
            has_active_alarm_.store(true);
            alarms_.set(id, reason);
        }
        return result;
    });
}

void Controller::notify_stage_stopped() {
    (void)run_on_controller_thread(
        [this]() -> Result<ProcessState> { return transition(ProcessTrigger::kStageStopped, {}); });
}

std::string Controller::current_wafer_id() const {
    std::lock_guard lock(wafer_id_mutex_);
    return current_wafer_id_;
}

}  // namespace ssim::core
