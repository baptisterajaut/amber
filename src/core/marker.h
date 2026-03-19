#ifndef CORE_MARKER_H
#define CORE_MARKER_H

#include <QString>

#define MARKER_SIZE 4

struct Marker {
  long frame;
  QString name;
  int color_label{0};
};

#endif  // CORE_MARKER_H
