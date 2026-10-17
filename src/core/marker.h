#ifndef CORE_MARKER_H
#define CORE_MARKER_H

#include <QString>

#define MARKER_SIZE 4

struct Marker {
  long frame;
  QString name;
  int color_label{0};
  // Frames covered after `frame`; 0 is a point marker. A range marker ends at frame + duration.
  // CBA: no range-from-In/Out or chapter export; add if asked
  long duration{0};
};

#endif  // CORE_MARKER_H
