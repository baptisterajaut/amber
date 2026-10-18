#ifndef CORE_GRAPHMATH_H
#define CORE_GRAPHMATH_H

#include <QVector>
#include <optional>

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

// Time snapping of keys dragged in the Graph Editor: of all moving keys (old_times[i] + diff), the one closest to one
// of its targets (targets[i]) within range frames brings the whole drag onto that target. Returns true and adjusts
// diff when it snapped, *target being the frame snapped to.
constexpr int kSnapRangePx = 10;  // as Timeline::get_snap_range(), in the Graph Editor's own zoom
bool SnapKeyDrag(long& diff, const QVector<long>& old_times, const QVector<QVector<long>>& targets, double range,
                 long* target);

// Adaptive grid. Value lines every 1, 2 or 5 x 10^n units: the smallest such step at least kValueGridMinPx apart.
constexpr double kValueGridMinPx = 40;
double ValueGridStep(double px_per_unit);
// Decimals needed to label multiples of a value step (0 for steps of 1 and more)
int StepDecimals(double step);
// Time lines on whole frames: 1, 2, 5, 10, 20, 50... frames under a second, then 1, 2, 5, 10, 15, 30 s, 1, 2, 5, 10,
// 15, 30 min, 1 h and doublings, a second being qRound(frame_rate) frames. The smallest step at least kTimeGridMinPx
// apart.
constexpr double kTimeGridMinPx = 80;
long TimeGridStep(double px_per_frame, double frame_rate);

// Bezier handles of a key, relative to it (as in core/keyframe.h)
struct Handles {
  double pre_x;
  double pre_y;
  double post_x;
  double post_y;
};
enum class EaseSide { kBoth, kIn, kOut };

// After Effects' Easy Ease: the eased handle(s) flat (y 0), x a third of the way to the neighbouring key on that side
// (AE's 33 % influence). In is the pre handle (arriving at the key), Out the post handle. A side with no neighbour
// keeps its x (still flattened).
Handles EasyEase(const Handles& h, long time, std::optional<long> prev_time, std::optional<long> next_time,
                 EaseSide side);

}  // namespace amber::graph

#endif  // CORE_GRAPHMATH_H
