#ifndef FILEFIELDWIDGET_H
#define FILEFIELDWIDGET_H

#include "effects/ui/effectfieldwidget.h"

class FileField;

class FileFieldWidget : public EffectFieldWidget {
  Q_OBJECT
public:
  explicit FileFieldWidget(EffectField* field, QObject* parent = nullptr);

  QWidget* CreateWidget(QWidget* existing = nullptr) override;
  void UpdateWidgetValue(QWidget* widget, double timecode) override;

private slots:
  void UpdateFromWidget(const QString& s);
};

#endif  // FILEFIELDWIDGET_H
