#ifndef CURVEFIELDWIDGET_H
#define CURVEFIELDWIDGET_H

#include <QVariant>
#include <QVector>

#include "effects/ui/effectfieldwidget.h"

class KeyframeDataChange;

// CurveField <-> CurveEditor. The wrapper that created the editor drives it for every attached field (multi-selected
// clips): one undo entry per gesture, holding one change per field.
class CurveFieldWidget : public EffectFieldWidget {
  Q_OBJECT
 public:
  explicit CurveFieldWidget(EffectField* field, QObject* parent = nullptr);
  ~CurveFieldWidget() override;

  QWidget* CreateWidget(QWidget* existing = nullptr) override;
  void UpdateWidgetValue(QWidget* widget, double timecode) override;

 private slots:
  void UpdateFromWidget(const QString& value);
  void FinishEdit(const QString& action, bool changed);

 private:
  QVector<EffectField*> fields_;  // this wrapper's field first, then the ones attached to its editor
  QVector<KeyframeDataChange*> changes_;
};

#endif  // CURVEFIELDWIDGET_H
