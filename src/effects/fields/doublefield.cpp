#include "doublefield.h"

#include "effects/effectrow.h"

DoubleField::DoubleField(EffectRow *parent, const QString &id)
    : EffectField(parent, id, EFFECT_FIELD_DOUBLE),
      min_(qSNaN()),
      max_(qSNaN())

{
  connect(this, &DoubleField::Changed, this, &DoubleField::ValueHasBeenSet, Qt::DirectConnection);
}

double DoubleField::GetDoubleAt(double timecode) { return GetValueAt(timecode).toDouble(); }

void DoubleField::SetMinimum(double minimum) {
  min_ = minimum;
  emit MinimumChanged(min_);
}

void DoubleField::SetMaximum(double maximum) {
  max_ = maximum;
  emit MaximumChanged(max_);
}

void DoubleField::SetDefault(double d) {
  default_ = d;
  SetDefaultData(d);

  if (!value_set_) {
    SetValueAt(0, d);
  }
}

void DoubleField::SetDisplayType(amber::DisplayType type) { display_type_ = type; }

void DoubleField::SetFrameRate(const double &rate) { frame_rate_ = rate; }

QVariant DoubleField::ConvertStringToValue(const QString &s) { return s.toDouble(); }

QString DoubleField::ConvertValueToString(const QVariant &v) { return QString::number(v.toDouble()); }

void DoubleField::ValueHasBeenSet() { value_set_ = true; }
