#include "core/alpha.h"

#include <array>

namespace amber {

namespace {

// kTable[a * 256 + c] = round(c * 255 / a), clamped (a blend can leave a channel above its alpha); row 0 is all zero
std::array<uchar, 256 * 256> make_table() {
  std::array<uchar, 256 * 256> t{};
  for (int a = 1; a < 256; a++) {
    for (int c = 0; c < 256; c++) {
      t[a * 256 + c] = uchar(qMin(255, (c * 255 + a / 2) / a));
    }
  }
  return t;
}

}  // namespace

void unpremultiply_rgba8(uchar* pixels, int width, int height, qsizetype stride) {
  static const std::array<uchar, 256 * 256> kTable = make_table();
  for (int y = 0; y < height; y++) {
    uchar* p = pixels + y * stride;
    for (int x = 0; x < width; x++, p += 4) {
      const int a = p[3];
      if (a == 255) continue;  // opaque: unchanged
      const uchar* row = kTable.data() + a * 256;
      p[0] = row[p[0]];
      p[1] = row[p[1]];
      p[2] = row[p[2]];
    }
  }
}

}  // namespace amber
