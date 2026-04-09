#ifndef COMBOFIELDWIDGET_H
#define COMBOFIELDWIDGET_H

#include "effects/ui/effectfieldwidget.h"

class ComboField;

class ComboFieldWidget : public EffectFieldWidget {
  Q_OBJECT
public:
  explicit ComboFieldWidget(EffectField* field, QObject* parent = nullptr);

  QWidget* CreateWidget(QWidget* existing = nullptr) override;
  void UpdateWidgetValue(QWidget* widget, double timecode) override;

private slots:
  void UpdateFromWidget(int index);
};

#endif  // COMBOFIELDWIDGET_H
