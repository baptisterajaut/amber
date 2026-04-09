#ifndef BOOLFIELDWIDGET_H
#define BOOLFIELDWIDGET_H

#include "effects/ui/effectfieldwidget.h"

class BoolField;

class BoolFieldWidget : public EffectFieldWidget {
  Q_OBJECT
public:
  explicit BoolFieldWidget(EffectField* field, QObject* parent = nullptr);

  QWidget* CreateWidget(QWidget* existing = nullptr) override;
  void UpdateWidgetValue(QWidget* widget, double timecode) override;

private slots:
  void UpdateFromWidget(bool b);
};

#endif  // BOOLFIELDWIDGET_H
