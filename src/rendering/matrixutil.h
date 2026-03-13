#ifndef MATRIXUTIL_H
#define MATRIXUTIL_H

#include <QMatrix4x4>

namespace MatrixUtil {

// Equivalent to glOrtho(left, right, bottom, top, near, far)
inline QMatrix4x4 ortho(float left, float right, float bottom, float top,
                        float near_val = -1.0f, float far_val = 1.0f) {
  QMatrix4x4 m;
  m.ortho(left, right, bottom, top, near_val, far_val);
  return m;
}

// Identity — replaces glLoadIdentity()
inline QMatrix4x4 identity() {
  return QMatrix4x4(); // default-constructed = identity
}

} // namespace MatrixUtil

#endif // MATRIXUTIL_H
