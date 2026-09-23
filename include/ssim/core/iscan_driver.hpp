#pragma once

// Thread-safety: implementations document their own thread-safety; every
// method here must be safe to call from the controller thread while the
// scan thread the driver owns is running (concurrency rule C1: the
// controller is the only *writer* of machine state, but it must still be
// able to command the scan thread it does not own the internals of).
//
// Dependency injection (PRD §6.6): the concrete scan thread (src/hw's
// ScanThread) drives ssim_hw's IStage/ILaserSensor, so it cannot live in
// ssim_core without violating the PRD §6.2 dependency rule (ssim_core may
// depend only on STL and the JSON library). This interface is the seam:
// ssim_core's Controller depends only on IScanDriver; the composition root
// (equipment_cli's main, later equipment_qt's) injects the real ScanThread.

#include <string>

namespace ssim::core {

class IScanDriver {
public:
    virtual ~IScanDriver() = default;

    // Starts scanning wafer_id on the driver's own thread. Must not block;
    // the driver reports progress and completion asynchronously (via the
    // event bus / completion queue it was constructed with). Undefined to
    // call again before join() has been called for the previous run.
    virtual void start(std::string wafer_id) = 0;

    // Requests an abort (FR-SCN-3: takes effect within 200 ms at
    // realtime_factor 1.0). Safe to call from any thread. No-op if not
    // currently running.
    virtual void request_abort() = 0;

    // Blocks until the scan thread (if running) has finished and been
    // joined. Safe to call even if start() was never called, and safe to
    // call more than once.
    virtual void join() = 0;
};

}  // namespace ssim::core
