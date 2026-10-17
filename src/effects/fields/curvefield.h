#ifndef CURVEFIELD_H
#define CURVEFIELD_H

#include "../effectfield.h"

#include "core/curves.h"

/**
 * @brief The CurveField class
 *
 * An EffectField derivative holding tone curves (RGB, Luma, R, G, B) as one canonical string (see core/curves.h),
 * edited with a CurveEditor. The default is the identity string, so the row's reset buttons work.
 *
 * Every string that comes in through ConvertStringToValue() (project load, paste) is parsed defensively and
 * re-serialised canonically, so comparing two values as strings is comparing the curves.
 */
class CurveField : public EffectField {
  Q_OBJECT
 public:
  CurveField(EffectRow* parent, const QString& id);

  QVariant ConvertStringToValue(const QString& s) override;

  amber::curves::CurveSet GetCurvesAt(double timecode);
};

#endif  // CURVEFIELD_H
