#include "main_window.hpp"

#include <QAction>
#include <QComboBox>
#include <QDesktopServices>
#include <QFileDialog>
#include <QFormLayout>
#include <QFutureWatcher>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenuBar>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QStatusBar>
#include <QUrl>
#include <QVBoxLayout>
#include <QtConcurrent>
#include <utility>

#include "settings_dialog.hpp"
#include "ssim/core/ui_rules.hpp"
#include "wafer_map_widget.hpp"

namespace ssim::qt {

namespace {
constexpr auto kUi = ssim::core::CommandSource::kUi;
}

MainWindow::MainWindow() {
    setWindowTitle(tr("StressScan-Sim operator panel"));
    build_ui();
    refresh();
}

MainWindow::~MainWindow() {
    // Detach the bridge from the bus before the runtime (and its bus) goes.
    bridge_.reset();
    runtime_.reset();
}

void MainWindow::build_ui() {
    auto* central = new QWidget;
    auto* root = new QVBoxLayout(central);

    // Status strip: comm, control mode, process state.
    auto* status = new QHBoxLayout;
    comm_label_ = new QLabel(tr("Comm: not connected"));
    process_label_ = new QLabel(tr("Process: Idle"));
    process_label_->setStyleSheet(QStringLiteral("font-weight: bold;"));
    mode_ = new QComboBox;
    mode_->addItem(tr("Offline"), static_cast<int>(ssim::core::ControlMode::kOffline));
    mode_->addItem(tr("Online-Local"), static_cast<int>(ssim::core::ControlMode::kOnlineLocal));
    mode_->addItem(tr("Online-Remote"), static_cast<int>(ssim::core::ControlMode::kOnlineRemote));
    status->addWidget(comm_label_);
    status->addSpacing(16);
    status->addWidget(new QLabel(tr("Control:")));
    status->addWidget(mode_);
    status->addSpacing(16);
    status->addWidget(process_label_);
    status->addStretch(1);
    root->addLayout(status);

    // Buttons.
    auto* buttons = new QHBoxLayout;
    start_ = new QPushButton(tr("Start"));
    stop_ = new QPushButton(tr("Stop"));
    abort_ = new QPushButton(tr("Abort"));
    clear_ = new QPushButton(tr("Clear alarm"));
    for (auto* b : {start_, stop_, abort_, clear_}) {
        buttons->addWidget(b);
    }
    buttons->addStretch(1);
    root->addLayout(buttons);

    alarm_banner_ = new QLabel;
    alarm_banner_->setStyleSheet(QStringLiteral(
        "background: #b71c1c; color: white; padding: 6px; font-weight: bold; border-radius: 3px;"));
    alarm_banner_->setWordWrap(true);
    alarm_banner_->hide();
    root->addWidget(alarm_banner_);

    // Map on the left, progress and result on the right.
    auto* middle = new QHBoxLayout;
    map_ = new WaferMapWidget;
    middle->addWidget(map_, 3);

    auto* side = new QVBoxLayout;
    progress_ = new QProgressBar;
    progress_->setRange(0, 100);
    // Explicit colours: the native macOS style drew nothing in dark mode.
    progress_->setStyleSheet(QStringLiteral(
        "QProgressBar { border: 1px solid #888; border-radius: 3px; text-align: center; "
        "min-height: 16px; }"
        "QProgressBar::chunk { background-color: #3b82f6; border-radius: 2px; }"));
    side->addWidget(new QLabel(tr("Scan progress")));
    side->addWidget(progress_);

    auto* result_box = new QGroupBox(tr("Last result"));
    auto* form = new QFormLayout(result_box);
    wafer_value_ = new QLabel(QStringLiteral("-"));
    stress_value_ = new QLabel(QStringLiteral("-"));
    curvature_value_ = new QLabel(QStringLiteral("-"));
    rms_value_ = new QLabel(QStringLiteral("-"));
    spec_value_ = new QLabel(QStringLiteral("-"));
    form->addRow(tr("Wafer"), wafer_value_);
    form->addRow(tr("Stress"), stress_value_);
    form->addRow(tr("Curvature"), curvature_value_);
    form->addRow(tr("Fit RMS"), rms_value_);
    form->addRow(tr("Spec"), spec_value_);
    side->addWidget(result_box);
    side->addStretch(1);
    middle->addLayout(side, 2);
    root->addLayout(middle, 1);

    log_ = new QPlainTextEdit;
    log_->setReadOnly(true);
    log_->setMaximumBlockCount(2000);
    log_->setMinimumHeight(110);
    root->addWidget(log_);

    setCentralWidget(central);

    auto* file_menu = menuBar()->addMenu(tr("&File"));
    load_action_ = file_menu->addAction(tr("Load config..."));
    settings_action_ = file_menu->addAction(tr("Settings..."));
    auto* open_action = file_menu->addAction(tr("Open results folder"));
    file_menu->addSeparator();
    auto* quit_action = file_menu->addAction(tr("Quit"));

    connect(start_, &QPushButton::clicked, this, &MainWindow::on_start);
    connect(stop_, &QPushButton::clicked, this, &MainWindow::on_stop);
    connect(abort_, &QPushButton::clicked, this, &MainWindow::on_abort);
    connect(clear_, &QPushButton::clicked, this, &MainWindow::on_clear);
    connect(mode_, QOverload<int>::of(&QComboBox::activated), this, &MainWindow::on_mode_changed);
    connect(load_action_, &QAction::triggered, this, &MainWindow::on_load_config);
    connect(settings_action_, &QAction::triggered, this, &MainWindow::on_settings);
    connect(open_action, &QAction::triggered, this, &MainWindow::on_open_results);
    connect(quit_action, &QAction::triggered, this, &QWidget::close);

    resize(880, 720);
}

void MainWindow::reset_views() {
    map_->clear();
    progress_->setValue(0);
    for (auto* l : {wafer_value_, stress_value_, curvature_value_, rms_value_, spec_value_}) {
        l->setText(QStringLiteral("-"));
    }
    alarm_text_.clear();
    alarm_banner_->hide();
}

QString MainWindow::rebuild(const ssim::core::Config& config) {
    if (in_flight_ > 0) {
        return tr("A command is still running.");
    }
    // Only ever called while Idle (or before the first build), so joining the
    // machine's idle threads here is quick.
    bridge_.reset();
    runtime_.reset();
    reset_views();

    auto created = ssim::machine::MachineRuntime::create(config, config.output.dir);
    if (!created) {
        refresh();
        return QString::fromStdString(created.error().message);
    }
    runtime_ = std::move(created).value();
    config_ = config;
    bridge_ = std::make_unique<EventBridge>(runtime_->bus());

    const auto queued = Qt::QueuedConnection;
    connect(bridge_.get(), &EventBridge::machineChanged, this, &MainWindow::refresh, queued);
    connect(
        bridge_.get(), &EventBridge::progress, this,
        [this](double percent) { progress_->setValue(static_cast<int>(percent)); }, queued);
    connect(
        bridge_.get(), &EventBridge::scanStarted, this,
        [this](const QString& wafer) {
            // A new wafer must not show the previous wafer's numbers: if it
            // ends in an alarm there is no result to replace them.
            map_->clear();
            progress_->setValue(0);
            wafer_value_->setText(wafer);
            for (auto* l : {stress_value_, curvature_value_, rms_value_, spec_value_}) {
                l->setText(QStringLiteral("-"));
            }
            spec_value_->setStyleSheet(QString());
        },
        queued);
    connect(
        bridge_.get(), &EventBridge::runAborted, this,
        [this](const QString&) { progress_->setValue(0); }, queued);
    connect(
        bridge_.get(), &EventBridge::alarmSet, this,
        [this](int alid, const QString& name, const QString& reason) {
            alarm_text_ = tr("ALARM %1  %2: %3").arg(alid).arg(name, reason);
            alarm_banner_->setText(alarm_text_);
            alarm_banner_->show();
        },
        queued);
    connect(
        bridge_.get(), &EventBridge::resultReady, this,
        [this](const QString& wafer, double stress, double unc, double curv, double rms,
               bool out_of_spec) {
            wafer_value_->setText(wafer);
            stress_value_->setText(tr("%1 MPa  +/- %2").arg(stress, 0, 'f', 1).arg(unc, 0, 'f', 2));
            curvature_value_->setText(tr("%1 /m").arg(curv, 0, 'g', 4));
            rms_value_->setText(tr("%1 um").arg(rms, 0, 'f', 3));
            spec_value_->setText(out_of_spec ? tr("OUT OF SPEC") : tr("in spec"));
            spec_value_->setStyleSheet(
                out_of_spec ? QStringLiteral("color: #b71c1c; font-weight: bold;") : QString());
        },
        queued);
    connect(
        bridge_.get(), &EventBridge::mapReady, this,
        [this](const MapPtr& map, double edge_mm) { map_->set_map(map, edge_mm); }, queued);
    connect(
        bridge_.get(), &EventBridge::logLines, this,
        [this](const QStringList& lines) {
            for (const auto& line : lines) {
                log_->appendPlainText(line);
            }
        },
        queued);

    log_->appendPlainText(tr("machine ready, output in %1")
                              .arg(QString::fromStdString(runtime_->run_dir().string())));
    refresh();
    return QString();
}

void MainWindow::refresh() {
    if (!runtime_) {
        for (auto* b : {start_, stop_, abort_, clear_}) {
            b->setEnabled(false);
        }
        mode_->setEnabled(false);
        load_action_->setEnabled(true);
        settings_action_->setEnabled(true);
        return;
    }
    const ssim::core::MachineSnapshot snap = runtime_->api().snapshot();
    const ssim::core::ActionRules rules =
        ssim::core::rules_for(snap.process_state, snap.control_mode, snap.alarm_active);

    start_->setEnabled(rules.can_start);
    stop_->setEnabled(rules.can_stop);
    abort_->setEnabled(rules.can_abort);
    clear_->setEnabled(rules.can_clear_alarm);
    mode_->setEnabled(rules.can_change_mode);
    load_action_->setEnabled(rules.can_change_mode && in_flight_ == 0);
    settings_action_->setEnabled(rules.can_change_mode && in_flight_ == 0);

    process_label_->setText(tr("Process: %1").arg(ssim::core::to_string(snap.process_state)));
    const int mode_index = mode_->findData(static_cast<int>(snap.control_mode));
    if (mode_index >= 0 && mode_index != mode_->currentIndex()) {
        mode_->setCurrentIndex(mode_index);
    }
    if (snap.alarm_active) {
        if (!alarm_text_.isEmpty()) {
            alarm_banner_->show();
        }
    } else {
        alarm_banner_->hide();
    }
}

void MainWindow::run_command(Command command) {
    if (!runtime_) {
        return;
    }
    ssim::core::MachineApi* api = &runtime_->api();
    ++in_flight_;
    auto* watcher = new QFutureWatcher<QString>(this);
    connect(watcher, &QFutureWatcher<QString>::finished, this, [this, watcher] {
        const QString error = watcher->result();
        --in_flight_;
        watcher->deleteLater();
        if (!error.isEmpty()) {
            statusBar()->showMessage(error, 6000);
        }
        refresh();
    });
    watcher->setFuture(QtConcurrent::run([command = std::move(command), api]() -> QString {
        auto result = command(*api);
        return result ? QString() : QString::fromStdString(result.error().message);
    }));
}

void MainWindow::on_start() {
    if (!runtime_) return;
    const std::string wafer_id = runtime_->next_wafer_id();
    run_command([wafer_id](ssim::core::MachineApi& api) { return api.start(wafer_id, 1, kUi); });
}

void MainWindow::on_stop() {
    run_command([](ssim::core::MachineApi& api) { return api.stop(kUi); });
}

void MainWindow::on_abort() {
    run_command([](ssim::core::MachineApi& api) { return api.abort(kUi); });
}

void MainWindow::on_clear() {
    run_command([](ssim::core::MachineApi& api) { return api.clear_alarm(kUi); });
}

void MainWindow::on_mode_changed(int index) {
    const auto mode = static_cast<ssim::core::ControlMode>(mode_->itemData(index).toInt());
    run_command([mode](ssim::core::MachineApi& api) { return api.set_control_mode(mode, kUi); });
}

void MainWindow::on_load_config() {
    const QString path =
        QFileDialog::getOpenFileName(this, tr("Load config"), QString(), tr("JSON (*.json)"));
    if (path.isEmpty()) {
        return;
    }
    auto loaded = ssim::core::load_config_file(path.toStdString());
    if (!loaded) {
        statusBar()->showMessage(QString::fromStdString(loaded.error().message), 8000);
        return;
    }
    const QString error = rebuild(loaded.value().config);
    if (!error.isEmpty()) {
        statusBar()->showMessage(error, 8000);
        return;
    }
    for (const auto& warning : loaded.value().warnings) {
        log_->appendPlainText(tr("config warning: %1").arg(QString::fromStdString(warning)));
    }
    log_->appendPlainText(tr("loaded %1").arg(path));
}

void MainWindow::on_settings() {
    SettingsDialog dialog(config_, this);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }
    const QString error = rebuild(dialog.config());
    if (!error.isEmpty()) {
        statusBar()->showMessage(error, 8000);
    }
}

void MainWindow::on_open_results() {
    if (!runtime_) return;
    std::filesystem::path dir = runtime_->last_wafer_dir();
    if (dir.empty()) {
        dir = runtime_->run_dir();
    }
    QDesktopServices::openUrl(QUrl::fromLocalFile(QString::fromStdString(dir.string())));
}

}  // namespace ssim::qt
