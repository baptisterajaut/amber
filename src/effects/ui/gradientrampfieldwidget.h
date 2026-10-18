#ifndef GRADIENTRAMPFIELDWIDGET_H
#define GRADIENTRAMPFIELDWIDGET_H

#include <QColor>
#include <QVariant>
#include <QVector>

#include "effects/ui/effectfieldwidget.h"

class GradientRampField;
class KeyframeDataChange;

// GradientRampField <-> GradientRampEditor, the way CurveFieldWidget drives a CurveEditor: the wrapper that created the
// editor drives it for every attached field (multi-selected clips), one undo entry per gesture holding one change per
// field. The editor's end handles edit each field's Start Color / End Color, and Reverse Ramp mirrors the stops and
// swaps those two colours in one entry.
class GradientRampFieldWidget : public EffectFieldWidget {
  Q_OBJECT
 public:
  explicit GradientRampFieldWidget(EffectField* field, QObject* parent = nullptr);
  ~GradientRampFieldWidget() override;

  QWidget* CreateWidget(QWidget* existing = nullptr) override;
  void UpdateWidgetValue(QWidget* widget, double timecode) override;

 private slots:
  void UpdateFromWidget(const QString& value);
  void FinishEdit(const QString& action, bool changed);
  void EditEndColor(int end, const QColor& color);
  void Reverse(const QString& value);

 private:
  QVector<GradientRampField*> fields_;  // this wrapper's field first, then the ones attached to its editor
  QVector<KeyframeDataChange*> changes_;
};

#endif  // GRADIENTRAMPFIELDWIDGET_H
