#ifndef STRINGFIELDWIDGET_H
#define STRINGFIELDWIDGET_H

#include "effects/ui/effectfieldwidget.h"

class StringField;

class StringFieldWidget : public EffectFieldWidget {
  Q_OBJECT
public:
  explicit StringFieldWidget(EffectField* field, QObject* parent = nullptr);

  QWidget* CreateWidget(QWidget* existing = nullptr) override;
  void UpdateWidgetValue(QWidget* widget, double timecode) override;

private slots:
  void UpdateFromWidget(const QString& s);
};

#endif  // STRINGFIELDWIDGET_H
