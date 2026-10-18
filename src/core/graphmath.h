#ifndef CORE_GRAPHMATH_H
#define CORE_GRAPHMATH_H

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

}  // namespace amber::graph

#endif  // CORE_GRAPHMATH_H
