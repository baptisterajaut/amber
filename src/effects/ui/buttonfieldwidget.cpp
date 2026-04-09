#include "buttonfieldwidget.h"

#include <QPushButton>

#include "effects/fields/buttonfield.h"

ButtonFieldWidget::ButtonFieldWidget(EffectField* field, QObject* parent)
    : EffectFieldWidget(field, parent) {}

QWidget* ButtonFieldWidget::CreateWidget(QWidget* existing) {
  ButtonField* bf = static_cast<ButtonField*>(field_);
  QPushButton* button;

  if (existing == nullptr) {
    button = new QPushButton();
    button->setCheckable(bf->IsCheckable());
    button->setEnabled(bf->IsEnabled());
    button->setText(bf->GetButtonText());
  } else {
    button = static_cast<QPushButton*>(existing);
  }

  connect(bf, &ButtonField::CheckedChanged, button, &QPushButton::setChecked);
  connect(bf, &EffectField::EnabledChanged, button, &QWidget::setEnabled);
  connect(button, &QPushButton::clicked, bf, &EffectField::Clicked);
  connect(button, &QPushButton::toggled, bf, &ButtonField::SetChecked);
  connect(button, &QPushButton::toggled, bf, &ButtonField::Toggled);

  return button;
}

void ButtonFieldWidget::UpdateWidgetValue(QWidget*, double) {
  // ButtonField is UI-only, no value to update
}
