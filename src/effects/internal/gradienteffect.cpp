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

#include "gradienteffect.h"

#include <QImage>
#include <QLinearGradient>
#include <QPainter>
#include <QRadialGradient>
#include <QtMath>

GradientEffect::GradientEffect(Clip* c, const EffectMeta* em) : Effect(c, em) {
  SetFlags(Effect::SuperimposeFlag);

  EffectRow* type_row = new EffectRow(this, tr("Type"));
  type_field = new ComboField(type_row, "type");
  type_field->AddItem(tr("Linear"), GRADIENT_TYPE_LINEAR);
  type_field->AddItem(tr("Radial"), GRADIENT_TYPE_RADIAL);
  type_field->SetValueAt(0, GRADIENT_TYPE_LINEAR);

  EffectRow* start_row = new EffectRow(this, tr("Start Color"));
  start_color_field = new ColorField(start_row, "start_color");
  start_color_field->SetValueAt(0, QColor(Qt::black));

  EffectRow* end_row = new EffectRow(this, tr("End Color"));
  end_color_field = new ColorField(end_row, "end_color");
  end_color_field->SetValueAt(0, QColor(Qt::white));

  EffectRow* angle_row = new EffectRow(this, tr("Angle"));
  angle_field = new DoubleField(angle_row, "angle");
  angle_field->SetMinimum(-180);
  angle_field->SetDefault(0);
  angle_field->SetMaximum(180);

  EffectRow* cx_row = new EffectRow(this, tr("Center X"));
  center_x_field = new DoubleField(cx_row, "center_x");
  center_x_field->SetMinimum(0);
  center_x_field->SetDefault(50);
  center_x_field->SetMaximum(100);

  EffectRow* cy_row = new EffectRow(this, tr("Center Y"));
  center_y_field = new DoubleField(cy_row, "center_y");
  center_y_field->SetMinimum(0);
  center_y_field->SetDefault(50);
  center_y_field->SetMaximum(100);

  EffectRow* radius_row = new EffectRow(this, tr("Radius"));
  radius_field = new DoubleField(radius_row, "radius");
  radius_field->SetMinimum(5);
  radius_field->SetDefault(50);
  radius_field->SetMaximum(150);
}

void GradientEffect::redraw(double timecode) {
  const int w = img.width();
  const int h = img.height();
  img.fill(Qt::transparent);

  QPainter p(&img);

  const QColor c0 = start_color_field->GetColorAt(timecode);
  const QColor c1 = end_color_field->GetColorAt(timecode);

  if (type_field->GetValueAt(timecode).toInt() == GRADIENT_TYPE_LINEAR) {
    const double angle_deg = angle_field->GetDoubleAt(timecode);
    const double rad = qDegreesToRadians(angle_deg);
    const double dx = qCos(rad);
    const double dy = qSin(rad);
    const QPointF center(w / 2.0, h / 2.0);
    const double half_diag = 0.5 * qSqrt(double(w) * w + double(h) * h);
    QLinearGradient g(center - QPointF(dx, dy) * half_diag, center + QPointF(dx, dy) * half_diag);
    g.setColorAt(0.0, c0);
    g.setColorAt(1.0, c1);
    p.fillRect(0, 0, w, h, g);
  } else {
    const double cx = center_x_field->GetDoubleAt(timecode) / 100.0 * w;
    const double cy = center_y_field->GetDoubleAt(timecode) / 100.0 * h;
    const double r = radius_field->GetDoubleAt(timecode) / 100.0 * qMax(w, h);
    QRadialGradient g(QPointF(cx, cy), r);
    g.setColorAt(0.0, c0);
    g.setColorAt(1.0, c1);
    p.fillRect(0, 0, w, h, g);
  }
}
