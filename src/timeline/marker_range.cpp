#include "marker_range.h"

#include <cstdlib>

namespace amber::marker_range {

int end_tick_at(const QVector<Marker>& markers, int pos_x, const std::function<int(long)>& frame_to_screen) {
  for (int i = 0; i < markers.size(); i++) {
    const Marker& m = markers.at(i);
    if (m.duration <= 0) continue;
    const int end_x = frame_to_screen(m.frame + m.duration);
    if (end_x - frame_to_screen(m.frame) < kMinEndTickLength) continue;
    if (std::abs(pos_x - end_x) <= kEndTickHitRange) return i;
  }
  return -1;
}

long duration_to(long marker_frame, long end_frame) { return end_frame > marker_frame ? end_frame - marker_frame : 0; }

}  // namespace amber::marker_range
