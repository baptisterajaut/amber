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

#include "timereffect.h"

#include <QFontDatabase>
#include <QFontMetrics>
#include <QImage>
#include <QPainter>
#include <QPainterPath>
#include <QtMath>

#include "core/timerformat.h"
#include "engine/clip.h"
#include "engine/sequence.h"

TimerEffect::TimerEffect(Clip* c, const EffectMeta* em)
    : Effect(c, em), font(QFontDatabase::systemFont(QFontDatabase::FixedFont)) {
  SetFlags(Effect::SuperimposeFlag);

  EffectRow* mode_row = new EffectRow(this, tr("Mode"));
  mode_field = new ComboField(mode_row, "mode");
  mode_field->AddItem(tr("Count Up"), MODE_COUNT_UP);
  mode_field->AddItem(tr("Count Down"), MODE_COUNT_DOWN);
  mode_field->SetValueAt(0, MODE_COUNT_UP);

  // Seconds added to the time shown (a countdown ends on this value)
  EffectRow* offset_row = new EffectRow(this, tr("Time Offset"));
  offset_field = new DoubleField(offset_row, "offset");
  offset_field->SetMinimum(-86400);
  offset_field->SetDefault(0);
  offset_field->SetMaximum(86400);

  EffectRow* format_row = new EffectRow(this, tr("Format"));
  format_field = new ComboField(format_row, "format");
  format_field->AddItem(tr("MM:SS"), int(amber::TimerFormat::MMSS));
  format_field->AddItem(tr("HH:MM:SS"), int(amber::TimerFormat::HHMMSS));
  format_field->AddItem(tr("Seconds"), int(amber::TimerFormat::Seconds));
  format_field->AddItem(tr("MM:SS.cc"), int(amber::TimerFormat::MMSSHundredths));
  format_field->SetValueAt(0, int(amber::TimerFormat::MMSS));

  EffectRow* prefix_row = new EffectRow(this, tr("Prefix"));
  prefix_field = new StringField(prefix_row, "prefix", false);
  prefix_field->SetColumnSpan(2);

  EffectRow* scale_row = new EffectRow(this, tr("Size"));
  scale_field = new DoubleField(scale_row, "scale");
  scale_field->SetColumnSpan(2);
  scale_field->SetMinimum(1);
  scale_field->SetDefault(100);
  scale_field->SetMaximum(1000);

  EffectRow* color_row = new EffectRow(this, tr("Color"));
  color_field = new ColorField(color_row, "color");
  color_field->SetColumnSpan(2);
  color_field->SetValueAt(0, QColor(Qt::white));

  EffectRow* bg_color_row = new EffectRow(this, tr("Background Color"));
  bg_color_field = new ColorField(bg_color_row, "bgcolor");
  bg_color_field->SetColumnSpan(2);
  bg_color_field->SetValueAt(0, QColor(Qt::black));

  EffectRow* bg_alpha_row = new EffectRow(this, tr("Background Opacity"));
  bg_alpha_field = new DoubleField(bg_alpha_row, "bgalpha");
  bg_alpha_field->SetColumnSpan(2);
  bg_alpha_field->SetMinimum(0);
  bg_alpha_field->SetDefault(50);
  bg_alpha_field->SetMaximum(100);

  EffectRow* position_offset_row = new EffectRow(this, tr("Offset"));
  offset_x_field = new DoubleField(position_offset_row, "offsetx");
  offset_y_field = new DoubleField(position_offset_row, "offsety");
}

void TimerEffect::redraw(double timecode) {
  const double fps = parent_clip->sequence != nullptr ? parent_clip->sequence->frame_rate : 0;
  const long last = qMax(0L, parent_clip->length() - 1);
  const long elapsed = amber::clip_elapsed_frames(timecode, fps, parent_clip->clip_in(), last);
  const bool countdown = mode_field->GetValueAt(timecode).toInt() == MODE_COUNT_DOWN;
  const double seconds = amber::timer_seconds(countdown, elapsed, last, fps, offset_field->GetDoubleAt(timecode));
  const auto format = amber::TimerFormat(format_field->GetValueAt(timecode).toInt());
  const QString text = prefix_field->GetStringAt(timecode) + amber::format_timer(seconds, format, countdown);

  img.fill(Qt::transparent);

  QPainter p(&img);
  p.setRenderHint(QPainter::Antialiasing);
  const int width = img.width();
  const int height = img.height();

  // Same size, placement and background box as the Timecode effect
  font.setPixelSize(qMax(1, qCeil(scale_field->GetDoubleAt(timecode) * .01 * (height / 10))));
  p.setFont(font);
  QFontMetrics fm(font);

  const int text_height = fm.height();
  const int text_width = fm.horizontalAdvance(text);
  QColor background_color = bg_color_field->GetColorAt(timecode);
  background_color.setAlpha(qCeil(bg_alpha_field->GetDoubleAt(timecode) * 2.55));

  const int text_x = int(offset_x_field->GetDoubleAt(timecode)) + (width / 2) - (text_width / 2);
  const int text_y = int(offset_y_field->GetDoubleAt(timecode)) + height - height / 10;
  const int rect_y = text_y + fm.descent() - text_height;

  QPainterPath path;
  path.addText(text_x, text_y, font, text);

  p.setPen(Qt::NoPen);
  p.setBrush(background_color);
  p.drawRect(QRect(text_x - fm.descent(), rect_y, text_width + fm.descent() * 2, text_height));
  p.setBrush(color_field->GetColorAt(timecode));
  p.drawPath(path);
}
