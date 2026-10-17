#include "effectfieldwidget.h"

#include "engine/undo/comboaction.h"
#include "engine/undo/undostack.h"

namespace {
// The wrapper driving a widget, so a wrapper attached later (EffectUI::AddAdditionalEffect) joins it
const char* const kOwnerProperty = "amber_effect_field_widget";
}  // namespace

EffectFieldWidget::EffectFieldWidget(EffectField* field, QObject* parent)
    : QObject(parent), field_(field) {}

EffectField* EffectFieldWidget::GetField() const {
  return field_;
}

bool EffectFieldWidget::AttachToWidget(QWidget* widget) {
  QPointer<QObject> owner_object = widget->property(kOwnerProperty).value<QPointer<QObject>>();
  EffectFieldWidget* owner = qobject_cast<EffectFieldWidget*>(owner_object.data());
  if (owner == nullptr) {
    widget->setProperty(kOwnerProperty, QVariant::fromValue(QPointer<QObject>(this)));
    return true;
  }
  if (owner != this && !owner->followers_.contains(this)) owner->followers_.append(this);
  return false;
}

void EffectFieldWidget::PushEdit(const std::function<QUndoCommand*(EffectFieldWidget*)>& apply) {
  QVector<QUndoCommand*> changes;
  if (QUndoCommand* c = apply(this)) changes.append(c);
  for (const QPointer<EffectFieldWidget>& follower : followers_) {
    if (follower.isNull()) continue;
    if (QUndoCommand* c = apply(follower.data())) changes.append(c);
  }

  if (changes.isEmpty()) return;
  if (changes.size() == 1) {
    amber::UndoStack.push(changes.first());
    return;
  }

  // Several clips' fields changed together: one entry, under the text a single one would have had
  ComboAction* ca = new ComboAction(changes.first()->text());
  for (QUndoCommand* c : changes) ca->append(c);
  amber::UndoStack.push(ca);
}
