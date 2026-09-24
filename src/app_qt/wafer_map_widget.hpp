#pragma once

// Thread-safety: GUI thread only.
//
// FR-UI-2: paints the height grid ssim_analysis already produced (no
// interpolation here). The colour image is built once when a map arrives, not
// on every paint. Heights arrive in metres and are shown in micrometres.

#include <QImage>
#include <QWidget>

#include "event_bridge.hpp"

namespace ssim::qt {

class WaferMapWidget : public QWidget {
    Q_OBJECT
public:
    explicit WaferMapWidget(QWidget* parent = nullptr);

    void set_map(const MapPtr& map, double edge_exclusion_mm);
    void clear();

    QSize sizeHint() const override { return {420, 380}; }
    QSize minimumSizeHint() const override { return {240, 220}; }

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    QImage image_;
    double min_um_ = 0.0;
    double max_um_ = 0.0;
    double edge_ring_fraction_ = 0.0;  // ring radius / wafer radius
};

}  // namespace ssim::qt
