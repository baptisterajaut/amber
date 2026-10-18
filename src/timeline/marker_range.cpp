#include "marker_range.h"

#include <cstdlib>

#include "engine/undo/undo_timeline.h"

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

// Index of the marker whose head is under pos_x, or -1, updating `selected` on a hit (shift toggles)
static int head_at(QVector<int>& selected, const QVector<Marker>& markers, int pos_x, int pos_y, int marker_offset,
                   int playhead_x, bool shift, const std::function<int(long)>& frame_to_screen) {
  if (pos_y <= marker_offset) return -1;
  if (pos_x >= playhead_x - kPlayheadSize && pos_x <= playhead_x + kPlayheadSize) return -1;
  for (int i = 0; i < markers.size(); i++) {
    int marker_pos = frame_to_screen(markers.at(i).frame);
    if (pos_x <= marker_pos - MARKER_SIZE || pos_x >= marker_pos + MARKER_SIZE) continue;
    bool found = false;
    for (int j = 0; j < selected.size(); j++) {
      if (selected.at(j) == i) {
        if (shift) selected.removeAt(j);
        found = true;
        break;
      }
    }
    if (!found) {
      if (!shift) selected.clear();
      selected.append(i);
    }
    return i;
  }
  return -1;
}

PressHit press_hit(QVector<int>& selected, const QVector<Marker>& markers, int pos_x, int pos_y, int marker_offset,
                   int playhead_x, bool shift, bool alt, const std::function<int(long)>& frame_to_screen) {
  PressHit hit;
  // a range marker's end tick wins over heads; its band is no hit target, so long ranges don't eat the ruler
  if (pos_y > marker_offset) hit.end = end_tick_at(markers, pos_x, frame_to_screen);
  if (hit.end < 0) {
    hit.head = head_at(selected, markers, pos_x, pos_y, marker_offset, playhead_x, shift, frame_to_screen);
  }
  // Alt-drag on a point marker's head drags out a duration (a point marker has no end tick to grab)
  if (hit.head >= 0 && alt && markers.at(hit.head).duration == 0) hit.end = hit.head;
  if (hit.end >= 0) selected = {hit.end};
  return hit;
}

long dragged_end_frame(long marker_frame, long original_duration, long press_frame, long mouse_frame) {
  return marker_frame + original_duration + mouse_frame - press_frame;
}

ResizeMarkerAction* resize_release_action(QVector<Marker>* markers, int index, long original_duration) {
  if (markers == nullptr || index < 0 || index >= markers->size()) return nullptr;
  const long new_duration = markers->at(index).duration;
  if (new_duration == original_duration) return nullptr;
  return new ResizeMarkerAction(markers, index, original_duration, new_duration);
}

}  // namespace amber::marker_range
