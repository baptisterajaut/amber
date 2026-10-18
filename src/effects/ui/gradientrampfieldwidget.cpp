#include "gradientrampfieldwidget.h"

#include "effects/fields/colorfield.h"
#include "effects/fields/gradientrampfield.h"
#include "engine/undo/undo.h"
#include "engine/undo/undo_effect.h"
#include "engine/undo/undostack.h"
#include "ui/gradientrampeditor.h"

namespace {
// The wrapper driving an editor, so a field attached later (AddAdditionalEffect) joins it
const char* const kOwnerProperty = "amber_gradient_ramp_field_widget";
}  // namespace

GradientRampFieldWidget::GradientRampFieldWidget(EffectField* field, QObject* parent)
    : EffectFieldWidget(field, parent) {}

GradientRampFieldWidget::~GradientRampFieldWidget() {
  // A gesture still open (the editor went first, or never got to end it): keep its edit undoable
  if (!changes_.isEmpty()) FinishEdit(tr("Change Color Ramp"), true);
}

QWidget* GradientRampFieldWidget::CreateWidget(QWidget* existing) {
  GradientRampField* field = static_cast<GradientRampField*>(field_);
  if (existing != nullptr) {
    GradientRampFieldWidget* owner =
        qobject_cast<GradientRampFieldWidget*>(existing->property(kOwnerProperty).value<QObject*>());
    if (owner != nullptr && !owner->fields_.contains(field)) owner->fields_.append(field);
    connect(field_, &EffectField::EnabledChanged, existing, &QWidget::setEnabled);
    return existing;
  }

  GradientRampEditor* editor = new GradientRampEditor();
  editor->setProperty(kOwnerProperty, QVariant::fromValue<QObject*>(this));
  editor->setEnabled(field_->IsEnabled());
  editor->setEndsEditable(field->StartField() != nullptr && field->EndField() != nullptr);
  fields_ = {field};
  connect(editor, &GradientRampEditor::rampEdited, this, &GradientRampFieldWidget::UpdateFromWidget);
  connect(editor, &GradientRampEditor::editFinished, this, &GradientRampFieldWidget::FinishEdit);
  connect(editor, &GradientRampEditor::endColorEdited, this, &GradientRampFieldWidget::EditEndColor);
  connect(editor, &GradientRampEditor::rampReversed, this, &GradientRampFieldWidget::Reverse);
  connect(field_, &EffectField::EnabledChanged, editor, &QWidget::setEnabled);
  return editor;
}

void GradientRampFieldWidget::UpdateWidgetValue(QWidget* widget, double timecode) {
  // NaN: the attached fields differ, show this one's ramp (the editor edits them all to what it shows)
  if (qIsNaN(timecode)) timecode = field_->Now();
  GradientRampField* field = static_cast<GradientRampField*>(field_);
  GradientRampEditor* editor = static_cast<GradientRampEditor*>(widget);
  editor->setRamp(field->GetValueAt(timecode).toString());
  // The ends are keyframeable fields of their own: shown at their own Now(), as their own widgets show them
  if (field->StartField() != nullptr && field->EndField() != nullptr) {
    editor->setEndColors(field->StartField()->GetColorAt(field->StartField()->Now()),
                         field->EndField()->GetColorAt(field->EndField()->Now()));
  }
}

void GradientRampFieldWidget::UpdateFromWidget(const QString& value) {
  // The gesture's first edit opens the changes, FinishEdit() pushes or drops them
  if (changes_.isEmpty()) {
    for (GradientRampField* f : fields_) changes_.append(new KeyframeDataChange(f));
  }
  for (GradientRampField* f : fields_) f->SetValueAt(f->Now(), value);
}

void GradientRampFieldWidget::FinishEdit(const QString& action, bool changed) {
  if (changes_.isEmpty()) return;
  if (!changed) {
    // Back where the gesture started (a stop added and dragged off again): restore every field, no undo entry
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

void GradientRampFieldWidget::EditEndColor(int end, const QColor& color) {
  ComboAction* ca = new ComboAction();
  for (GradientRampField* f : fields_) {
    ColorField* cf = end == 0 ? f->StartField() : f->EndField();
    if (cf == nullptr) continue;
    KeyframeDataChange* kdc = new KeyframeDataChange(cf);
    cf->SetValueAt(cf->Now(), color);
    kdc->SetNewKeyframes();
    ca->append(kdc);
  }
  if (!ca->hasActions()) {
    delete ca;
    return;
  }
  ca->setText(tr("Change Color"));
  amber::UndoStack.push(ca);
}

void GradientRampFieldWidget::Reverse(const QString& value) {
  // One entry for the three fields of every attached effect: the mirrored stops, and the two ends swapped (each
  // effect its own ends, at their own Now(), so keyframed ends get a key there like any colour edit)
  ComboAction* ca = new ComboAction();
  for (GradientRampField* f : fields_) {
    KeyframeDataChange* ramp = new KeyframeDataChange(f);
    f->SetValueAt(f->Now(), value);
    ramp->SetNewKeyframes();
    ca->append(ramp);

    ColorField* start = f->StartField();
    ColorField* end = f->EndField();
    if (start == nullptr || end == nullptr) continue;
    const QColor a = start->GetColorAt(start->Now());
    const QColor b = end->GetColorAt(end->Now());
    KeyframeDataChange* sa = new KeyframeDataChange(start);
    KeyframeDataChange* sb = new KeyframeDataChange(end);
    start->SetValueAt(start->Now(), b);
    end->SetValueAt(end->Now(), a);
    sa->SetNewKeyframes();
    sb->SetNewKeyframes();
    ca->append(sa);
    ca->append(sb);
  }
  ca->setText(tr("Reverse Color Ramp"));
  amber::UndoStack.push(ca);
}
