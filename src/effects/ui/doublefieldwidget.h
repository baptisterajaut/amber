#ifndef DOUBLEFIELDWIDGET_H
#define DOUBLEFIELDWIDGET_H

#include "effects/ui/effectfieldwidget.h"

class DoubleField;
class KeyframeDataChange;

class DoubleFieldWidget : public EffectFieldWidget {
  Q_OBJECT
public:
  explicit DoubleFieldWidget(EffectField* field, QObject* parent = nullptr);

  QWidget* CreateWidget(QWidget* existing = nullptr) override;
  void UpdateWidgetValue(QWidget* widget, double timecode) override;

private slots:
  void UpdateFromWidget(double d);

private:
  KeyframeDataChange* kdc_{nullptr};
};

#endif  // DOUBLEFIELDWIDGET_H
