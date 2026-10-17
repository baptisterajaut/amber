#ifndef MARKER_RANGE_H
#define MARKER_RANGE_H

#include <QVector>
#include <functional>

#include "core/marker.h"

// Range marker maths for the ruler, kept free of widgets so it can be tested on its own.
namespace amber::marker_range {

// A press this many pixels or closer to a range marker's end tick grabs the end.
constexpr int kEndTickHitRange = 3;

// Index of the first range marker whose end tick is within kEndTickHitRange px of pos_x, or -1. Point markers
// (duration 0) have no end tick.
int end_tick_at(const QVector<Marker>& markers, int pos_x, const std::function<int(long)>& frame_to_screen);

// Duration of a marker starting at marker_frame whose end is dragged to end_frame, clamped to >= 0.
long duration_to(long marker_frame, long end_frame);

}  // namespace amber::marker_range

#endif  // MARKER_RANGE_H
