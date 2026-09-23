#pragma once

// Thread-safety: every event here is a plain, copyable value type — safe to
// pass by value into EventBus::publish() and to copy into subscriber
// handlers on any thread.
//
// PRD §6.5 "Actions" column and §8.6.5 (CEIDs 2001-2008): one struct per
// distinct event the controller emits. Subscribers (logger, later the UI
// and SECS/GEM module) depend on these types without the controller
// depending on any subscriber (Observer pattern, PRD §6.6).

#include <cstdint>
#include <string>

#include "ssim/core/command.hpp"
#include "ssim/core/process_state_machine.hpp"

namespace ssim::core {

struct ScanStarted {
    std::string wafer_id;
    int slot = 0;
};

// FR-SCN-4: published at most as often as the scan thread advances, never
// faster than the subscriber can be expected to keep up with — callers that
// need a rate cap (e.g. the Day 3 Qt panel's 30 Hz limit) apply it
// themselves on the subscribing side.
struct ScanProgress {
    std::string wafer_id;
    double percent = 0.0;  // 0..100
};

struct ScanComplete {
    std::string wafer_id;
    int slot = 0;
    bool out_of_spec = false;
};

struct WaferOutOfSpec {
    std::string wafer_id;
    double stress_mpa = 0.0;
};

struct RunAborted {
    std::string wafer_id;
};

struct CassetteComplete {
    std::string cassette_id;
};

struct AlarmSet {
    int alid = 0;
    std::string name;
    std::string reason;
};

struct AlarmCleared {
    int alid = 0;
    std::string name;
};

struct StateChanged {
    ProcessState from;
    ProcessState to;
};

struct ControlStateChanged {
    ControlMode from;
    ControlMode to;
};

// Published by the scan driver when it has pushed the last sample block for
// every line of the current wafer (PRD §6.5 Scanning -> Processing trigger:
// "All lines complete"). The controller subscribes to this to drive that
// transition; the composition root (equipment_cli) subscribes to it too, to
// know when to start running the ssim_analysis pipeline it owns (ssim_core
// must not depend on ssim_analysis — PRD §6.2).
struct ScanLinesComplete {
    std::string wafer_id;
};

}  // namespace ssim::core
