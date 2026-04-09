#ifndef FONTFIELDWIDGET_H
#define FONTFIELDWIDGET_H

#include "effects/ui/effectfieldwidget.h"

class FontField;

class FontFieldWidget : public EffectFieldWidget {
  Q_OBJECT
public:
  explicit FontFieldWidget(EffectField* field, QObject* parent = nullptr);

  QWidget* CreateWidget(QWidget* existing = nullptr) override;
  void UpdateWidgetValue(QWidget* widget, double timecode) override;

private slots:
  void UpdateFromWidget(const QString& s);
};

#endif  // FONTFIELDWIDGET_H
