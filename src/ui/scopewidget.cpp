/***

    Olive - Non-Linear Video Editor
    Copyright (C) 2019  Olive Team

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.

***/

#include "scopewidget.h"

#include <QPainter>
#include <QPainterPath>

#include <algorithm>
#include <cmath>

using namespace amber::scopes;

namespace {

const QColor kGraticule(128, 128, 128, 140);
const QColor kLabel(150, 150, 150);
constexpr int kParadeGap = 2;  // px between the R, G and B thirds

// Counts -> image at the grid's native size, value 0 on the bottom row. Intensity is log1p(count) / log1p(max):
// linear counts leave everything but the most common level invisible.
QImage counts_image(const uint32_t* counts, int columns, uint32_t max_count, QColor tint) {
  QImage img(columns, 256, QImage::Format_RGB32);
  img.fill(Qt::black);
  if (max_count == 0) return img;
  const double scale = 1.0 / std::log1p(double(max_count));
  for (int v = 0; v < 256; v++) {
    QRgb* line = reinterpret_cast<QRgb*>(img.scanLine(255 - v));
    const uint32_t* row = counts + size_t(v) * columns;
    for (int x = 0; x < columns; x++) {
      if (row[x] == 0) continue;
      const double k = std::log1p(double(row[x])) * scale;
      line[x] = qRgb(int(tint.red() * k), int(tint.green() * k), int(tint.blue() * k));
    }
  }
  return img;
}

uint32_t max_of(const std::vector<uint32_t>& v) { return v.empty() ? 0 : *std::max_element(v.begin(), v.end()); }

// 0, 25, 50, 75, 100% lines across `plot` (horizontal = value on the y axis, vertical = value on the x axis),
// labelled outside the plot: on the left, or under it.
void draw_level_graticule(QPainter& p, const QRectF& plot, bool vertical) {
  const QFontMetrics fm = p.fontMetrics();
  for (int i = 0; i <= 4; i++) {
    const QString label = QString::number(i * 25) + QLatin1Char('%');
    p.setPen(kGraticule);
    if (vertical) {
      const double x = plot.left() + plot.width() * i / 4.0;
      p.drawLine(QPointF(x, plot.top()), QPointF(x, plot.bottom()));
      const double w = fm.horizontalAdvance(label);
      const double lx = std::clamp(x - w / 2, 0.0, plot.right() - w);
      p.setPen(kLabel);
      p.drawText(QPointF(lx, plot.bottom() + 2 + fm.ascent()), label);
    } else {
      const double y = plot.bottom() - plot.height() * i / 4.0;
      p.drawLine(QPointF(plot.left(), y), QPointF(plot.right(), y));
      p.setPen(kLabel);
      p.drawText(QRectF(0, y - fm.height() / 2.0, plot.left() - 4, fm.height()), Qt::AlignRight | Qt::AlignVCenter,
                 label);
    }
  }
}

}  // namespace

ScopeWidget::ScopeWidget(QWidget* parent) : QWidget(parent) {
  setAttribute(Qt::WA_OpaquePaintEvent);
  setMinimumSize(120, 80);
}

void ScopeWidget::setMode(ScopeMode mode) {
  if (mode == mode_) return;
  mode_ = mode;
  rebuild();
  update();
}

void ScopeWidget::setFrame(Sample sample) {
  sample_ = std::move(sample);
  rebuild();
  update();
}

void ScopeWidget::rebuild() {
  for (QImage& img : images_) img = QImage();
  histogram_ = Histogram();
  if (sample_.empty()) return;

  const uchar* px = sample_.data.data();
  const int w = sample_.w, h = sample_.h, stride = w * 4;
  switch (mode_) {
    case ScopeMode::LumaWaveform: {
      WaveformGrid g = luma_waveform(px, w, h, stride);
      images_[0] = counts_image(g.counts.data(), g.columns, max_of(g.counts), QColor(235, 235, 235));
      break;
    }
    case ScopeMode::RgbParade: {
      std::array<WaveformGrid, 3> g = rgb_parade(px, w, h, stride);
      // One scale for the three thirds so their brightness compares
      const uint32_t m = std::max({max_of(g[0].counts), max_of(g[1].counts), max_of(g[2].counts)});
      const QColor tints[3] = {QColor(255, 60, 60), QColor(60, 255, 60), QColor(80, 110, 255)};
      for (int c = 0; c < 3; c++) images_[c] = counts_image(g[c].counts.data(), g[c].columns, m, tints[c]);
      break;
    }
    case ScopeMode::Histogram:
      histogram_ = histogram(px, w, h, stride);
      break;
    case ScopeMode::Vectorscope: {
      Vectorscope v = vectorscope(px, w, h, stride);
      images_[0] = counts_image(v.counts.data(), 256, max_of(v.counts), QColor(235, 235, 235));
      break;
    }
  }
}

void ScopeWidget::paintEvent(QPaintEvent*) {
  QPainter p(this);
  p.fillRect(rect(), Qt::black);
  p.setRenderHint(QPainter::Antialiasing);
  p.setRenderHint(QPainter::SmoothPixmapTransform);
  QFont f = font();
  f.setPointSizeF(f.pointSizeF() * 0.85);
  p.setFont(f);
  const QFontMetrics fm = p.fontMetrics();
  const double half_line = fm.height() / 2.0 + 2;

  if (mode_ == ScopeMode::Vectorscope) {
    const double side = std::max(10.0, std::min(width(), height()) - 2 * half_line);
    const QRectF sq((width() - side) / 2, (height() - side) / 2, side, side);
    const QPointF c = sq.center();
    if (!images_[0].isNull()) p.drawImage(sq, images_[0]);

    p.setPen(kGraticule);
    p.setBrush(Qt::NoBrush);
    p.drawEllipse(c, side / 2, side / 2);
    p.drawLine(QPointF(sq.left(), c.y()), QPointF(sq.right(), c.y()));
    p.drawLine(QPointF(c.x(), sq.top()), QPointF(c.x(), sq.bottom()));

    // Skin-tone line, 123 degrees counter-clockwise from +Cb (Cr up)
    const double skin = 123.0 * M_PI / 180.0;
    p.drawLine(c, c + QPointF(std::cos(skin), -std::sin(skin)) * (side / 2));

    // Targets of the 75% colour bars: 8 x 8 grid pixels around each primary/secondary's (Cb, Cr) * 0.75
    struct Target {
      double r, g, b;
      const char* name;
    };
    static const Target targets[] = {{1, 0, 0, QT_TR_NOOP("R")}, {1, 0, 1, QT_TR_NOOP("Mg")},
                                     {0, 0, 1, QT_TR_NOOP("B")}, {0, 1, 1, QT_TR_NOOP("Cy")},
                                     {0, 1, 0, QT_TR_NOOP("G")}, {1, 1, 0, QT_TR_NOOP("Yl")}};
    const double box = side * 8 / 256;
    for (const Target& t : targets) {
      double cb, cr;
      cb_cr(t.r, t.g, t.b, &cb, &cr);
      const QPointF dir(cb * 0.75, -cr * 0.75);  // screen space, unit = side
      const QPointF pt = c + dir * side;
      p.setPen(kGraticule);
      p.drawRect(QRectF(pt.x() - box / 2, pt.y() - box / 2, box, box));
      // Label just outside the box, away from the centre
      const double len = std::hypot(dir.x(), dir.y());
      const QPointF out = pt + dir / len * (box / 2 + fm.height() * 0.7);
      const QString label = tr(t.name);
      p.setPen(kLabel);
      p.drawText(QRectF(out.x() - 20, out.y() - fm.height() / 2.0, 40, fm.height()), Qt::AlignCenter, label);
    }
    return;
  }

  if (mode_ == ScopeMode::Histogram) {
    const QRectF plot(4, half_line, width() - 8, height() - half_line - fm.height() - 4);
    const auto& r = histogram_.r;
    const auto& g = histogram_.g;
    const auto& b = histogram_.b;
    const auto& y = histogram_.luma;
    // Scale to the tallest bin, ignoring clipped blacks and whites (0 and 255), which would flatten the rest;
    // those still draw, capped at full height.
    uint32_t top = 0;
    for (int i = 1; i < 255; i++) top = std::max({top, r[i], g[i], b[i], y[i]});
    if (top == 0) {
      for (int i : {0, 255}) top = std::max({top, r[i], g[i], b[i], y[i]});
    }
    if (top > 0) {
      auto curve = [&](const std::array<uint32_t, 256>& bins, bool closed) {
        QPainterPath path;
        if (closed) path.moveTo(plot.left(), plot.bottom());
        for (int i = 0; i < 256; i++) {
          const double x = plot.left() + plot.width() * (i + 0.5) / 256;
          const double yy = plot.bottom() - plot.height() * std::min(1.0, double(bins[i]) / top);
          if (i == 0 && !closed)
            path.moveTo(x, yy);
          else
            path.lineTo(x, yy);
        }
        if (closed) {
          path.lineTo(plot.right(), plot.bottom());
          path.closeSubpath();
        }
        return path;
      };
      p.setPen(Qt::NoPen);
      p.setCompositionMode(QPainter::CompositionMode_Plus);
      p.setBrush(QColor(200, 0, 0));
      p.drawPath(curve(r, true));
      p.setBrush(QColor(0, 200, 0));
      p.drawPath(curve(g, true));
      p.setBrush(QColor(0, 0, 200));
      p.drawPath(curve(b, true));
      p.setCompositionMode(QPainter::CompositionMode_SourceOver);
      p.setBrush(Qt::NoBrush);
      p.setPen(QPen(Qt::white, 1.2));
      p.drawPath(curve(y, false));
    }
    // The x axis is the level here: vertical lines, labels under the plot
    draw_level_graticule(p, plot, true);
    return;
  }

  // Waveforms: value up, labels on the left
  const double label_w = fm.horizontalAdvance(QStringLiteral("100%")) + 8;
  const QRectF plot(label_w, half_line, width() - label_w - 4, height() - 2 * half_line);
  if (mode_ == ScopeMode::LumaWaveform) {
    if (!images_[0].isNull()) p.drawImage(plot, images_[0]);
  } else {
    const double third = (plot.width() - 2 * kParadeGap) / 3;
    for (int c = 0; c < 3; c++) {
      if (images_[c].isNull()) continue;
      p.drawImage(QRectF(plot.left() + c * (third + kParadeGap), plot.top(), third, plot.height()), images_[c]);
    }
  }
  draw_level_graticule(p, plot, false);
}
