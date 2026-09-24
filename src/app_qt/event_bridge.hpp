#pragma once

// Thread-safety: constructed, used and destroyed on the GUI thread. The
// EventBus handlers it installs run on whichever machine thread publishes
// (controller, scan, processing); they never touch a widget. They either emit
// a signal (queued to the GUI thread by the receiver's connection) or leave a
// value in a latest-value slot that a GUI-thread timer picks up.
//
// FR-UI-3: progress updates are coalesced through a 33 ms timer, so the
// panel sees at most ~30 per second no matter how fast the scan reports.
// Log lines are batched by the same timer.

#include <QObject>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <atomic>
#include <memory>
#include <mutex>
#include <vector>

#include "ssim/analysis/wafer_map.hpp"
#include "ssim/core/event_bus.hpp"

namespace ssim::qt {

using MapPtr = std::shared_ptr<const ssim::analysis::WaferMap>;

class EventBridge : public QObject {
    Q_OBJECT
public:
    explicit EventBridge(ssim::core::EventBus& bus, QObject* parent = nullptr);
    ~EventBridge() override;

    // Minimum time between progress signals; exposed for the smoke test.
    static constexpr int kTickMs = 33;

Q_SIGNALS:
    // State, control mode or alarm status changed: re-read the snapshot.
    void machineChanged();
    void alarmSet(int alid, QString name, QString reason);
    void alarmCleared(int alid, QString name);
    void resultReady(QString wafer_id, double stress_mpa, double stress_unc_mpa,
                     double curvature_per_m, double fit_rms_um, bool out_of_spec);
    void mapReady(ssim::qt::MapPtr map, double edge_exclusion_mm);
    void outputsWritten(QString dir, bool ok);
    void scanStarted(QString wafer_id);
    void runAborted(QString wafer_id);
    void progress(double percent);
    void logLines(QStringList lines);

private:
    void on_tick();
    void add_log(const QString& line);

    ssim::core::EventBus& bus_;
    std::vector<ssim::core::EventBus::SubscriptionId> subscriptions_;

    std::atomic<double> latest_progress_{0.0};
    std::atomic<bool> progress_dirty_{false};

    std::mutex log_mutex_;
    QStringList pending_log_;

    QTimer* timer_ = nullptr;
};

}  // namespace ssim::qt

Q_DECLARE_METATYPE(ssim::qt::MapPtr)
