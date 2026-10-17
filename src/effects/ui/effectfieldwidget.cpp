#include "effectfieldwidget.h"

EffectFieldWidget::EffectFieldWidget(EffectField* field, QObject* parent)
    : QObject(parent), field_(field) {}

EffectField* EffectFieldWidget::GetField() const {
  return field_;
}
