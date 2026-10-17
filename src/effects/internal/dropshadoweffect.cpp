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

#include "dropshadoweffect.h"

#include <QtMath>

DropShadowEffect::DropShadowEffect(Clip* c, const EffectMeta* em) : Effect(c, em) {
  // No flags: process_effect() skips it, render_and_blend_clip() draws it

  EffectRow* color_row = new EffectRow(this, tr("Color"));
  color_field = new ColorField(color_row, "color");
  color_field->SetValueAt(0, QColor(Qt::black));

  EffectRow* opacity_row = new EffectRow(this, tr("Opacity"));
  opacity_field = new DoubleField(opacity_row, "opacity");
  opacity_field->SetMinimum(0);
  opacity_field->SetMaximum(100);
  opacity_field->SetDefault(50);

  // Degrees clockwise from straight up, the direction the shadow is cast (After Effects / Premiere convention)
  EffectRow* direction_row = new EffectRow(this, tr("Direction"));
  direction_field = new DoubleField(direction_row, "direction");
  direction_field->SetDefault(135);

  EffectRow* distance_row = new EffectRow(this, tr("Distance"));
  distance_field = new DoubleField(distance_row, "distance");
  distance_field->SetMinimum(0);
  distance_field->SetMaximum(1000);
  distance_field->SetDefault(10);

  EffectRow* softness_row = new EffectRow(this, tr("Softness"));
  softness_field = new DoubleField(softness_row, "softness");
  softness_field->SetMinimum(0);
  softness_field->SetMaximum(100);
  softness_field->SetDefault(10);
}

DropShadowEffect::Shadow DropShadowEffect::shadow_at(double timecode) {
  Shadow s;
  s.color = color_field->GetColorAt(timecode);
  s.opacity = qBound(0.0, opacity_field->GetDoubleAt(timecode) * 0.01, 1.0);
  const double theta = qDegreesToRadians(direction_field->GetDoubleAt(timecode));
  const double distance = qMax(0.0, distance_field->GetDoubleAt(timecode));
  s.dx = distance * qSin(theta);
  s.dy = -distance * qCos(theta);
  s.softness = qBound(0.0, softness_field->GetDoubleAt(timecode), 100.0);
  return s;
}
