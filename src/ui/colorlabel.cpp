#include "colorlabel.h"

#include <QAction>
#include <QIcon>
#include <QPixmap>

namespace amber {

struct ColorLabelEntry {
  const char* name;
  QRgb rgb;
};

static constexpr ColorLabelEntry kColorLabels[kColorLabelCount] = {
    {"Red", 0xFFFF0000},    {"Maroon", 0xFF800000}, {"Orange", 0xFFFF8000}, {"Brown", 0xFF8B4513},
    {"Yellow", 0xFFFFFF00}, {"Olive", 0xFF808000},  {"Lime", 0xFF00FF00},   {"Green", 0xFF008000},
    {"Cyan", 0xFF00FFFF},   {"Teal", 0xFF008080},   {"Blue", 0xFF0000FF},   {"Navy", 0xFF000080},
    {"Pink", 0xFFFF69B4},   {"Purple", 0xFF800080}, {"Silver", 0xFFC0C0C0}, {"Gray", 0xFF808080},
};

QColor GetColorLabel(int index) {
  if (index < 1 || index > kColorLabelCount) return QColor();
  return QColor::fromRgba(kColorLabels[index - 1].rgb);
}

QString GetColorLabelName(int index) {
  if (index < 1 || index > kColorLabelCount) return QString();
  return QObject::tr(kColorLabels[index - 1].name);
}

QMenu* BuildColorLabelMenu(QWidget* parent) {
  QMenu* menu = new QMenu(QObject::tr("Color Label"), parent);

  QAction* none_action = menu->addAction(QObject::tr("None"));
  none_action->setData(0);

  menu->addSeparator();

  for (int i = 0; i < kColorLabelCount; i++) {
    QPixmap px(16, 16);
    px.fill(QColor::fromRgba(kColorLabels[i].rgb));
    QAction* a = menu->addAction(QIcon(px), QObject::tr(kColorLabels[i].name));
    a->setData(i + 1);
  }

  return menu;
}

}  // namespace amber
