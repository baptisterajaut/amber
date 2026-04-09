#ifndef LABELFIELDWIDGET_H
#define LABELFIELDWIDGET_H

#include "effects/ui/effectfieldwidget.h"

class LabelField;

class LabelFieldWidget : public EffectFieldWidget {
  Q_OBJECT
public:
  explicit LabelFieldWidget(EffectField* field, QObject* parent = nullptr);

  QWidget* CreateWidget(QWidget* existing = nullptr) override;
  void UpdateWidgetValue(QWidget* widget, double timecode) override;
};

#endif  // LABELFIELDWIDGET_H
