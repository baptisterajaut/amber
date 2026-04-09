#include "colorlabel.h"

#include <QAction>
#include <QIcon>
#include <QPixmap>

namespace amber {

QMenu* BuildColorLabelMenu(QWidget* parent) {
  QMenu* menu = new QMenu(QObject::tr("Color Label"), parent);

  QAction* none_action = menu->addAction(QObject::tr("None"));
  none_action->setData(0);

  menu->addSeparator();

  for (int i = 1; i <= kColorLabelCount; i++) {
    QPixmap px(16, 16);
    px.fill(GetColorLabel(i));
    QAction* a = menu->addAction(QIcon(px), GetColorLabelName(i));
    a->setData(i);
  }

  return menu;
}

}  // namespace amber
