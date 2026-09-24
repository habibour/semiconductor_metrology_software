#include "wafer_map_widget.hpp"

#include <QColor>
#include <QPainter>
#include <QPen>
#include <algorithm>
#include <cmath>
#include <limits>

namespace ssim::qt {

namespace {

// Blue -> cyan -> green -> yellow -> red, t in [0, 1].
QColor ramp(double t) {
    t = std::clamp(t, 0.0, 1.0);
    const double h = (1.0 - t) * 240.0;  // hue 240 (blue) down to 0 (red)
    return QColor::fromHsvF(h / 360.0, 0.85, 0.95);
}

}  // namespace

WaferMapWidget::WaferMapWidget(QWidget* parent) : QWidget(parent) {
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
}

void WaferMapWidget::clear() {
    image_ = QImage();
    update();
}

void WaferMapWidget::set_map(const MapPtr& map, double edge_exclusion_mm) {
    if (!map || map->width <= 0 || map->height <= 0) {
        clear();
        return;
    }
    double lo = std::numeric_limits<double>::infinity();
    double hi = -std::numeric_limits<double>::infinity();
    for (double h : map->heights_m) {
        if (std::isfinite(h)) {
            lo = std::min(lo, h);
            hi = std::max(hi, h);
        }
    }
    if (!(lo <= hi)) {
        clear();
        return;
    }
    min_um_ = lo * 1e6;
    max_um_ = hi * 1e6;
    const double span = std::max(hi - lo, 1e-12);

    QImage img(map->width, map->height, QImage::Format_ARGB32);
    img.fill(Qt::transparent);
    for (int j = 0; j < map->height; ++j) {
        // Row j grows with +y; flip so +y is up on screen.
        const int out_row = map->height - 1 - j;
        for (int i = 0; i < map->width; ++i) {
            const double h = map->heights_m[static_cast<std::size_t>(j) * map->width + i];
            if (std::isfinite(h)) {
                img.setPixelColor(i, out_row, ramp((h - lo) / span));
            }
        }
    }
    image_ = std::move(img);

    const double radius_mm = map->diameter_m * 1000.0 / 2.0;
    edge_ring_fraction_ =
        radius_mm > 0.0 ? std::max(0.0, (radius_mm - edge_exclusion_mm) / radius_mm) : 0.0;
    update();
}

void WaferMapWidget::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    p.fillRect(rect(), palette().window());

    const int bar_w = 16;
    const int label_w = 64;
    const int margin = 10;
    const QRect avail =
        rect().adjusted(margin, margin, -(margin + bar_w + label_w + margin), -margin);
    const int side = std::max(0, std::min(avail.width(), avail.height()));
    const QRect square(avail.left() + (avail.width() - side) / 2,
                       avail.top() + (avail.height() - side) / 2, side, side);

    if (image_.isNull()) {
        p.setPen(palette().color(QPalette::PlaceholderText));
        p.drawText(rect(), Qt::AlignCenter, tr("No wafer map yet"));
        return;
    }

    p.drawImage(square, image_);

    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(palette().color(QPalette::WindowText), 1));
    p.drawEllipse(square);
    if (edge_ring_fraction_ > 0.0 && edge_ring_fraction_ < 1.0) {
        p.setPen(QPen(palette().color(QPalette::WindowText), 1, Qt::DashLine));
        const QPointF c = square.center();
        const double r = square.width() / 2.0 * edge_ring_fraction_;
        p.drawEllipse(c, r, r);
    }

    // Colour bar with the height range in micrometres.
    const QRect bar(square.right() + margin * 2, square.top(), bar_w, square.height());
    for (int y = 0; y < bar.height(); ++y) {
        const double t = 1.0 - static_cast<double>(y) / std::max(1, bar.height() - 1);
        p.setPen(ramp(t));
        p.drawLine(bar.left(), bar.top() + y, bar.right(), bar.top() + y);
    }
    p.setPen(palette().color(QPalette::WindowText));
    p.drawRect(bar);
    p.drawText(QRect(bar.right() + 4, bar.top() - 2, label_w, 16), Qt::AlignLeft,
               QStringLiteral("%1 um").arg(max_um_, 0, 'f', 1));
    p.drawText(QRect(bar.right() + 4, bar.bottom() - 12, label_w, 16), Qt::AlignLeft,
               QStringLiteral("%1 um").arg(min_um_, 0, 'f', 1));
}

}  // namespace ssim::qt
