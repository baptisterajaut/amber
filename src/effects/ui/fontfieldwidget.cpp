#include "fontfieldwidget.h"

#include <QDebug>

#include "effects/fields/fontfield.h"
#include "effects/effectrow.h"
#include "effects/keyframe.h"
#include "engine/undo/undo.h"
#include "engine/undo/undostack.h"
#include "ui/comboboxex.h"

FontFieldWidget::FontFieldWidget(EffectField* field, QObject* parent)
    : EffectFieldWidget(field, parent) {}

QWidget* FontFieldWidget::CreateWidget(QWidget* existing) {
  FontField* ff = static_cast<FontField*>(field_);
  ComboBoxEx* fcb;

  if (existing == nullptr) {
    fcb = new ComboBoxEx();
    fcb->setScrollingEnabled(false);
    fcb->addItems(ff->GetFontList());
  } else {
    fcb = static_cast<ComboBoxEx*>(existing);
  }

  connect(fcb, &QComboBox::currentTextChanged, this, &FontFieldWidget::UpdateFromWidget);
  connect(ff, &EffectField::EnabledChanged, fcb, &QWidget::setEnabled);

  return fcb;
}

void FontFieldWidget::UpdateWidgetValue(QWidget* widget, double timecode) {
  FontField* ff = static_cast<FontField*>(field_);
  QVariant data = ff->GetValueAt(timecode);

  ComboBoxEx* cb = static_cast<ComboBoxEx*>(widget);

  const QStringList& fonts = ff->GetFontList();
  for (int i = 0; i < fonts.size(); i++) {
    if (fonts.at(i) == data) {
      cb->blockSignals(true);
      cb->setCurrentIndex(i);
      cb->blockSignals(false);
      return;
    }
  }

  qWarning() << "Failed to set FontField value from data";
}

void FontFieldWidget::UpdateFromWidget(const QString& s) {
  FontField* ff = static_cast<FontField*>(field_);
  KeyframeDataChange* kdc = new KeyframeDataChange(ff);

  ff->SetValueAt(ff->Now(), s);

  kdc->SetNewKeyframes();
  kdc->setText(QObject::tr("Change Font"));
  amber::UndoStack.push(kdc);
}
