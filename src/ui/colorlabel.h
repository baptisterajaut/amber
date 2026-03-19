#ifndef COLORLABEL_H
#define COLORLABEL_H

#include <QColor>
#include <QMenu>
#include <QString>

namespace amber {

constexpr int kColorLabelCount = 16;

QColor GetColorLabel(int index);
QString GetColorLabelName(int index);

QMenu* BuildColorLabelMenu(QWidget* parent);

}  // namespace amber

#endif  // COLORLABEL_H
