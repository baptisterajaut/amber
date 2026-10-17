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

#ifndef TIMEREFFECT_H
#define TIMEREFFECT_H

#include "effects/effect.h"

#include <QFont>

// Time elapsed since the clip's start (or left until its end), drawn like the Timecode effect.
class TimerEffect : public Effect {
  Q_OBJECT
 public:
  // Combo data, saved in projects: append only
  enum Mode { MODE_COUNT_UP, MODE_COUNT_DOWN };

  TimerEffect(Clip* c, const EffectMeta* em);
  void redraw(double timecode) override;

 protected:
  // The time shown follows the clip time, which changes without any field changing
  bool AlwaysUpdate() override { return true; }

 private:
  ComboField* mode_field;
  DoubleField* offset_field;
  ComboField* format_field;
  StringField* prefix_field;
  DoubleField* scale_field;
  ColorField* color_field;
  ColorField* bg_color_field;
  DoubleField* bg_alpha_field;
  DoubleField* offset_x_field;
  DoubleField* offset_y_field;

  // Fixed-width system font so the digits don't jitter; pixel size set per redraw
  QFont font;
};

#endif  // TIMEREFFECT_H
