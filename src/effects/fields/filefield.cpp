#include "filefield.h"

#include <QDebug>

#include "effects/effect.h"
#include "effects/effectrow.h"
#include "global/config.h"
#include "ui/embeddedfilechooser.h"

FileField::FileField(EffectRow* parent, const QString &id) :
  EffectField(parent, id, EFFECT_FIELD_FILE)
{
  // Set default value to an empty string
  SetValueAt(0, "");
}

QString FileField::GetFileAt(double timecode)
{
  return GetValueAt(timecode).toString();
}

QWidget *FileField::CreateWidget(QWidget *existing)
{
  EmbeddedFileChooser* efc = (existing != nullptr) ? static_cast<EmbeddedFileChooser*>(existing) : new EmbeddedFileChooser();

  connect(efc, &EmbeddedFileChooser::changed, this, &FileField::UpdateFromWidget);
  connect(this, &EffectField::EnabledChanged, efc, &QWidget::setEnabled);

  return efc;
}

void FileField::UpdateWidgetValue(QWidget *widget, double timecode)
{
  EmbeddedFileChooser* efc = static_cast<EmbeddedFileChooser*>(widget);

  efc->blockSignals(true);
  efc->setFilename(GetFileAt(timecode));
  efc->blockSignals(false);
}

void FileField::UpdateFromWidget(const QString &s)
{
  // Only emitted when the user picks a file: picking a VST plugin is consent to run it, so trust its path before the
  // field change loads it (VSTHost::PluginPathTrusted)
  const Effect* e = GetParentRow()->GetParentEffect();
  if (!s.isEmpty() && e->meta != nullptr && e->meta->internal == EFFECT_INTERNAL_VST &&
      !amber::CurrentConfig.trusted_vst_plugins.contains(s)) {
    amber::CurrentConfig.trusted_vst_plugins.append(s);
  }

  KeyframeDataChange* kdc = new KeyframeDataChange(this);

  SetValueAt(Now(), s);

  kdc->SetNewKeyframes();
  kdc->setText(QObject::tr("Change Value"));
  amber::UndoStack.push(kdc);
}
