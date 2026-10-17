#ifndef CORE_ALPHA_H
#define CORE_ALPHA_H

#include <QtGlobal>

namespace amber {

// Premultiplied RGBA8 (the compositor's output) to straight alpha, in place: each colour channel becomes
// round(c * 255 / a), clamped to 255; a pixel with alpha 0 becomes (0, 0, 0, 0). `stride` is the row length in bytes.
void unpremultiply_rgba8(uchar* pixels, int width, int height, qsizetype stride);

}  // namespace amber

#endif  // CORE_ALPHA_H
