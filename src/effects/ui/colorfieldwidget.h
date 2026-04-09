#ifndef COLORFIELDWIDGET_H
#define COLORFIELDWIDGET_H

#include <QMetaObject>
#include "effects/ui/effectfieldwidget.h"

class ColorField;

class ColorFieldWidget : public EffectFieldWidget {
  Q_OBJECT
public:
  explicit ColorFieldWidget(EffectField* field, QObject* parent = nullptr);

  QWidget* CreateWidget(QWidget* existing = nullptr) override;
  void UpdateWidgetValue(QWidget* widget, double timecode) override;

private slots:
  void UpdateFromWidget(const QColor& c);

private:
  QMetaObject::Connection pick_connection_;
  QMetaObject::Connection cancel_connection_;
  void disconnectPick();
};

#endif  // COLORFIELDWIDGET_H
