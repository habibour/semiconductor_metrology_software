#include "settings_dialog.hpp"

#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QLabel>
#include <QMessageBox>
#include <QVBoxLayout>

namespace ssim::qt {

namespace {

QDoubleSpinBox* make_spin(double lo, double hi, double value, int decimals, const QString& suffix) {
    auto* box = new QDoubleSpinBox;
    box->setRange(lo, hi);
    box->setDecimals(decimals);
    box->setValue(value);
    box->setSuffix(suffix);
    return box;
}

}  // namespace

SettingsDialog::SettingsDialog(const ssim::core::Config& current, QWidget* parent)
    : QDialog(parent), base_(current) {
    setWindowTitle(tr("Settings"));

    // Ranges follow PRD 7.1 / 8.6.6.
    edge_exclusion_mm_ = make_spin(0.0, 20.0, current.scan.edge_exclusion_mm, 1, tr(" mm"));
    fit_rms_limit_um_ = make_spin(0.1, 50.0, current.analysis.fit_rms_limit_um, 2, tr(" um"));
    spec_low_mpa_ = make_spin(-5000.0, 5000.0, current.analysis.stress_spec_low_mpa, 0, tr(" MPa"));
    spec_high_mpa_ =
        make_spin(-5000.0, 5000.0, current.analysis.stress_spec_high_mpa, 0, tr(" MPa"));
    truth_stress_mpa_ = make_spin(-2000.0, 2000.0, current.wafer.truth.stress_mpa, 0, tr(" MPa"));
    realtime_factor_ = make_spin(0.0, 100.0, current.scan.realtime_factor, 2, QString());
    realtime_factor_->setToolTip(tr("1 = realistic timing, 0 = as fast as possible"));

    auto* form = new QFormLayout;
    form->addRow(tr("Edge exclusion"), edge_exclusion_mm_);
    form->addRow(tr("Fit RMS limit"), fit_rms_limit_um_);
    form->addRow(tr("Stress spec, low"), spec_low_mpa_);
    form->addRow(tr("Stress spec, high"), spec_high_mpa_);
    form->addRow(tr("Simulated true stress"), truth_stress_mpa_);
    form->addRow(tr("Real-time factor"), realtime_factor_);

    auto* note = new QLabel(tr("Applying rebuilds the machine (only possible while idle)."));
    note->setWordWrap(true);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(buttons, &QDialogButtonBox::accepted, this, &SettingsDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &SettingsDialog::reject);

    auto* layout = new QVBoxLayout(this);
    layout->addLayout(form);
    layout->addWidget(note);
    layout->addWidget(buttons);
}

ssim::core::Config SettingsDialog::config() const {
    ssim::core::Config cfg = base_;
    cfg.scan.edge_exclusion_mm = edge_exclusion_mm_->value();
    cfg.analysis.fit_rms_limit_um = fit_rms_limit_um_->value();
    cfg.analysis.stress_spec_low_mpa = spec_low_mpa_->value();
    cfg.analysis.stress_spec_high_mpa = spec_high_mpa_->value();
    cfg.wafer.truth.stress_mpa = truth_stress_mpa_->value();
    cfg.scan.realtime_factor = realtime_factor_->value();
    return cfg;
}

void SettingsDialog::accept() {
    if (spec_low_mpa_->value() >= spec_high_mpa_->value()) {
        QMessageBox::warning(this, tr("Settings"),
                             tr("The low spec limit must be below the high spec limit."));
        return;
    }
    QDialog::accept();
}

}  // namespace ssim::qt
