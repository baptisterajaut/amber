#include "fontfield.h"

#include <QFontDatabase>

// NOTE/TODO: This shares a lot of similarity with ComboField, and could probably be a derived class of it

FontField::FontField(EffectRow* parent, const QString &id) :
  EffectField(parent, id, EFFECT_FIELD_FONT)
{
  font_list = QFontDatabase::families();

  SetValueAt(0, font_list.first());
}

QString FontField::GetFontAt(double timecode)
{
  return GetValueAt(timecode).toString();
}

