#include "event_bridge.hpp"

#include <QDateTime>

#include "ssim/core/events.hpp"
#include "ssim/machine/machine_events.hpp"

namespace ssim::qt {

namespace {
constexpr int kMaxPendingLogLines = 1000;
}

EventBridge::EventBridge(ssim::core::EventBus& bus, QObject* parent) : QObject(parent), bus_(bus) {
    qRegisterMetaType<MapPtr>("ssim::qt::MapPtr");
    qRegisterMetaType<MapPtr>("MapPtr");

    namespace c = ssim::core;

    subscriptions_.push_back(bus_.subscribe<c::StateChanged>([this](const c::StateChanged& e) {
        add_log(QStringLiteral("state %1 -> %2").arg(c::to_string(e.from), c::to_string(e.to)));
        Q_EMIT machineChanged();
    }));
    subscriptions_.push_back(
        bus_.subscribe<c::ControlStateChanged>([this](const c::ControlStateChanged& e) {
            add_log(
                QStringLiteral("control %1 -> %2").arg(c::to_string(e.from), c::to_string(e.to)));
            Q_EMIT machineChanged();
        }));
    subscriptions_.push_back(bus_.subscribe<c::ScanStarted>([this](const c::ScanStarted& e) {
        add_log(QStringLiteral("scan started: %1").arg(QString::fromStdString(e.wafer_id)));
        latest_progress_.store(0.0);
        progress_dirty_.store(true);
        Q_EMIT scanStarted(QString::fromStdString(e.wafer_id));
    }));
    subscriptions_.push_back(bus_.subscribe<c::RunAborted>([this](const c::RunAborted& e) {
        add_log(QStringLiteral("run aborted: %1").arg(QString::fromStdString(e.wafer_id)));
        latest_progress_.store(0.0);
        progress_dirty_.store(true);
        Q_EMIT runAborted(QString::fromStdString(e.wafer_id));
    }));
    subscriptions_.push_back(bus_.subscribe<c::ScanProgress>([this](const c::ScanProgress& e) {
        latest_progress_.store(e.percent);
        progress_dirty_.store(true);
    }));
    subscriptions_.push_back(bus_.subscribe<c::AlarmSet>([this](const c::AlarmSet& e) {
        add_log(QStringLiteral("ALARM %1 %2: %3")
                    .arg(e.alid)
                    .arg(QString::fromStdString(e.name), QString::fromStdString(e.reason)));
        Q_EMIT alarmSet(e.alid, QString::fromStdString(e.name), QString::fromStdString(e.reason));
        Q_EMIT machineChanged();
    }));
    subscriptions_.push_back(bus_.subscribe<c::AlarmCleared>([this](const c::AlarmCleared& e) {
        add_log(
            QStringLiteral("alarm cleared: %1 %2").arg(e.alid).arg(QString::fromStdString(e.name)));
        Q_EMIT alarmCleared(e.alid, QString::fromStdString(e.name));
        Q_EMIT machineChanged();
    }));
    subscriptions_.push_back(
        bus_.subscribe<c::WaferResultReady>([this](const c::WaferResultReady& e) {
            add_log(QStringLiteral("result %1: %2 MPa (+/- %3)%4")
                        .arg(QString::fromStdString(e.wafer_id))
                        .arg(e.stress_mpa, 0, 'f', 1)
                        .arg(e.stress_unc_mpa, 0, 'f', 2)
                        .arg(e.out_of_spec ? QStringLiteral(" OUT OF SPEC") : QString()));
            Q_EMIT resultReady(QString::fromStdString(e.wafer_id), e.stress_mpa, e.stress_unc_mpa,
                               e.curvature_per_m, e.fit_rms_um, e.out_of_spec);
        }));
    subscriptions_.push_back(
        bus_.subscribe<ssim::machine::WaferMapReady>([this](const ssim::machine::WaferMapReady& e) {
            Q_EMIT mapReady(e.map, e.edge_exclusion_mm);
        }));
    subscriptions_.push_back(bus_.subscribe<ssim::machine::WaferOutputsWritten>(
        [this](const ssim::machine::WaferOutputsWritten& e) {
            add_log(QStringLiteral("files %1: %2")
                        .arg(e.ok ? QStringLiteral("written to") : QStringLiteral("FAILED in"))
                        .arg(QString::fromStdString(e.dir.string())));
            Q_EMIT outputsWritten(QString::fromStdString(e.dir.string()), e.ok);
        }));

    timer_ = new QTimer(this);
    timer_->setInterval(kTickMs);
    connect(timer_, &QTimer::timeout, this, &EventBridge::on_tick);
    timer_->start();
}

EventBridge::~EventBridge() {
    // Detach before the members go away: handlers still on the bus would
    // otherwise call into a half-destroyed bridge.
    for (auto id : subscriptions_) {
        bus_.unsubscribe(id);
    }
}

void EventBridge::add_log(const QString& line) {
    const QString stamped =
        QDateTime::currentDateTime().toString(QStringLiteral("hh:mm:ss.zzz ")) + line;
    std::lock_guard lock(log_mutex_);
    if (pending_log_.size() >= kMaxPendingLogLines) {
        pending_log_.removeFirst();
    }
    pending_log_.append(stamped);
}

void EventBridge::on_tick() {
    if (progress_dirty_.exchange(false)) {
        Q_EMIT progress(latest_progress_.load());
    }
    QStringList lines;
    {
        std::lock_guard lock(log_mutex_);
        lines.swap(pending_log_);
    }
    if (!lines.isEmpty()) {
        Q_EMIT logLines(lines);
    }
}

}  // namespace ssim::qt
