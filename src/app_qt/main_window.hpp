#pragma once

// Thread-safety: GUI thread only. The machine runs on its own threads; this
// window reads MachineApi::snapshot() (non-blocking) and receives events
// through EventBridge. Commands are run on a worker thread through
// QtConcurrent, because MachineApi's command methods wait for the
// controller thread and the GUI thread must never wait (FR-UI-3).
//
// FR-UI-1: button enabling comes from ssim::core::rules_for(), which is
// unit-tested without Qt. The controller still has the final say and its
// rejection reason is shown in the status bar.

#include <QMainWindow>
#include <QString>
#include <functional>
#include <memory>

#include "event_bridge.hpp"
#include "ssim/core/config.hpp"
#include "ssim/core/machine_api.hpp"
#include "ssim/machine/machine_runtime.hpp"

class QComboBox;
class QLabel;
class QPlainTextEdit;
class QProgressBar;
class QPushButton;

namespace ssim::qt {

class WaferMapWidget;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    MainWindow();
    ~MainWindow() override;

    // Builds (or rebuilds) the machine from config. Returns an empty string
    // on success, otherwise a message. Refused while a command is in flight.
    QString rebuild(const ssim::core::Config& config);

    // For the offscreen smoke test.
    QPushButton* start_button() const { return start_; }
    QPushButton* stop_button() const { return stop_; }
    QPushButton* abort_button() const { return abort_; }
    QPushButton* clear_button() const { return clear_; }
    QComboBox* mode_combo() const { return mode_; }
    QLabel* alarm_banner() const { return alarm_banner_; }
    ssim::machine::MachineRuntime* runtime() const { return runtime_.get(); }
    EventBridge* bridge() const { return bridge_.get(); }

private Q_SLOTS:
    void refresh();
    void on_start();
    void on_stop();
    void on_abort();
    void on_clear();
    void on_mode_changed(int index);
    void on_load_config();
    void on_settings();
    void on_open_results();

private:
    using Command =
        std::function<ssim::core::Result<ssim::core::ProcessState>(ssim::core::MachineApi&)>;

    void build_ui();
    void run_command(Command command);
    void reset_views();

    std::unique_ptr<ssim::machine::MachineRuntime> runtime_;
    std::unique_ptr<EventBridge> bridge_;  // declared after runtime_: destroyed first
    ssim::core::Config config_;
    int in_flight_ = 0;
    QString alarm_text_;

    QLabel* comm_label_ = nullptr;
    QLabel* process_label_ = nullptr;
    QComboBox* mode_ = nullptr;
    QPushButton* start_ = nullptr;
    QPushButton* stop_ = nullptr;
    QPushButton* abort_ = nullptr;
    QPushButton* clear_ = nullptr;
    QLabel* alarm_banner_ = nullptr;
    QProgressBar* progress_ = nullptr;
    WaferMapWidget* map_ = nullptr;
    QLabel* wafer_value_ = nullptr;
    QLabel* stress_value_ = nullptr;
    QLabel* curvature_value_ = nullptr;
    QLabel* rms_value_ = nullptr;
    QLabel* spec_value_ = nullptr;
    QPlainTextEdit* log_ = nullptr;
    QAction* load_action_ = nullptr;
    QAction* settings_action_ = nullptr;
};

}  // namespace ssim::qt
