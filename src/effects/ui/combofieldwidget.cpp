#include "combofieldwidget.h"

#include <QDebug>

#include "effects/fields/combofield.h"
#include "effects/effectrow.h"
#include "effects/keyframe.h"
#include "engine/undo/undo.h"
#include "engine/undo/undostack.h"
#include "ui/comboboxex.h"

ComboFieldWidget::ComboFieldWidget(EffectField* field, QObject* parent)
    : EffectFieldWidget(field, parent) {}

QWidget* ComboFieldWidget::CreateWidget(QWidget* existing) {
  ComboField* cf = static_cast<ComboField*>(field_);
  ComboBoxEx* cb;

  if (existing == nullptr) {
    cb = new ComboBoxEx();
    cb->setScrollingEnabled(false);

    for (int i = 0; i < cf->ItemCount(); i++) {
      cb->addItem(cf->ItemName(i));
    }
  } else {
    cb = static_cast<ComboBoxEx*>(existing);
  }

  connect(cb, qOverload<int>(&QComboBox::activated), this, &ComboFieldWidget::UpdateFromWidget);
  connect(cf, &EffectField::EnabledChanged, cb, &QWidget::setEnabled);

  return cb;
}

void ComboFieldWidget::UpdateWidgetValue(QWidget* widget, double timecode) {
  ComboField* cf = static_cast<ComboField*>(field_);
  QVariant data = cf->GetValueAt(timecode);

  ComboBoxEx* cb = static_cast<ComboBoxEx*>(widget);

  for (int i = 0; i < cf->ItemCount(); i++) {
    if (cf->ItemData(i) == data) {
      cb->blockSignals(true);
      cb->setCurrentIndex(i);
      cb->blockSignals(false);

      emit cf->DataChanged(data);
      return;
    }
  }

  qWarning() << "Failed to set ComboField value from data";
}

void ComboFieldWidget::UpdateFromWidget(int index) {
  ComboField* cf = static_cast<ComboField*>(field_);
  KeyframeDataChange* kdc = new KeyframeDataChange(cf);

  cf->SetValueAt(cf->Now(), cf->ItemData(index));

  kdc->SetNewKeyframes();
  kdc->setText(QObject::tr("Change Value"));
  amber::UndoStack.push(kdc);
}
