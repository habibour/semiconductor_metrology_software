// Offscreen smoke test for the operator panel (FR-UI-1, FR-UI-3, NFR-PERF-4).
// It drives the real MainWindow with real clicks against a real MachineRuntime.

#include <QAction>
#include <QComboBox>
#include <QDir>
#include <QElapsedTimer>
#include <QFont>
#include <QImage>
#include <QLabel>
#include <QPainter>
#include <QProgressBar>
#include <QPushButton>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTimer>
#include <QtTest>
#include <algorithm>

#include "main_window.hpp"

using ssim::qt::MainWindow;

namespace {

ssim::core::Config make_config(const QTemporaryDir& dir, double rtf) {
    ssim::core::Config cfg;
    cfg.output.dir = dir.path().toStdString();
    cfg.scan.realtime_factor = rtf;
    return cfg;
}

QLabel* label_with_prefix(MainWindow& w, const QString& prefix) {
    for (auto* l : w.findChildren<QLabel*>()) {
        if (l->text().startsWith(prefix)) return l;
    }
    return nullptr;
}

bool has_label_text(MainWindow& w, const QString& text) {
    for (auto* l : w.findChildren<QLabel*>()) {
        if (l->text() == text) return true;
    }
    return false;
}

QAction* action_named(MainWindow& w, const QString& text) {
    for (auto* a : w.findChildren<QAction*>()) {
        if (a->text() == text) return a;
    }
    return nullptr;
}

// SSIM_SCREENSHOT_DIR=/some/dir saves a PNG of the window at each stage.
void shot(MainWindow& w, const char* name) {
    const QByteArray dir = qgetenv("SSIM_SCREENSHOT_DIR");
    if (!dir.isEmpty()) {
        w.grab().save(
            QDir(QString::fromLocal8Bit(dir)).filePath(QString::fromLatin1(name) + ".png"));
    }
}

}  // namespace

// Grabs the window every ~120 ms into numbered PNG files, with a caption drawn
// on each frame. Used only by the on-demand demoFrames test to make the
// README's demo GIF: the frames are rendered by the real widgets on Qt's
// offscreen platform, they are not a screen recording.
class FrameRecorder : public QObject {
public:
    FrameRecorder(QWidget* widget, const QString& dir) : widget_(widget), dir_(dir) {
        timer_.setInterval(120);
        QObject::connect(&timer_, &QTimer::timeout, [this] { capture(); });
        timer_.start();
    }
    void set_caption(const QString& text) { caption_ = text; }
    void hold(int ms) { QTest::qWait(ms); }
    int frames() const { return index_; }

private:
    void capture() {
        QImage image = widget_->grab().toImage().convertToFormat(QImage::Format_RGB32);
        if (!caption_.isEmpty()) {
            QPainter painter(&image);
            painter.setRenderHint(QPainter::TextAntialiasing);
            QFont font = painter.font();
            font.setPointSize(15);
            font.setBold(true);
            painter.setFont(font);
            const QRect bar(0, image.height() - 40, image.width(), 40);
            painter.fillRect(bar, QColor(20, 20, 20, 215));
            painter.setPen(Qt::white);
            painter.drawText(bar.adjusted(12, 0, -12, 0), Qt::AlignVCenter | Qt::AlignLeft,
                             caption_);
        }
        image.save(
            QDir(dir_).filePath(QStringLiteral("frame_%1.png").arg(index_++, 4, 10, QChar('0'))));
    }

    QWidget* widget_;
    QString dir_;
    QString caption_;
    QTimer timer_;
    int index_ = 0;
};

class PanelSmokeTest : public QObject {
    Q_OBJECT

private Q_SLOTS:
    // On demand only: SSIM_FRAMES_DIR=/some/dir records the demo session as
    // numbered PNG frames (see scripts/make_demo_gif.sh). Skipped otherwise.
    void demoFrames() {
        const QString frames_dir = qEnvironmentVariable("SSIM_FRAMES_DIR");
        if (frames_dir.isEmpty()) {
            qInfo() << "skipped: set SSIM_FRAMES_DIR to record demo frames";
            return;
        }
        QTemporaryDir dir;
        auto cfg = make_config(dir, 3.0);
        ssim::core::FaultConfig fault;  // the second wafer has a sensor fault
        fault.wafer = "W002";
        fault.type = "spike";
        fault.rate = 0.05;
        fault.amplitude_um = 40.0;
        cfg.faults.push_back(fault);

        MainWindow w;
        QVERIFY(w.rebuild(cfg).isEmpty());
        w.show();
        auto* process = label_with_prefix(w, QStringLiteral("Process:"));
        QVERIFY(process);
        FrameRecorder recorder(&w, frames_dir);

        recorder.set_caption(
            QStringLiteral("Idle. The machine is Online-Local; Start is enabled."));
        recorder.hold(1400);

        recorder.set_caption(
            QStringLiteral("Start wafer W001: progress, then the wafer map and the result"));
        QTest::mouseClick(w.start_button(), Qt::LeftButton);
        QTRY_VERIFY_WITH_TIMEOUT(w.runtime()->api().snapshot().last_result.has_value(), 60000);
        QTRY_VERIFY_WITH_TIMEOUT(process->text() == QStringLiteral("Process: Idle"), 10000);
        recorder.hold(4500);

        recorder.set_caption(QStringLiteral("Wafer W002 has an injected sensor fault"));
        QTest::mouseClick(w.start_button(), Qt::LeftButton);
        QTRY_VERIFY_WITH_TIMEOUT(!w.alarm_banner()->isHidden(), 60000);
        recorder.set_caption(
            QStringLiteral("Alarm 1001: no result is reported and Start stays disabled"));
        recorder.hold(2600);

        recorder.set_caption(QStringLiteral("Clear alarm: the machine is ready again"));
        QTest::mouseClick(w.clear_button(), Qt::LeftButton);
        QTRY_VERIFY_WITH_TIMEOUT(w.alarm_banner()->isHidden(), 5000);
        recorder.hold(1800);

        recorder.set_caption(
            QStringLiteral("Online-Remote: the host is in charge, so the operator cannot Start"));
        w.mode_combo()->setCurrentIndex(2);
        Q_EMIT w.mode_combo()->activated(2);
        QTRY_VERIFY_WITH_TIMEOUT(!w.start_button()->isEnabled(), 5000);
        recorder.hold(2600);

        qInfo() << "recorded" << recorder.frames() << "frames";
        QVERIFY(recorder.frames() > 40);
    }

    // Scripted version of the manual checklist: real clicks, real machine.
    void guidedWalkthrough() {
        QTemporaryDir dir;
        auto cfg = make_config(dir, 8.0);
        ssim::core::FaultConfig fault;
        fault.wafer = "W002";
        fault.type = "spike";
        fault.rate = 0.05;
        fault.amplitude_um = 40.0;
        cfg.faults.push_back(fault);

        MainWindow w;
        QVERIFY(w.rebuild(cfg).isEmpty());
        w.show();
        auto* process = label_with_prefix(w, QStringLiteral("Process:"));
        QVERIFY(process);
        auto* bar = w.findChild<QProgressBar*>();
        QVERIFY(bar);
        auto idle = [&] { return process->text() == QStringLiteral("Process: Idle"); };

        // 1. startup
        QCOMPARE(w.mode_combo()->currentText(), QStringLiteral("Online-Local"));
        QVERIFY(idle());
        QVERIFY(w.start_button()->isEnabled());
        QVERIFY(!w.stop_button()->isEnabled() && !w.abort_button()->isEnabled() &&
                !w.clear_button()->isEnabled());
        QVERIFY(w.alarm_banner()->isHidden());
        shot(w, "01_startup");

        // 2. normal scan, W001
        QTest::mouseClick(w.start_button(), Qt::LeftButton);
        QTRY_VERIFY_WITH_TIMEOUT(process->text() == QStringLiteral("Process: Scanning"), 5000);
        QTRY_VERIFY_WITH_TIMEOUT(bar->value() > 0, 30000);
        QVERIFY(!w.start_button()->isEnabled());
        QVERIFY(w.stop_button()->isEnabled() && w.abort_button()->isEnabled());
        // 11. config actions are unavailable while busy
        QVERIFY(!action_named(w, QStringLiteral("Load config..."))->isEnabled());
        QVERIFY(!action_named(w, QStringLiteral("Settings..."))->isEnabled());
        QVERIFY(!w.mode_combo()->isEnabled());
        shot(w, "02_scanning");
        QTRY_VERIFY_WITH_TIMEOUT(w.runtime()->api().snapshot().last_result.has_value(), 60000);
        QTRY_VERIFY_WITH_TIMEOUT(idle() && w.start_button()->isEnabled(), 10000);
        QVERIFY(has_label_text(w, QStringLiteral("W001")));
        QVERIFY(has_label_text(w, QStringLiteral("in spec")));
        QCOMPARE(bar->value(), 100);
        shot(w, "03_result_w001");

        // 3. W002 has the injected fault: alarm, no result, Start blocked
        QTest::mouseClick(w.start_button(), Qt::LeftButton);
        QTRY_VERIFY_WITH_TIMEOUT(!w.alarm_banner()->isHidden(), 60000);
        QVERIFY(w.alarm_banner()->text().contains(QStringLiteral("1001")));
        QTRY_VERIFY_WITH_TIMEOUT(process->text() == QStringLiteral("Process: Alarm"), 5000);
        QVERIFY(!w.start_button()->isEnabled());
        QVERIFY(w.clear_button()->isEnabled());
        QCOMPARE(w.runtime()->api().snapshot().last_result->wafer_id, std::string("W001"));
        // No stale numbers from W001 under W002's name.
        QVERIFY(has_label_text(w, QStringLiteral("W002")));
        QVERIFY(!has_label_text(w, QStringLiteral("in spec")));
        for (auto* l : w.findChildren<QLabel*>()) {
            QVERIFY2(!l->text().contains(QStringLiteral("-180.0 MPa")), "stale result shown");
        }
        shot(w, "04_alarm");

        // 4. clear
        QTest::mouseClick(w.clear_button(), Qt::LeftButton);
        QTRY_VERIFY_WITH_TIMEOUT(w.alarm_banner()->isHidden() && idle(), 5000);
        QVERIFY(w.start_button()->isEnabled());

        // 5. third wafer is clean
        QTest::mouseClick(w.start_button(), Qt::LeftButton);
        QTRY_VERIFY_WITH_TIMEOUT(w.runtime()->api().snapshot().last_result.has_value() &&
                                     w.runtime()->api().snapshot().last_result->wafer_id == "W003",
                                 60000);
        QTRY_VERIFY_WITH_TIMEOUT(idle(), 10000);

        // 6. Online-Remote disables Start
        w.mode_combo()->setCurrentIndex(2);
        Q_EMIT w.mode_combo()->activated(2);
        QTRY_VERIFY_WITH_TIMEOUT(!w.start_button()->isEnabled(), 5000);
        shot(w, "05_remote");
        w.mode_combo()->setCurrentIndex(1);
        Q_EMIT w.mode_combo()->activated(1);
        QTRY_VERIFY_WITH_TIMEOUT(w.start_button()->isEnabled(), 5000);

        // 7. abort mid-scan: back to Idle, no new result
        const auto results_before = w.runtime()->api().snapshot().last_result->wafer_id;
        QTest::mouseClick(w.start_button(), Qt::LeftButton);
        QTRY_VERIFY_WITH_TIMEOUT(process->text() == QStringLiteral("Process: Scanning"), 5000);
        QTest::mouseClick(w.abort_button(), Qt::LeftButton);
        QTRY_VERIFY_WITH_TIMEOUT(idle(), 10000);
        QCOMPARE(w.runtime()->api().snapshot().last_result->wafer_id, results_before);
        QVERIFY(w.start_button()->isEnabled());
        QTRY_COMPARE_WITH_TIMEOUT(bar->value(), 0, 5000);  // no stale progress after an abort
        shot(w, "06_after_abort");

        // 13. closing the window mid-scan does not hang
        QTest::mouseClick(w.start_button(), Qt::LeftButton);
        QTRY_VERIFY_WITH_TIMEOUT(process->text() == QStringLiteral("Process: Scanning"), 5000);
        QElapsedTimer close_timer;
        close_timer.start();
        QVERIFY(w.close());
        QVERIFY(close_timer.elapsed() < 2000);
    }

    // Settings path (rebuild): different truth stress, and the spec window.
    void rebuildWithNewSettingsChangesTheResult() {
        QTemporaryDir dir;
        auto cfg = make_config(dir, 0.0);
        cfg.wafer.truth.stress_mpa = 150.0;
        cfg.analysis.stress_spec_high_mpa = 100.0;

        MainWindow w;
        QVERIFY(w.rebuild(make_config(dir, 0.0)).isEmpty());
        QVERIFY(w.rebuild(cfg).isEmpty());  // as File > Settings does
        w.show();
        QTest::mouseClick(w.start_button(), Qt::LeftButton);
        QTRY_VERIFY_WITH_TIMEOUT(w.runtime()->api().snapshot().last_result.has_value(), 30000);
        const auto result = *w.runtime()->api().snapshot().last_result;
        QCOMPARE(result.wafer_id, std::string("W001"));  // numbering restarted
        QVERIFY(std::abs(result.stress_mpa - 150.0) < 3.0);
        QVERIFY(result.out_of_spec);
        QTRY_VERIFY_WITH_TIMEOUT(has_label_text(w, QStringLiteral("OUT OF SPEC")), 5000);
        shot(w, "07_out_of_spec");
    }

    void buttonsFollowTheRules() {
        QTemporaryDir dir;
        MainWindow window;
        QVERIFY(window.rebuild(make_config(dir, 0.0)).isEmpty());
        window.show();

        QVERIFY(window.start_button()->isEnabled());
        QVERIFY(!window.stop_button()->isEnabled());
        QVERIFY(!window.abort_button()->isEnabled());
        QVERIFY(!window.clear_button()->isEnabled());

        // PRD 6.5: in Online-Remote the operator cannot Start.
        window.mode_combo()->setCurrentIndex(2);
        Q_EMIT window.mode_combo()->activated(2);
        QTRY_VERIFY_WITH_TIMEOUT(!window.start_button()->isEnabled(), 5000);

        window.mode_combo()->setCurrentIndex(1);
        Q_EMIT window.mode_combo()->activated(1);
        QTRY_VERIFY_WITH_TIMEOUT(window.start_button()->isEnabled(), 5000);
    }

    void alarmBannerAppearsAndClears() {
        QTemporaryDir dir;
        auto cfg = make_config(dir, 0.0);
        ssim::core::FaultConfig fault;
        fault.wafer = "W002";
        fault.type = "spike";
        fault.rate = 0.05;
        fault.amplitude_um = 40.0;
        cfg.faults.push_back(fault);

        MainWindow window;
        QVERIFY(window.rebuild(cfg).isEmpty());
        window.show();

        QTest::mouseClick(window.start_button(), Qt::LeftButton);
        QTRY_VERIFY_WITH_TIMEOUT(window.runtime()->api().snapshot().last_result.has_value(), 30000);
        QTRY_VERIFY_WITH_TIMEOUT(window.start_button()->isEnabled(), 30000);
        QVERIFY(window.alarm_banner()->isHidden());

        QTest::mouseClick(window.start_button(), Qt::LeftButton);
        QTRY_VERIFY_WITH_TIMEOUT(!window.alarm_banner()->isHidden(), 30000);
        QVERIFY(window.alarm_banner()->text().contains(QStringLiteral("1001")));
        QTRY_VERIFY_WITH_TIMEOUT(window.clear_button()->isEnabled(), 5000);
        QVERIFY(!window.start_button()->isEnabled());  // an alarm blocks scans

        QTest::mouseClick(window.clear_button(), Qt::LeftButton);
        QTRY_VERIFY_WITH_TIMEOUT(window.alarm_banner()->isHidden(), 5000);
        QTRY_VERIFY_WITH_TIMEOUT(window.start_button()->isEnabled(), 5000);
    }

    // FR-UI-3 / NFR-PERF-4: measured, not assumed. A 10 ms heartbeat on the
    // GUI thread shows how long the event loop was ever unresponsive, and the
    // progress signal count shows the update rate.
    void scanKeepsGuiResponsiveAndRateLimited() {
        QTemporaryDir dir;
        MainWindow window;
        QVERIFY(window.rebuild(make_config(dir, 4.0)).isEmpty());  // ~3 s per wafer
        window.show();

        QSignalSpy progress(window.bridge(), &ssim::qt::EventBridge::progress);

        qint64 max_gap_ms = 0;
        QElapsedTimer since_tick;
        since_tick.start();
        QTimer heartbeat;
        heartbeat.setInterval(10);
        QObject::connect(&heartbeat, &QTimer::timeout,
                         [&] { max_gap_ms = std::max(max_gap_ms, since_tick.restart()); });
        heartbeat.start();

        QElapsedTimer scan_time;
        scan_time.start();
        QTest::mouseClick(window.start_button(), Qt::LeftButton);
        QTRY_VERIFY_WITH_TIMEOUT(window.runtime()->api().snapshot().last_result.has_value(), 60000);
        QTRY_VERIFY_WITH_TIMEOUT(window.start_button()->isEnabled(), 10000);
        const qint64 elapsed_ms = scan_time.elapsed();
        heartbeat.stop();

        qInfo() << "scan took" << elapsed_ms << "ms; progress signals" << progress.count()
                << "; max GUI-thread gap" << max_gap_ms << "ms";

        // Optional: SSIM_SCREENSHOT=/path/panel.png saves what the window
        // looks like after the scan (useful for the README and for review).
        const QByteArray shot = qgetenv("SSIM_SCREENSHOT");
        if (!shot.isEmpty()) {
            QVERIFY(window.grab().save(QString::fromLocal8Bit(shot)));
        }

        QVERIFY(progress.count() >= 1);
        QVERIFY(progress.count() <= elapsed_ms / ssim::qt::EventBridge::kTickMs + 2);
        QVERIFY2(max_gap_ms <= 50, "GUI event loop stalled for more than 50 ms");
    }
};

QTEST_MAIN(PanelSmokeTest)
#include "panel_smoke_test.moc"
