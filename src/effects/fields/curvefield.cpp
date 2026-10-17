#include "curvefield.h"

CurveField::CurveField(EffectRow* parent, const QString& id) : EffectField(parent, id, EFFECT_FIELD_CURVE) {
  const QString identity = amber::curves::identity_string();
  SetValueAt(0, identity);
  SetDefaultData(identity);
}

QVariant CurveField::ConvertStringToValue(const QString& s) { return amber::curves::canonical(s); }

amber::curves::CurveSet CurveField::GetCurvesAt(double timecode) {
  return amber::curves::parse(GetValueAt(timecode).toString());
}
