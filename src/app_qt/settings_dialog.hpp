#pragma once

// Thread-safety: GUI thread only.
//
// FR-UI-4: edits the small set of values that make sense to change between
// runs. Every field is range-limited to the PRD's stated bounds, so the
// resulting Config is valid by construction. It is applied by building a new
// machine runtime while Idle; this is not the S2F15 runtime-constants path.

#include <QDialog>

#include "ssim/core/config.hpp"

class QDoubleSpinBox;

namespace ssim::qt {

class SettingsDialog : public QDialog {
    Q_OBJECT
public:
    SettingsDialog(const ssim::core::Config& current, QWidget* parent = nullptr);

    // The current config with the dialog's values applied.
    ssim::core::Config config() const;

    void accept() override;

private:
    ssim::core::Config base_;
    QDoubleSpinBox* edge_exclusion_mm_;
    QDoubleSpinBox* fit_rms_limit_um_;
    QDoubleSpinBox* spec_low_mpa_;
    QDoubleSpinBox* spec_high_mpa_;
    QDoubleSpinBox* truth_stress_mpa_;
    QDoubleSpinBox* realtime_factor_;
};

}  // namespace ssim::qt
