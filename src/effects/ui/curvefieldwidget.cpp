#include "curvefieldwidget.h"

#include "effects/effectfield.h"
#include "engine/undo/undo.h"
#include "engine/undo/undo_effect.h"
#include "engine/undo/undostack.h"
#include "ui/curveeditor.h"

namespace {
// The wrapper driving an editor, so a field attached later (AddAdditionalEffect) joins it
const char* const kOwnerProperty = "amber_curve_field_widget";
}  // namespace

CurveFieldWidget::CurveFieldWidget(EffectField* field, QObject* parent) : EffectFieldWidget(field, parent) {}

CurveFieldWidget::~CurveFieldWidget() {
  // A gesture still open (the editor went first, or never got to end it): keep its edit undoable
  if (!changes_.isEmpty()) FinishEdit(tr("Change Curve"), true);
}

QWidget* CurveFieldWidget::CreateWidget(QWidget* existing) {
  if (existing != nullptr) {
    CurveFieldWidget* owner = qobject_cast<CurveFieldWidget*>(existing->property(kOwnerProperty).value<QObject*>());
    if (owner != nullptr && !owner->fields_.contains(field_)) owner->fields_.append(field_);
    connect(field_, &EffectField::EnabledChanged, existing, &QWidget::setEnabled);
    return existing;
  }

  CurveEditor* editor = new CurveEditor();
  editor->setProperty(kOwnerProperty, QVariant::fromValue<QObject*>(this));
  editor->setEnabled(field_->IsEnabled());
  fields_ = {field_};
  connect(editor, &CurveEditor::curvesEdited, this, &CurveFieldWidget::UpdateFromWidget);
  connect(editor, &CurveEditor::editFinished, this, &CurveFieldWidget::FinishEdit);
  connect(field_, &EffectField::EnabledChanged, editor, &QWidget::setEnabled);
  return editor;
}

void CurveFieldWidget::UpdateWidgetValue(QWidget* widget, double timecode) {
  // NaN: the attached fields differ, show this one's curves (the editor edits them all to what it shows)
  if (qIsNaN(timecode)) timecode = field_->Now();
  static_cast<CurveEditor*>(widget)->setCurves(field_->GetValueAt(timecode).toString());
}

void CurveFieldWidget::UpdateFromWidget(const QString& value) {
  // The gesture's first edit opens the changes, FinishEdit() pushes or drops them
  if (changes_.isEmpty()) {
    for (EffectField* f : fields_) changes_.append(new KeyframeDataChange(f));
  }
  for (EffectField* f : fields_) f->SetValueAt(f->Now(), value);
}

void CurveFieldWidget::FinishEdit(const QString& action, bool changed) {
  if (changes_.isEmpty()) return;
  if (!changed) {
    // Back where the gesture started (or a click that moved nothing): restore every field, no undo entry
    for (int i = 0; i < changes_.size(); i++) {
      changes_[i]->doUndo();
      emit fields_[i]->Changed();
    }
    qDeleteAll(changes_);
    changes_.clear();
    return;
  }
  ComboAction* ca = new ComboAction();
  for (KeyframeDataChange* kdc : changes_) {
    kdc->SetNewKeyframes();
    ca->append(kdc);
  }
  changes_.clear();
  ca->setText(action);
  amber::UndoStack.push(ca);
}
