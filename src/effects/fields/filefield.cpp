#include "filefield.h"

FileField::FileField(EffectRow* parent, const QString &id) :
  EffectField(parent, id, EFFECT_FIELD_FILE)
{
  // Set default value to an empty string
  SetValueAt(0, "");
}

QString FileField::GetFileAt(double timecode)
{
  return GetValueAt(timecode).toString();
}
