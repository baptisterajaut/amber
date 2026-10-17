#include "filefieldwidget.h"

#include "effects/fields/filefield.h"
#include "effects/effect.h"
#include "effects/effectrow.h"
#include "effects/keyframe.h"
#include "engine/undo/undo.h"
#include "engine/undo/undostack.h"
#include "global/config.h"
#include "ui/embeddedfilechooser.h"

FileFieldWidget::FileFieldWidget(EffectField* field, QObject* parent)
    : EffectFieldWidget(field, parent) {}

QWidget* FileFieldWidget::CreateWidget(QWidget* existing) {
  FileField* ff = static_cast<FileField*>(field_);
  EmbeddedFileChooser* efc = (existing != nullptr)
      ? static_cast<EmbeddedFileChooser*>(existing)
      : new EmbeddedFileChooser();

  connect(efc, &EmbeddedFileChooser::changed, this, &FileFieldWidget::UpdateFromWidget);
  connect(ff, &EffectField::EnabledChanged, efc, &QWidget::setEnabled);

  return efc;
}

void FileFieldWidget::UpdateWidgetValue(QWidget* widget, double timecode) {
  FileField* ff = static_cast<FileField*>(field_);
  EmbeddedFileChooser* efc = static_cast<EmbeddedFileChooser*>(widget);

  efc->blockSignals(true);
  efc->setFilename(ff->GetFileAt(timecode));
  efc->blockSignals(false);
}

void FileFieldWidget::UpdateFromWidget(const QString& s) {
  FileField* ff = static_cast<FileField*>(field_);

  // Only emitted when the user picks a file: picking a VST plugin is consent to run it, so trust its path before the
  // field change loads it (VSTHost::PluginPathTrusted)
  const Effect* e = ff->GetParentRow()->GetParentEffect();
  if (!s.isEmpty() && e->meta != nullptr && e->meta->internal == EFFECT_INTERNAL_VST &&
      !amber::CurrentConfig.trusted_vst_plugins.contains(s)) {
    amber::CurrentConfig.trusted_vst_plugins.append(s);
  }

  KeyframeDataChange* kdc = new KeyframeDataChange(ff);

  ff->SetValueAt(ff->Now(), s);

  kdc->SetNewKeyframes();
  kdc->setText(QObject::tr("Change Value"));
  amber::UndoStack.push(kdc);
}
