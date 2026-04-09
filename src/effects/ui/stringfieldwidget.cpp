#include "stringfieldwidget.h"

#include <QtMath>

#include "effects/fields/stringfield.h"
#include "effects/effectrow.h"
#include "effects/keyframe.h"
#include "engine/undo/undo.h"
#include "engine/undo/undostack.h"
#include "global/config.h"
#include "ui/texteditex.h"

StringFieldWidget::StringFieldWidget(EffectField* field, QObject* parent)
    : EffectFieldWidget(field, parent) {}

QWidget* StringFieldWidget::CreateWidget(QWidget* existing) {
  StringField* sf = static_cast<StringField*>(field_);
  TextEditEx* text_edit;

  if (existing == nullptr) {
    text_edit = new TextEditEx(nullptr, sf->IsRichText());
    text_edit->setEnabled(sf->IsEnabled());
    text_edit->setUndoRedoEnabled(true);
    text_edit->setTextHeight(qCeil(text_edit->fontMetrics().lineSpacing() * amber::CurrentConfig.effect_textbox_lines
                                   + text_edit->document()->documentMargin()
                                   + text_edit->document()->documentMargin() + 2));
  } else {
    text_edit = static_cast<TextEditEx*>(existing);
  }

  connect(text_edit, &TextEditEx::textModified, this, &StringFieldWidget::UpdateFromWidget);
  connect(sf, &EffectField::EnabledChanged, text_edit, &QWidget::setEnabled);

  return text_edit;
}

void StringFieldWidget::UpdateWidgetValue(QWidget* widget, double timecode) {
  StringField* sf = static_cast<StringField*>(field_);
  TextEditEx* text = static_cast<TextEditEx*>(widget);

  text->blockSignals(true);
  int pos = text->textCursor().position();

  if (sf->IsRichText()) {
    text->setHtml(sf->GetValueAt(timecode).toString());
  } else {
    text->setPlainText(sf->GetValueAt(timecode).toString());
  }

  QTextCursor new_cursor(text->document());
  new_cursor.setPosition(pos);
  text->setTextCursor(new_cursor);
  text->blockSignals(false);
}

void StringFieldWidget::UpdateFromWidget(const QString& s) {
  StringField* sf = static_cast<StringField*>(field_);
  KeyframeDataChange* kdc = new KeyframeDataChange(sf);

  sf->SetValueAt(sf->Now(), s);

  kdc->SetNewKeyframes();
  kdc->setText(QObject::tr("Change Value"));
  amber::UndoStack.push(kdc);
}
