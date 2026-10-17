#include "boolfield.h"

#include <QCheckBox>

BoolField::BoolField(EffectRow *parent, const QString &id) :
  EffectField(parent, id, EFFECT_FIELD_BOOL)
{}

bool BoolField::GetBoolAt(double timecode)
{
  return GetValueAt(timecode).toBool();
}

QWidget *BoolField::CreateWidget(QWidget *existing)
{
  QCheckBox* cb;

  if (existing == nullptr) {

    cb = new QCheckBox();
    cb->setEnabled(IsEnabled());

  } else {

    cb = static_cast<QCheckBox*>(existing);

  }

  if (existing == nullptr) {
    connect(cb, &QCheckBox::toggled, this, &BoolField::UpdateFromWidget);
  } else {
    FollowWidget(cb);
  }
  connect(this, &EffectField::EnabledChanged, cb, &QWidget::setEnabled);
  connect(cb, &QCheckBox::toggled, this, &BoolField::Toggled);

  return cb;
}

void BoolField::UpdateWidgetValue(QWidget *widget, double timecode)
{
  QCheckBox* cb = static_cast<QCheckBox*>(widget);

  // Setting the checked state on the checkbox below normally triggers a change() signal that will then trickle back
  // to setting a value on this field. Therefore we block its signals while we're setting this.

  cb->blockSignals(true);

  if (qIsNaN(timecode)) {
    cb->setTristate(true);
    cb->setCheckState(Qt::PartiallyChecked);
  } else {
    cb->setTristate(false);
    cb->setChecked(GetBoolAt(timecode));
  }

  cb->blockSignals(false);

  emit Toggled(cb->isChecked());
}

QVariant BoolField::ConvertStringToValue(const QString &s)
{
  return (s == "1");
}

QString BoolField::ConvertValueToString(const QVariant &v)
{
  return QString::number(v.toBool());
}

void BoolField::UpdateFromWidget(bool b)
{
  PushEdit(sender(), [b](EffectField* f) { return static_cast<BoolField*>(f)->ApplyValue(b); });
}

QUndoCommand* BoolField::ApplyValue(bool b)
{
  KeyframeDataChange* kdc = new KeyframeDataChange(this);

  SetValueAt(Now(), b);

  kdc->SetNewKeyframes();
  kdc->setText(QObject::tr("Change Value"));
  return kdc;
}
