#ifndef GRADIENTRAMPFIELD_H
#define GRADIENTRAMPFIELD_H

#include <QPointer>

#include "../effectfield.h"

#include "core/gradientramp.h"

class ColorField;

/**
 * @brief The GradientRampField class
 *
 * An EffectField derivative holding a colour ramp's intermediate stops as one canonical string (see
 * core/gradientramp.h), edited with a GradientRampEditor. The default is "" (no intermediate stop), so the row's reset
 * button clears the ramp.
 *
 * The ramp's two ends are two other fields of the effect (Start Color and End Color, keyframeable); SetEndFields()
 * names them so the editor can show them and edit them.
 *
 * Every string that comes in through ConvertStringToValue() (project load, paste) is parsed defensively and
 * re-serialised canonically, so comparing two values as strings is comparing the ramps.
 */
class GradientRampField : public EffectField {
  Q_OBJECT
 public:
  GradientRampField(EffectRow* parent, const QString& id);

  QVariant ConvertStringToValue(const QString& s) override;

  void SetEndFields(ColorField* start, ColorField* end);
  ColorField* StartField() const { return start_; }
  ColorField* EndField() const { return end_; }

  amber::gradientramp::Ramp GetRampAt(double timecode);

 private:
  QPointer<ColorField> start_;
  QPointer<ColorField> end_;
};

#endif  // GRADIENTRAMPFIELD_H
