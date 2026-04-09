#include "effectfieldwidget.h"

#include "effects/effectfield.h"

// Factory includes -- all subclass headers now exist
#include "effects/ui/boolfieldwidget.h"
#include "effects/ui/buttonfieldwidget.h"
#include "effects/ui/colorfieldwidget.h"
#include "effects/ui/combofieldwidget.h"
#include "effects/ui/doublefieldwidget.h"
#include "effects/ui/filefieldwidget.h"
#include "effects/ui/fontfieldwidget.h"
#include "effects/ui/labelfieldwidget.h"
#include "effects/ui/stringfieldwidget.h"

#include "effects/fields/buttonfield.h"
#include "effects/fields/labelfield.h"

EffectFieldWidget::EffectFieldWidget(EffectField* field, QObject* parent)
    : QObject(parent), field_(field) {}

EffectField* EffectFieldWidget::GetField() const {
  return field_;
}

EffectFieldWidget* EffectFieldWidget::Create(EffectField* field, QObject* parent) {
  switch (field->type()) {
    case EffectField::EFFECT_FIELD_BOOL:
      return new BoolFieldWidget(field, parent);
    case EffectField::EFFECT_FIELD_DOUBLE:
      return new DoubleFieldWidget(field, parent);
    case EffectField::EFFECT_FIELD_COLOR:
      return new ColorFieldWidget(field, parent);
    case EffectField::EFFECT_FIELD_COMBO:
      return new ComboFieldWidget(field, parent);
    case EffectField::EFFECT_FIELD_STRING:
      return new StringFieldWidget(field, parent);
    case EffectField::EFFECT_FIELD_FONT:
      return new FontFieldWidget(field, parent);
    case EffectField::EFFECT_FIELD_FILE:
      return new FileFieldWidget(field, parent);
    case EffectField::EFFECT_FIELD_UI:
      // ButtonField and LabelField both use EFFECT_FIELD_UI.
      // Distinguish via dynamic_cast.
      if (dynamic_cast<ButtonField*>(field)) {
        return new ButtonFieldWidget(field, parent);
      } else if (dynamic_cast<LabelField*>(field)) {
        return new LabelFieldWidget(field, parent);
      }
      return nullptr;
    default:
      return nullptr;
  }
}
