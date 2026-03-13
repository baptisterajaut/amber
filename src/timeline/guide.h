#ifndef GUIDE_H
#define GUIDE_H

#include <QVector>

struct Guide {
  enum Orientation : uint8_t { Horizontal, Vertical };
  Orientation orientation{Horizontal};
  int position{0};  // pixels in video/image space (0 = top-left)
  bool mirror{false};
};

#endif  // GUIDE_H
