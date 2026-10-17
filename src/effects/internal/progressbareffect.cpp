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

#include "progressbareffect.h"

#include <QImage>
#include <QPainter>
#include <QtMath>

#include "core/timerformat.h"
#include "engine/clip.h"
#include "engine/sequence.h"

ProgressBarEffect::ProgressBarEffect(Clip* c, const EffectMeta* em) : Effect(c, em) {
  SetFlags(Effect::SuperimposeFlag);

  EffectRow* source_row = new EffectRow(this, tr("Source"));
  source_field = new ComboField(source_row, "source");
  source_field->AddItem(tr("Clip Duration"), SOURCE_CLIP_DURATION);
  source_field->AddItem(tr("Manual"), SOURCE_MANUAL);
  source_field->SetValueAt(0, SOURCE_CLIP_DURATION);

  EffectRow* progress_row = new EffectRow(this, tr("Progress"));
  progress_field = new DoubleField(progress_row, "progress");
  progress_field->SetMinimum(0);
  progress_field->SetDefault(0);
  progress_field->SetMaximum(100);

  EffectRow* position_row = new EffectRow(this, tr("Position"));
  position_field = new ComboField(position_row, "position");
  position_field->AddItem(tr("Bottom"), POSITION_BOTTOM);
  position_field->AddItem(tr("Top"), POSITION_TOP);
  position_field->SetValueAt(0, POSITION_BOTTOM);

  // Thickness and margin are percentages of the frame height
  EffectRow* thickness_row = new EffectRow(this, tr("Thickness"));
  thickness_field = new DoubleField(thickness_row, "thickness");
  thickness_field->SetMinimum(0.1);
  thickness_field->SetDefault(2);
  thickness_field->SetMaximum(100);

  EffectRow* margin_row = new EffectRow(this, tr("Margin"));
  margin_field = new DoubleField(margin_row, "margin");
  margin_field->SetMinimum(0);
  margin_field->SetDefault(0);
  margin_field->SetMaximum(50);

  EffectRow* direction_row = new EffectRow(this, tr("Direction"));
  direction_field = new ComboField(direction_row, "direction");
  direction_field->AddItem(tr("Left to Right"), DIRECTION_LEFT_TO_RIGHT);
  direction_field->AddItem(tr("Right to Left"), DIRECTION_RIGHT_TO_LEFT);
  direction_field->SetValueAt(0, DIRECTION_LEFT_TO_RIGHT);

  EffectRow* color_row = new EffectRow(this, tr("Color"));
  color_field = new ColorField(color_row, "color");
  color_field->SetValueAt(0, QColor(Qt::white));

  EffectRow* bg_color_row = new EffectRow(this, tr("Background Color"));
  bg_color_field = new ColorField(bg_color_row, "bgcolor");
  bg_color_field->SetValueAt(0, QColor(Qt::black));

  EffectRow* bg_alpha_row = new EffectRow(this, tr("Background Opacity"));
  bg_alpha_field = new DoubleField(bg_alpha_row, "bgalpha");
  bg_alpha_field->SetMinimum(0);
  bg_alpha_field->SetDefault(50);
  bg_alpha_field->SetMaximum(100);
}

void ProgressBarEffect::redraw(double timecode) {
  const int w = img.width();
  const int h = img.height();
  img.fill(Qt::transparent);

  double fraction;
  if (source_field->GetValueAt(timecode).toInt() == SOURCE_MANUAL) {
    fraction = qBound(0.0, progress_field->GetDoubleAt(timecode) / 100.0, 1.0);
  } else {
    const double fps = parent_clip->sequence != nullptr ? parent_clip->sequence->frame_rate : 0;
    const long last = qMax(0L, parent_clip->length() - 1);
    fraction = amber::progress_fraction(amber::clip_elapsed_frames(timecode, fps, parent_clip->clip_in(), last), last);
  }

  const int thickness = qMax(1, qRound(h * thickness_field->GetDoubleAt(timecode) / 100.0));
  const int margin = qRound(h * margin_field->GetDoubleAt(timecode) / 100.0);
  const int y = (position_field->GetValueAt(timecode).toInt() == POSITION_TOP) ? margin : h - margin - thickness;

  QColor bg = bg_color_field->GetColorAt(timecode);
  bg.setAlpha(qCeil(bg_alpha_field->GetDoubleAt(timecode) * 2.55));

  const int fill = qRound(fraction * w);
  const int fill_x = (direction_field->GetValueAt(timecode).toInt() == DIRECTION_RIGHT_TO_LEFT) ? w - fill : 0;

  QPainter p(&img);
  p.fillRect(QRect(0, y, w, thickness), bg);
  p.fillRect(QRect(fill_x, y, fill, thickness), color_field->GetColorAt(timecode));
}
