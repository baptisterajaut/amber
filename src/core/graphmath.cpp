#include "graphmath.h"

#include <cmath>

namespace amber::graph {

double ClampToRange(double value, double minimum, double maximum) {
  if (!std::isnan(minimum) && value < minimum) value = minimum;
  if (!std::isnan(maximum) && value > maximum) value = maximum;
  return value;
}

double ClampDragValue(double value, double start, double minimum, double maximum) {
  if (!std::isnan(start)) {
    if (!std::isnan(minimum) && start < minimum) minimum = start;
    if (!std::isnan(maximum) && start > maximum) maximum = start;
  }
  return ClampToRange(value, minimum, maximum);
}

static void pad_span(double& lo, double& hi, double min_span, bool widen_only_empty) {
  double span = hi - lo;
  if (widen_only_empty ? span <= 0 : span < min_span) {
    const double centre = (lo + hi) * 0.5;
    lo = centre - min_span * 0.5;
    hi = centre + min_span * 0.5;
    span = min_span;
  }
  lo -= span * kFitPadding;
  hi += span * kFitPadding;
}

FitRect FitRange(double min_time, double max_time, double min_value, double max_value) {
  FitRect r{min_time, max_time, min_value, max_value};
  pad_span(r.min_time, r.max_time, kFitMinTimeSpan, false);
  pad_span(r.min_value, r.max_value, kFitMinValueSpan, true);
  return r;
}

}  // namespace amber::graph
