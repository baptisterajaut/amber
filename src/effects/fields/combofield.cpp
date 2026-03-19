#include "combofield.h"

#include <QDebug>

#include "ui/comboboxex.h"

ComboField::ComboField(EffectRow* parent, const QString& id) :
  EffectField(parent, id, EFFECT_FIELD_COMBO)
{}

void ComboField::AddItem(const QString &text, const QVariant &data)
{
  ComboFieldItem item;

  item.name = text;
  item.data = data;

  items_.append(item);
}

QWidget *ComboField::CreateWidget(QWidget *existing)
{
  ComboBoxEx* cb;

  if (existing == nullptr) {
    cb = new ComboBoxEx();

    cb->setScrollingEnabled(false);

    for (const auto & item : items_) {
      cb->addItem(item.name);
    }
  } else {
    cb = static_cast<ComboBoxEx*>(existing);
  }

  connect(cb, qOverload<int>(&QComboBox::activated), this, &ComboField::UpdateFromWidget);
  connect(this, &EffectField::EnabledChanged, cb, &QWidget::setEnabled);

  return cb;
}

void ComboField::UpdateWidgetValue(QWidget *widget, double timecode)
{
  QVariant data = GetValueAt(timecode);

  ComboBoxEx* cb = static_cast<ComboBoxEx*>(widget);

  for (int i=0;i<items_.size();i++) {
    if (items_.at(i).data == data) {
      cb->blockSignals(true);
      cb->setCurrentIndex(i);
      cb->blockSignals(false);

      emit DataChanged(data);
      return;
    }
  }

  qWarning() << "Failed to set ComboField value from data";
}

void ComboField::UpdateFromWidget(int index)
{
  KeyframeDataChange* kdc = new KeyframeDataChange(this);

  SetValueAt(Now(), items_.at(index).data);

  kdc->SetNewKeyframes();
  kdc->setText(QObject::tr("Change Value"));
  amber::UndoStack.push(kdc);
}
