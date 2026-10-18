#ifndef CORE_GRAPHMATH_H
#define CORE_GRAPHMATH_H

#include <QVector>

namespace amber::graph {

// Pure math behind the Graph Editor (ui/graphview.cpp), kept here so it can be tested without a window.

// Value of a key dragged in the Graph Editor: it follows the mouse unrounded (no DoubleField declares a display
// precision; the screen already quantizes to 1 px), kept inside the field's range. A NaN bound is no bound, as in
// DoubleField.
double ClampToRange(double value, double minimum, double maximum);

// Range shown by "Zoom to Show All" / "Zoom to Selection": the keys' bounding box padded by kFitPadding of its span
// on each side. A zero value span (flat curve, single key) is widened to kFitMinValueSpan, a time span under
// kFitMinTimeSpan frames to that many frames, both around their centre.
struct FitRect {
  double min_time;
  double max_time;
  double min_value;
  double max_value;
};
constexpr double kFitPadding = 0.1;
constexpr double kFitMinValueSpan = 1.0;
constexpr double kFitMinTimeSpan = 10.0;
FitRect FitRange(double min_time, double max_time, double min_value, double max_value);

// Frame offset of keys dragged in time. Keys may pass each other (everything evaluates keys by time, not by index),
// but none may land on the frame of a key that isn't moving in its own field: playback would only ever see one of
// the two. On a collision the offset steps one frame back toward where the drag came from (previous_diff), like the
// Effect Controls keyframe view. old_times[i] is moved key i's time at the drag start, fixed_times[i] the times of
// the keys of its field that are not moving.
long AvoidKeyCollisions(long diff, long previous_diff, const QVector<long>& old_times,
                        const QVector<QVector<long>>& fixed_times);

}  // namespace amber::graph

#endif  // CORE_GRAPHMATH_H
