#include "stringfield.h"

StringField::StringField(EffectRow* parent, const QString& id, bool rich_text) :
  EffectField(parent, id, EFFECT_FIELD_STRING),
  rich_text_(rich_text)
{
  // Set default value to an empty string
  SetValueAt(0, "");
}

QString StringField::GetStringAt(double timecode)
{
  return GetValueAt(timecode).toString();
}

