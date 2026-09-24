#include "ssim/core/machine_api.hpp"

#include <utility>

namespace ssim::core {

MachineApi::MachineApi(Controller& controller, EventBus& bus) : controller_(controller), bus_(bus) {
    progress_sub_ = bus_.subscribe<ScanProgress>(
        [this](const ScanProgress& e) { progress_percent_.store(e.percent); });
    started_sub_ =
        bus_.subscribe<ScanStarted>([this](const ScanStarted&) { progress_percent_.store(0.0); });
    result_sub_ = bus_.subscribe<WaferResultReady>([this](const WaferResultReady& e) {
        std::lock_guard lock(result_mutex_);
        last_result_ = e;
    });
}

MachineApi::~MachineApi() {
    bus_.unsubscribe(progress_sub_);
    bus_.unsubscribe(started_sub_);
    bus_.unsubscribe(result_sub_);
}

Result<ProcessState> MachineApi::submit(CommandPayload payload, CommandSource source) {
    return controller_.submit_command(
        Command{std::move(payload), source, next_correlation_id_.fetch_add(1)});
}

Result<ProcessState> MachineApi::start(std::string wafer_id, int slot, CommandSource source) {
    return submit(StartCommand{std::move(wafer_id), slot}, source);
}

Result<ProcessState> MachineApi::stop(CommandSource source) {
    return submit(StopCommand{}, source);
}

Result<ProcessState> MachineApi::abort(CommandSource source) {
    return submit(AbortCommand{}, source);
}

Result<ProcessState> MachineApi::clear_alarm(CommandSource source) {
    return submit(ClearAlarmCommand{}, source);
}

Result<ProcessState> MachineApi::set_control_mode(ControlMode mode, CommandSource source) {
    return submit(SetControlModeCommand{mode}, source);
}

MachineSnapshot MachineApi::snapshot() const {
    MachineSnapshot snap;
    snap.process_state = controller_.state();
    snap.control_mode = controller_.control_mode();
    snap.alarm_active = controller_.has_active_alarm();
    snap.progress_percent = progress_percent_.load();
    snap.wafer_id = controller_.current_wafer_id();
    {
        std::lock_guard lock(result_mutex_);
        snap.last_result = last_result_;
    }
    return snap;
}

}  // namespace ssim::core
