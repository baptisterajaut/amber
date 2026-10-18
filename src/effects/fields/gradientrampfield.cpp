#include "gradientrampfield.h"

#include "colorfield.h"

GradientRampField::GradientRampField(EffectRow* parent, const QString& id)
    : EffectField(parent, id, EFFECT_FIELD_GRADIENT_RAMP) {
  SetValueAt(0, QString());
  SetDefaultData(QString());
}

QVariant GradientRampField::ConvertStringToValue(const QString& s) { return amber::gradientramp::canonical(s); }

void GradientRampField::SetEndFields(ColorField* start, ColorField* end) {
  start_ = start;
  end_ = end;
}

amber::gradientramp::Ramp GradientRampField::GetRampAt(double timecode) {
  return amber::gradientramp::parse(GetValueAt(timecode).toString());
}
