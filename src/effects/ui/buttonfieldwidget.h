#ifndef BUTTONFIELDWIDGET_H
#define BUTTONFIELDWIDGET_H

#include "effects/ui/effectfieldwidget.h"

class ButtonField;

class ButtonFieldWidget : public EffectFieldWidget {
  Q_OBJECT
public:
  explicit ButtonFieldWidget(EffectField* field, QObject* parent = nullptr);

  QWidget* CreateWidget(QWidget* existing = nullptr) override;
  void UpdateWidgetValue(QWidget* widget, double timecode) override;
};

#endif  // BUTTONFIELDWIDGET_H
