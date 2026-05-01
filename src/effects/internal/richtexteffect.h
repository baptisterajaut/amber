#ifndef RICHTEXTEFFECT_H
#define RICHTEXTEFFECT_H

#include "effects/effect.h"

class RichTextEffect : public Effect {
  Q_OBJECT
public:
  RichTextEffect(Clip* c, const EffectMeta *em);
  void redraw(double timecode) override;
protected:
  bool AlwaysUpdate() override;
private:
  StringField* text_val;
  DoubleField* padding_field;
  DoubleField* position_x;
  DoubleField* position_y;
  ComboField* vertical_align;
  ComboField* autoscroll;

  BoolField* shadow_bool;
  DoubleField* shadow_angle;
  DoubleField* shadow_distance;
  ColorField* shadow_color;
  DoubleField* shadow_softness;
  DoubleField* shadow_opacity;

  BoolField* stroke_bool;
  ColorField* stroke_color;
  DoubleField* stroke_width;
};

#endif // RICHTEXTEFFECT_H
