#include "colorlabel.h"

#include <QAction>
#include <QCoreApplication>
#include <QIcon>
#include <QPixmap>

namespace amber {

struct ColorLabelEntry {
  const char* name;
  QRgb rgb;
};

static constexpr ColorLabelEntry kColorLabels[kColorLabelCount] = {
    {QT_TRANSLATE_NOOP("ColorLabel", "Red"), 0xFFFF0000},    {QT_TRANSLATE_NOOP("ColorLabel", "Maroon"), 0xFF800000},
    {QT_TRANSLATE_NOOP("ColorLabel", "Orange"), 0xFFFF8000}, {QT_TRANSLATE_NOOP("ColorLabel", "Brown"), 0xFF8B4513},
    {QT_TRANSLATE_NOOP("ColorLabel", "Yellow"), 0xFFFFFF00}, {QT_TRANSLATE_NOOP("ColorLabel", "Olive"), 0xFF808000},
    {QT_TRANSLATE_NOOP("ColorLabel", "Lime"), 0xFF00FF00},   {QT_TRANSLATE_NOOP("ColorLabel", "Green"), 0xFF008000},
    {QT_TRANSLATE_NOOP("ColorLabel", "Cyan"), 0xFF00FFFF},   {QT_TRANSLATE_NOOP("ColorLabel", "Teal"), 0xFF008080},
    {QT_TRANSLATE_NOOP("ColorLabel", "Blue"), 0xFF0000FF},   {QT_TRANSLATE_NOOP("ColorLabel", "Navy"), 0xFF000080},
    {QT_TRANSLATE_NOOP("ColorLabel", "Pink"), 0xFFFF69B4},   {QT_TRANSLATE_NOOP("ColorLabel", "Purple"), 0xFF800080},
    {QT_TRANSLATE_NOOP("ColorLabel", "Silver"), 0xFFC0C0C0}, {QT_TRANSLATE_NOOP("ColorLabel", "Gray"), 0xFF808080},
};

QColor GetColorLabel(int index) {
  if (index < 1 || index > kColorLabelCount) return QColor();
  return QColor::fromRgba(kColorLabels[index - 1].rgb);
}

QString GetColorLabelName(int index) {
  if (index < 1 || index > kColorLabelCount) return QString();
  return QCoreApplication::translate("ColorLabel", kColorLabels[index - 1].name);
}

QMenu* BuildColorLabelMenu(QWidget* parent) {
  QMenu* menu = new QMenu(QObject::tr("Color Label"), parent);

  QAction* none_action = menu->addAction(QObject::tr("None"));
  none_action->setData(0);

  menu->addSeparator();

  for (int i = 0; i < kColorLabelCount; i++) {
    QPixmap px(16, 16);
    px.fill(QColor::fromRgba(kColorLabels[i].rgb));
    QAction* a = menu->addAction(QIcon(px), GetColorLabelName(i + 1));
    a->setData(i + 1);
  }

  return menu;
}

}  // namespace amber
