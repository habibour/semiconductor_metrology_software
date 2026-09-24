#pragma once

// Thread-safety: api(), bus(), config(), next_wafer_id() and last_wafer_dir()
// are safe to call from any thread. Construction and destruction must happen
// on one thread (the owner), and destruction must not race with a caller of
// api() on another thread.
//
// Threads owned here: logger, controller, scan, processing (named), plus the
// fit pool. This is the reusable composition root (PRD §6.6 dependency
// injection): it wires ssim_core, ssim_hw and ssim_analysis for a machine
// that scans wafers one after another, so the Qt panel (and later the GEM
// wiring) do not each re-implement what equipment_cli's main() does once.
//
// Flow of one wafer: the scan thread reports "all lines complete", the
// controller moves to Processing, and the processing thread drains the
// sample queue, runs the analysis pipeline, raises quality alarms through the
// controller, publishes WaferResultReady and WaferMapReady, then writes the
// output files. Nothing here runs on a GUI thread.
//
// Config is immutable for the lifetime of the runtime: changing a setting
// means building a new runtime (only sensible while Idle).

#include <atomic>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#include "ssim/analysis/fit_pool.hpp"
#include "ssim/core/alarms.hpp"
#include "ssim/core/clock.hpp"
#include "ssim/core/config.hpp"
#include "ssim/core/controller.hpp"
#include "ssim/core/event_bus.hpp"
#include "ssim/core/logger.hpp"
#include "ssim/core/machine_api.hpp"
#include "ssim/core/queue.hpp"
#include "ssim/core/result.hpp"
#include "ssim/core/scan_types.hpp"
#include "ssim/hw/hardware_factory.hpp"
#include "ssim/hw/scan_thread.hpp"
#include "ssim/hw/wafer_model.hpp"

namespace ssim::machine {

// FR-OUT-5: creates a fresh <output_root>/<run_id>/ (never reuses an
// existing directory; a numeric suffix is added on a same-second clash).
[[nodiscard]] ssim::core::Result<std::filesystem::path> create_run_dir(
    const std::filesystem::path& output_root);

struct RuntimeOptions {
    // Start the optional SECS/GEM link (HSMS listener plus GEM service) when
    // config.comm.enabled is also true. Off by default so tests and tools that
    // build a runtime do not open a port. Ignored, with an error from
    // start_comm(), in a build without SSIM_ENABLE_SECSGEM.
    bool start_comm = false;
};

class MachineRuntime {
public:
    // run_dir must already exist (see create_run_dir); wafer output goes in
    // run_dir/<wafer_id>/ and the machine log in run_dir/machine.log.
    MachineRuntime(ssim::core::Config config, std::filesystem::path run_dir);
    ~MachineRuntime();

    MachineRuntime(const MachineRuntime&) = delete;
    MachineRuntime& operator=(const MachineRuntime&) = delete;

    [[nodiscard]] static ssim::core::Result<std::unique_ptr<MachineRuntime>> create(
        ssim::core::Config config, const std::filesystem::path& output_root,
        RuntimeOptions options = {});

    // Starts the SECS/GEM link (FR-MC-3: the machine works fully without it).
    // Does nothing and succeeds when config.comm.enabled is false. Fails with
    // an error value if the port cannot be bound, or if this build has no
    // SECS/GEM module. Call once, after construction.
    [[nodiscard]] ssim::core::Result<bool> start_comm();

    // The HSMS port actually listening, or 0 if the link is not running.
    std::uint16_t hsms_port() const;

    ssim::core::MachineApi& api() { return *api_; }
    ssim::core::EventBus& bus() { return bus_; }
    const ssim::core::Config& config() const { return config_; }

    // "W001", "W002", ...; advances when a scan actually starts, so a
    // rejected Start does not burn an id.
    std::string next_wafer_id() const;

    // Output directory of the most recent wafer (empty before the first).
    std::filesystem::path last_wafer_dir() const;
    const std::filesystem::path& run_dir() const { return run_dir_; }

private:
    // Lets the controller start the same ScanThread again: the plain
    // ScanThread::start() is undefined until the previous run was joined,
    // and nothing else joins between runs.
    class RestartableScanDriver final : public ssim::core::IScanDriver {
    public:
        explicit RestartableScanDriver(ssim::hw::ScanThread& inner) : inner_(inner) {}
        void start(std::string wafer_id) override;
        void request_abort() override { inner_.request_abort(); }
        void join() override { inner_.join(); }

    private:
        ssim::hw::ScanThread& inner_;
    };

    void worker_loop();
    void process_wafer(const std::string& wafer_id);
    void drain_sample_queue();
    void shutdown();

    ssim::core::Config config_;
    std::filesystem::path run_dir_;
    std::string run_id_;

    ssim::core::SystemClock clock_;
    ssim::core::Logger logger_;
    ssim::hw::WaferModel wafer_model_;
    ssim::hw::SimulatedHardware hw_;
    ssim::core::EventBus bus_;
    ssim::core::AlarmManager alarms_;
    ssim::core::BoundedQueue<ssim::core::SampleBlock> sample_queue_;
    ssim::core::BoundedQueue<std::string> jobs_;  // wafer ids whose scan finished
    ssim::analysis::FitThreadPool pool_;
    ssim::hw::ScanThread scan_thread_;
    RestartableScanDriver driver_;
    ssim::core::Controller controller_;
    std::unique_ptr<ssim::core::MachineApi> api_;

    std::atomic<int> wafers_started_{0};
    mutable std::mutex dir_mutex_;
    std::filesystem::path last_wafer_dir_;

    // The SECS/GEM link. Defined in the .cpp so that this header never
    // includes secsgem or Asio, and stays usable in a build without them.
    struct Comm;
    std::unique_ptr<Comm> comm_;

    std::thread worker_;
    bool shut_down_ = false;
};

}  // namespace ssim::machine
