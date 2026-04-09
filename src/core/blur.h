#ifndef CORE_BLUR_H
#define CORE_BLUR_H

#include <QImage>
#include <QRect>

namespace amber {

void blur(QImage& result, const QRect& rect, int radius, bool alphaOnly);

}  // namespace amber

#endif  // CORE_BLUR_H
