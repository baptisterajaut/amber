#include "buttonfield.h"

#include <QPushButton>

ButtonField::ButtonField(EffectRow *parent, const QString &string) :
  EffectField(parent, nullptr, EFFECT_FIELD_UI),
  button_text_(string)
{}

void ButtonField::SetCheckable(bool c)
{
  checkable_ = c;
}

void ButtonField::SetChecked(bool c)
{
  checked_ = c;
  emit CheckedChanged(c);
}

QWidget *ButtonField::CreateWidget(QWidget *existing)
{
  QPushButton* button;

  if (existing == nullptr) {

    button = new QPushButton();

    button->setCheckable(checkable_);
    button->setEnabled(IsEnabled());
    button->setText(button_text_);

  } else {

    button = static_cast<QPushButton*>(existing);

  }

  connect(this, &ButtonField::CheckedChanged, button, &QPushButton::setChecked);
  connect(this, &EffectField::EnabledChanged, button, &QWidget::setEnabled);
  connect(button, &QPushButton::clicked, this, &EffectField::Clicked);
  connect(button, &QPushButton::toggled, this, &ButtonField::SetChecked);
  connect(button, &QPushButton::toggled, this, &ButtonField::Toggled);

  return button;
}
