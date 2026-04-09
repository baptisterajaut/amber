#include "colorfieldwidget.h"

#include <QColor>
#include <QHBoxLayout>
#include <QPushButton>

#include "effects/fields/colorfield.h"
#include "effects/effectrow.h"
#include "effects/keyframe.h"
#include "engine/undo/undo.h"
#include "engine/undo/undostack.h"
#include "panels/panels.h"
#include "panels/viewer.h"
#include "ui/colorbutton.h"
#include "ui/icons.h"
#include "ui/viewerwidget.h"

ColorFieldWidget::ColorFieldWidget(EffectField* field, QObject* parent)
    : EffectFieldWidget(field, parent) {}

void ColorFieldWidget::disconnectPick() {
  QObject::disconnect(pick_connection_);
  QObject::disconnect(cancel_connection_);
}

QWidget* ColorFieldWidget::CreateWidget(QWidget* existing) {
  ColorField* cf = static_cast<ColorField*>(field_);
  ColorButton* cb;
  QWidget* wrapper;

  if (existing != nullptr) {
    wrapper = existing;
    cb = existing->findChild<ColorButton*>();
  } else {
    wrapper = new QWidget();
    wrapper->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Minimum);

    QHBoxLayout* layout = new QHBoxLayout(wrapper);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(2);

    cb = new ColorButton();
    layout->addWidget(cb);

    QPushButton* eyedropper = new QPushButton();
    eyedropper->setIcon(amber::icon::CreateIconFromSVG(":/icons/eyedropper.svg", false));
    eyedropper->setIconSize(QSize(10, 10));
    eyedropper->setFixedSize(16, 16);
    eyedropper->setFlat(true);
    eyedropper->setToolTip(tr("Pick color from viewer"));
    layout->addWidget(eyedropper);

    connect(eyedropper, &QPushButton::clicked, this, [this, cb]() {
      if (panel_sequence_viewer == nullptr || panel_sequence_viewer->viewer_widget == nullptr) {
        return;
      }
      ViewerWidget* vw = panel_sequence_viewer->viewer_widget;

      disconnectPick();
      vw->startColorPick();

      pick_connection_ = connect(vw, &ViewerWidget::colorPicked, this, [this, cb](const QColor& c) {
        disconnectPick();
        cb->set_color(c);
        emit cb->color_changed(c);
      });

      cancel_connection_ = connect(vw, &ViewerWidget::colorPickCancelled, this, [this]() {
        disconnectPick();
      });
    });
  }

  connect(cb, &ColorButton::color_changed, this, &ColorFieldWidget::UpdateFromWidget);
  connect(cf, &EffectField::EnabledChanged, wrapper, &QWidget::setEnabled);

  return wrapper;
}

void ColorFieldWidget::UpdateWidgetValue(QWidget* widget, double timecode) {
  ColorField* cf = static_cast<ColorField*>(field_);
  ColorButton* cb = widget->findChild<ColorButton*>();
  if (cb != nullptr) {
    cb->set_color(cf->GetColorAt(timecode));
  }
}

void ColorFieldWidget::UpdateFromWidget(const QColor& c) {
  ColorField* cf = static_cast<ColorField*>(field_);
  KeyframeDataChange* kdc = new KeyframeDataChange(cf);

  cf->SetValueAt(cf->Now(), c);

  kdc->SetNewKeyframes();
  kdc->setText(QObject::tr("Change Color"));
  amber::UndoStack.push(kdc);
}
