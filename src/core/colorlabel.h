#ifndef CORE_COLORLABEL_H
#define CORE_COLORLABEL_H

#include <QColor>
#include <QString>

namespace amber {

constexpr int kColorLabelCount = 16;

QColor GetColorLabel(int index);
QString GetColorLabelName(int index);

}  // namespace amber

#endif  // CORE_COLORLABEL_H
