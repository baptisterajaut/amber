#ifndef CORE_GUIDE_H
#define CORE_GUIDE_H

#include <cstdint>

struct Guide {
  enum Orientation : uint8_t { Horizontal, Vertical };
  Orientation orientation{Horizontal};
  int position{0};  // pixels in video/image space (0 = top-left)
  bool mirror{false};
};

#endif  // CORE_GUIDE_H
