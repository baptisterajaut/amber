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

#ifndef DROPSHADOWEFFECT_H
#define DROPSHADOWEFFECT_H

#include "effects/effect.h"

// Shadow cast by the clip as already transformed (works on opaque picture-in-picture clips). Does nothing in the
// effect chain: compose_sequence() looks it up and draws it in sequence space, between the clip's back-buffer render
// and its blend onto the sequence (render_and_blend_clip()).
class DropShadowEffect : public Effect {
  Q_OBJECT
 public:
  DropShadowEffect(Clip* c, const EffectMeta* em);

  // Values at `timecode`, in sequence pixels (the compositor scales them to the preview resolution)
  struct Shadow {
    QColor color;
    double opacity;  // 0..1
    double dx;       // offset to the right
    double dy;       // offset downwards (screen Y down)
    double softness;
  };
  Shadow shadow_at(double timecode);

 private:
  ColorField* color_field;
  DoubleField* opacity_field;
  DoubleField* direction_field;
  DoubleField* distance_field;
  DoubleField* softness_field;
};

#endif  // DROPSHADOWEFFECT_H
