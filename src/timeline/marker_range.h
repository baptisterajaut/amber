#ifndef MARKER_RANGE_H
#define MARKER_RANGE_H

#include <QVector>
#include <functional>

#include "core/marker.h"

class ResizeMarkerAction;

// Range marker maths for the ruler, kept free of widgets so it can be tested on its own.
namespace amber::marker_range {

// A press this many pixels or closer to a range marker's end tick grabs the end.
constexpr int kEndTickHitRange = 3;

// A range marker drawn shorter than this has no grabbable end tick: its end would sit in the head's own hit band
// (MARKER_SIZE each side) and win every press, so the marker could not be moved. Zoom in to resize it.
constexpr int kMinEndTickLength = 2 * MARKER_SIZE;

// Index of the first range marker whose end tick is within kEndTickHitRange px of pos_x, or -1. Point markers
// (duration 0) and range markers shorter than kMinEndTickLength px on screen have no end tick.
int end_tick_at(const QVector<Marker>& markers, int pos_x, const std::function<int(long)>& frame_to_screen);

// Duration of a marker starting at marker_frame whose end is dragged to end_frame, clamped to >= 0.
long duration_to(long marker_frame, long end_frame);

// Half-width of the playhead's handle on the ruler: a press on it grabs the playhead, not a marker head.
constexpr int kPlayheadSize = 6;

// What a left press on the ruler grabs. end >= 0: that marker's end, dragged to resize it (a range marker's end tick,
// which wins over every head, or a point marker's head pressed with Alt); else head >= 0: the selected markers, dragged
// to move them; both -1: no marker, the press moves the playhead. Presses at or above marker_offset (the timecode band)
// grab no marker. `selected` follows the press like a list click (shift toggles the pressed head) and holds only `end`
// when an end is grabbed.
struct PressHit {
  int end{-1};
  int head{-1};
};
PressHit press_hit(QVector<int>& selected, const QVector<Marker>& markers, int pos_x, int pos_y, int marker_offset,
                   int playhead_x, bool shift, bool alt, const std::function<int(long)>& frame_to_screen);

// The frame a marker's end is dragged to: where it was at the press (marker_frame + original_duration), moved by the
// frames the mouse travelled since. Snapping applies to it before duration_to().
long dragged_end_frame(long marker_frame, long original_duration, long press_frame, long mouse_frame);

// The undo step that ends an end drag: the marker's duration went from original_duration to its current one. nullptr
// when it didn't change or the index is stale.
ResizeMarkerAction* resize_release_action(QVector<Marker>* markers, int index, long original_duration);

}  // namespace amber::marker_range

#endif  // MARKER_RANGE_H
