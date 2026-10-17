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

#ifndef PROGRESSBAREFFECT_H
#define PROGRESSBAREFFECT_H

#include "effects/effect.h"

// A bar along the top or bottom edge that fills over the clip's duration (or follows a keyframeable value).
class ProgressBarEffect : public Effect {
  Q_OBJECT
 public:
  // Combo data, saved in projects: append only
  enum Source { SOURCE_CLIP_DURATION, SOURCE_MANUAL };
  enum Position { POSITION_BOTTOM, POSITION_TOP };
  enum Direction { DIRECTION_LEFT_TO_RIGHT, DIRECTION_RIGHT_TO_LEFT };

  ProgressBarEffect(Clip* c, const EffectMeta* em);
  void redraw(double timecode) override;

 protected:
  // The fill follows the clip time, which changes without any field changing
  bool AlwaysUpdate() override { return true; }

 private:
  ComboField* source_field;
  DoubleField* progress_field;
  ComboField* position_field;
  DoubleField* thickness_field;
  DoubleField* margin_field;
  ComboField* direction_field;
  ColorField* color_field;
  ColorField* bg_color_field;
  DoubleField* bg_alpha_field;
};

#endif  // PROGRESSBAREFFECT_H
