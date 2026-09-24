#include "ssim/hw/scan_thread.hpp"

#include <gtest/gtest.h>

#include <atomic>
#include <functional>
#include <mutex>
#include <string>

#include "ssim/core/alarms.hpp"
#include "ssim/core/event_bus.hpp"
#include "ssim/hw/laser_sensor_sim.hpp"
#include "ssim/hw/stage_sim.hpp"

namespace ssim::hw {
namespace {

// Calls on_trigger() the Nth time move_to() is invoked, letting a test
// deterministically request an abort partway through a scan without any
// real threading race or sleep (the callback runs on the scan thread
// itself, synchronously inside run()'s own call to move_to()).
class HookedStage : public IStage {
public:
    void set_line(double angle_rad) override {
        angle_ = angle_rad;
        position_ = 0.0;
    }
    void move_to(double position_m) override {
        position_ = position_m;
        ++call_count;
        if (call_count == trigger_at && on_trigger) {
            on_trigger();
        }
    }
    double current_position_m() const override { return position_; }
    double current_angle_rad() const override { return angle_; }

    int call_count = 0;
    int trigger_at = -1;
    std::function<void()> on_trigger;

private:
    double angle_ = 0.0;
    double position_ = 0.0;
};

ssim::core::WaferConfig small_wafer_config() {
    ssim::core::WaferConfig cfg;
    cfg.diameter_mm = 100.0;
    cfg.thickness_um = 775.0;
    cfg.film_thickness_um = 1.0;
    cfg.biaxial_modulus_gpa = 180.5;
    cfg.truth.stress_mpa = -180.0;
    cfg.truth.initial_curvature_1_per_m = 0.0;
    cfg.seed = 99;
    return cfg;
}

ssim::core::ScanConfig small_scan_config() {
    ssim::core::ScanConfig cfg;
    cfg.lines = 3;
    cfg.points_per_mm = 10;  // 100mm * 10 = 1000 points/line, small and fast
    cfg.speed_mm_per_s = 150.0;
    cfg.realtime_factor = 0.0;  // no real sleep (CLAUDE.md §6.5)
    cfg.edge_exclusion_mm = 3.0;
    return cfg;
}

struct Fixture {
    ssim::core::WaferConfig wafer_config = small_wafer_config();
    ssim::core::ScanConfig scan_config = small_scan_config();
    WaferModel wafer_model{wafer_config};
    StageSim stage{scan_config.speed_mm_per_s, scan_config.realtime_factor};
    LaserSensorSim laser{stage, wafer_model, 0.5e-6, wafer_model.seed()};
    ssim::core::EventBus bus;
    ssim::core::AlarmManager alarms{bus};
    ssim::core::BoundedQueue<ssim::core::SampleBlock> queue{64,
                                                            ssim::core::BackpressurePolicy::kBlock};

    std::mutex done_mutex;
    bool completed = false;
    bool stopped = false;
    std::string completed_wafer_id;

    std::unique_ptr<ScanThread> make_scan_thread(std::vector<ssim::core::FaultConfig> faults = {}) {
        return std::make_unique<ScanThread>(
            stage, laser, wafer_model, scan_config, std::move(faults), queue, bus, alarms,
            [this](std::string wafer_id) {
                std::lock_guard lock(done_mutex);
                completed = true;
                completed_wafer_id = std::move(wafer_id);
            },
            [this] {
                std::lock_guard lock(done_mutex);
                stopped = true;
            });
    }
};

TEST(ScanThread, CompletesAllLinesAndPushesOneBlockPerLine) {
    Fixture f;
    auto scan = f.make_scan_thread();

    scan->start("W001");
    scan->join();

    {
        std::lock_guard lock(f.done_mutex);
        EXPECT_TRUE(f.completed);
        EXPECT_FALSE(f.stopped);
        EXPECT_EQ(f.completed_wafer_id, "W001");
    }
    EXPECT_EQ(f.queue.size(), static_cast<std::size_t>(f.scan_config.lines));

    auto block = f.queue.pop();
    ASSERT_TRUE(block.has_value());
    EXPECT_EQ(block->line_index, 0u);
    EXPECT_NEAR(block->angle_rad, 0.0, 1e-12);
    EXPECT_GT(block->positions_m.size(), 0u);
    EXPECT_EQ(block->positions_m.size(), block->heights_m.size());
}

TEST(ScanThread, LineAnglesAreEvenlySpacedOver180Degrees) {
    Fixture f;
    auto scan = f.make_scan_thread();
    scan->start("W001");
    scan->join();

    for (int i = 0; i < f.scan_config.lines; ++i) {
        auto block = f.queue.pop();
        ASSERT_TRUE(block.has_value());
        const double expected = M_PI * static_cast<double>(i) / f.scan_config.lines;
        EXPECT_NEAR(block->angle_rad, expected, 1e-9);
    }
}

// FR-SCN-3: an abort mid-scan stops promptly, leaves fewer than the full
// sample count, and reports via on_stopped rather than on_lines_complete.
TEST(ScanThread, AbortMidScanStopsPromptlyAndCallsOnStopped) {
    Fixture f;
    auto hooked_stage = std::make_unique<HookedStage>();
    // A fresh sensor bound to hooked_stage, not f.laser (which is bound to
    // f.stage) — this test only exercises abort mechanics/timing, not
    // sensor data fidelity.
    LaserSensorSim hooked_laser(*hooked_stage, f.wafer_model, 0.5e-6, f.wafer_model.seed());
    ScanThread scan(
        *hooked_stage, hooked_laser, f.wafer_model, f.scan_config, {}, f.queue, f.bus, f.alarms,
        [&f](std::string wafer_id) {
            std::lock_guard lock(f.done_mutex);
            f.completed = true;
            f.completed_wafer_id = std::move(wafer_id);
        },
        [&f] {
            std::lock_guard lock(f.done_mutex);
            f.stopped = true;
        });
    hooked_stage->trigger_at = 50;  // well short of a full line's ~1000 points
    hooked_stage->on_trigger = [&scan] { scan.request_abort(); };

    scan.start("W001");
    scan.join();

    std::lock_guard lock(f.done_mutex);
    EXPECT_TRUE(f.stopped);
    EXPECT_FALSE(f.completed);
    // At most one partial block (from the line that was aborted mid-way,
    // if move_to had already run once before the trigger) reached the
    // queue — never the full line count.
    EXPECT_LT(f.queue.size(), static_cast<std::size_t>(f.scan_config.lines));
}

TEST(ScanThread, SpikeFaultOnlyAppliesToMatchingWaferId) {
    Fixture f;
    ssim::core::FaultConfig fc;
    fc.wafer = "W999";  // does not match the wafer actually scanned
    fc.type = "spike";
    fc.rate = 1.0;  // would offset every sample if it applied
    fc.amplitude_um = 1000.0;
    auto scan = f.make_scan_thread({fc});

    scan->start("W001");
    scan->join();

    auto block = f.queue.pop();
    ASSERT_TRUE(block.has_value());
    // With no matching fault, every height should equal the (noisy) laser
    // reading, which stays within a few sigma of zero for this flat wafer
    // (k0 = 0, no tilt) — nowhere near the 1000 um spike amplitude.
    for (double h : block->heights_m) {
        EXPECT_LT(std::abs(h), 1e-4);
    }
}

}  // namespace
}  // namespace ssim::hw
