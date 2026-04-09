#include "boolfieldwidget.h"

#include <QCheckBox>

#include "effects/fields/boolfield.h"
#include "effects/effectrow.h"
#include "effects/keyframe.h"
#include "engine/undo/undo.h"
#include "engine/undo/undostack.h"

BoolFieldWidget::BoolFieldWidget(EffectField* field, QObject* parent)
    : EffectFieldWidget(field, parent) {}

QWidget* BoolFieldWidget::CreateWidget(QWidget* existing) {
  BoolField* bf = static_cast<BoolField*>(field_);
  QCheckBox* cb;

  if (existing == nullptr) {
    cb = new QCheckBox();
    cb->setEnabled(bf->IsEnabled());
  } else {
    cb = static_cast<QCheckBox*>(existing);
  }

  connect(cb, &QCheckBox::toggled, this, &BoolFieldWidget::UpdateFromWidget);
  connect(bf, &EffectField::EnabledChanged, cb, &QWidget::setEnabled);
  connect(cb, &QCheckBox::toggled, bf, &BoolField::Toggled);

  return cb;
}

void BoolFieldWidget::UpdateWidgetValue(QWidget* widget, double timecode) {
  BoolField* bf = static_cast<BoolField*>(field_);
  QCheckBox* cb = static_cast<QCheckBox*>(widget);

  cb->blockSignals(true);

  if (qIsNaN(timecode)) {
    cb->setTristate(true);
    cb->setCheckState(Qt::PartiallyChecked);
  } else {
    cb->setTristate(false);
    cb->setChecked(bf->GetBoolAt(timecode));
  }

  cb->blockSignals(false);

  emit bf->Toggled(cb->isChecked());
}

void BoolFieldWidget::UpdateFromWidget(bool b) {
  BoolField* bf = static_cast<BoolField*>(field_);
  KeyframeDataChange* kdc = new KeyframeDataChange(bf);

  bf->SetValueAt(bf->Now(), b);

  kdc->SetNewKeyframes();
  kdc->setText(QObject::tr("Change Value"));
  amber::UndoStack.push(kdc);
}
