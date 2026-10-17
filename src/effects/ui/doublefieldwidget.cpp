#include "doublefieldwidget.h"

#include "effects/effectrow.h"
#include "effects/fields/doublefield.h"
#include "effects/keyframe.h"
#include "engine/undo/undo.h"
#include "engine/undo/undostack.h"
#include "ui/labelslider.h"

DoubleFieldWidget::DoubleFieldWidget(EffectField* field, QObject* parent) : EffectFieldWidget(field, parent) {}

QWidget* DoubleFieldWidget::CreateWidget(QWidget* existing) {
  DoubleField* df = static_cast<DoubleField*>(field_);
  LabelSlider* ls;

  if (existing == nullptr) {
    ls = new LabelSlider();

    if (!qIsNaN(df->GetMinimum())) {
      ls->SetMinimum(df->GetMinimum());
    }
    ls->SetDefault(df->GetDefault());
    if (!qIsNaN(df->GetMaximum())) {
      ls->SetMaximum(df->GetMaximum());
    }
    ls->SetDisplayType(static_cast<amber::DisplayType>(df->GetDisplayType()));
    ls->SetFrameRate(df->GetFrameRate());

    ls->setEnabled(df->IsEnabled());
  } else {
    ls = static_cast<LabelSlider*>(existing);
  }

  if (AttachToWidget(ls)) connect(ls, &LabelSlider::valueChanged, this, &DoubleFieldWidget::UpdateFromWidget);
  connect(ls, &LabelSlider::clicked, df, &EffectField::Clicked);
  connect(df, &EffectField::EnabledChanged, ls, &QWidget::setEnabled);
  connect(df, &DoubleField::MaximumChanged, ls, &LabelSlider::SetMaximum);
  connect(df, &DoubleField::MinimumChanged, ls, &LabelSlider::SetMinimum);

  return ls;
}

void DoubleFieldWidget::UpdateWidgetValue(QWidget* widget, double timecode) {
  DoubleField* df = static_cast<DoubleField*>(field_);
  if (qIsNaN(timecode)) {
    static_cast<LabelSlider*>(widget)->SetValue(qSNaN());
  } else {
    static_cast<LabelSlider*>(widget)->SetValue(df->GetDoubleAt(timecode));
  }
}

void DoubleFieldWidget::UpdateFromWidget(double d) {
  // The attached wrappers are called directly, so they get the slider from here rather than from sender()
  LabelSlider* ls = static_cast<LabelSlider*>(sender());
  PushEdit([d, ls](EffectFieldWidget* w) { return static_cast<DoubleFieldWidget*>(w)->ApplyValue(d, ls); });
}

QUndoCommand* DoubleFieldWidget::ApplyValue(double d, LabelSlider* ls) {
  DoubleField* df = static_cast<DoubleField*>(field_);

  // A drag opens the undo entry on its first move and pushes it on release; a typed value or "Reset to Default"
  // arrives as a single non-drag change and gets its own entry
  bool single_change = (kdc_ == nullptr && !ls->IsDragging());
  double old_value = df->GetDoubleAt(df->Now());
  int old_key_count = df->keyframes.size();
  if (kdc_ == nullptr) {
    kdc_ = new KeyframeDataChange(df);
  }

  df->SetValueAt(df->Now(), d);

  // Same value typed again (or Reset on a field already at default) and no keyframe added: nothing to undo
  if (single_change && old_value == d && old_key_count == df->keyframes.size()) {
    delete kdc_;
    kdc_ = nullptr;
    return nullptr;
  }

  if (!ls->IsDragging() && kdc_ != nullptr) {
    kdc_->SetNewKeyframes();
    kdc_->setText(QObject::tr("Change Value"));

    KeyframeDataChange* kdc = kdc_;
    kdc_ = nullptr;
    return kdc;
  }

  return nullptr;
}
