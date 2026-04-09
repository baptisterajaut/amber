#include "colorlabel.h"

#include <QObject>

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

}  // namespace amber
