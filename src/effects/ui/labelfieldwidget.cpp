#include "labelfieldwidget.h"

#include <QLabel>

#include "effects/fields/labelfield.h"

LabelFieldWidget::LabelFieldWidget(EffectField* field, QObject* parent)
    : EffectFieldWidget(field, parent) {}

QWidget* LabelFieldWidget::CreateWidget(QWidget* existing) {
  LabelField* lf = static_cast<LabelField*>(field_);
  QLabel* label;

  if (existing == nullptr) {
    label = new QLabel(lf->GetLabelText());
    label->setEnabled(lf->IsEnabled());
  } else {
    label = static_cast<QLabel*>(existing);
  }

  connect(lf, &EffectField::EnabledChanged, label, &QWidget::setEnabled);

  return label;
}

void LabelFieldWidget::UpdateWidgetValue(QWidget*, double) {
  // LabelField is UI-only, no value to update
}
