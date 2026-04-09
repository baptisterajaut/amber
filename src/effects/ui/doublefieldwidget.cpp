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

  connect(ls, &LabelSlider::valueChanged, this, &DoubleFieldWidget::UpdateFromWidget);
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
  DoubleField* df = static_cast<DoubleField*>(field_);
  LabelSlider* ls = static_cast<LabelSlider*>(sender());

  if (ls->IsDragging() && kdc_ == nullptr) {
    kdc_ = new KeyframeDataChange(df);
  }

  df->SetValueAt(df->Now(), d);

  if (!ls->IsDragging() && kdc_ != nullptr) {
    kdc_->SetNewKeyframes();
    kdc_->setText(QObject::tr("Change Value"));

    amber::UndoStack.push(kdc_);

    kdc_ = nullptr;
  }
}
