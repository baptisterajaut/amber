#include "graphmath.h"

#include <cmath>
#include <cstdlib>

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

long AvoidKeyCollisions(long diff, long previous_diff, const QVector<long>& old_times,
                        const QVector<QVector<long>>& fixed_times) {
  const long step = (previous_diff > diff) ? 1 : -1;
  const int n = qMin(old_times.size(), fixed_times.size());
  int fixed_count = 0;
  for (int i = 0; i < n; i++) fixed_count += fixed_times.at(i).size();
  // Each step clears at least one collision for good in that direction, so this ends within fixed_count steps
  for (int guard = 0; guard <= fixed_count; guard++) {
    bool hit = false;
    for (int i = 0; i < n && !hit; i++) hit = fixed_times.at(i).contains(old_times.at(i) + diff);
    if (!hit) break;
    diff += step;
  }
  return diff;
}

bool SnapKeyDrag(long& diff, const QVector<long>& old_times, const QVector<QVector<long>>& targets, double range,
                 long* target) {
  bool found = false;
  long best_shift = 0;
  long best_target = 0;
  const int n = qMin(old_times.size(), targets.size());
  for (int i = 0; i < n; i++) {
    const long t = old_times.at(i) + diff;
    for (long candidate : targets.at(i)) {
      const long shift = candidate - t;
      if (std::abs(double(shift)) <= range && (!found || std::abs(shift) < std::abs(best_shift))) {
        found = true;
        best_shift = shift;
        best_target = candidate;
      }
    }
  }
  if (found) {
    diff += best_shift;
    if (target) *target = best_target;
  }
  return found;
}

double ValueGridStep(double px_per_unit) {
  if (!(px_per_unit > 0) || std::isinf(px_per_unit)) return 1.0;
  const double min_step = kValueGridMinPx / px_per_unit;
  const double magnitude = std::pow(10.0, std::floor(std::log10(min_step)));
  for (double m : {1.0, 2.0, 5.0}) {
    if (m * magnitude >= min_step * (1 - 1e-9)) return m * magnitude;
  }
  return 10.0 * magnitude;
}

int StepDecimals(double step) {
  if (!(step > 0) || step >= 1) return 0;
  return int(std::ceil(-std::log10(step) - 1e-9));
}

long TimeGridStep(double px_per_frame, double frame_rate) {
  const long second = qMax(1L, long(qRound(frame_rate)));
  QVector<long> steps;
  for (long f : {1, 2, 5, 10, 20, 50, 100, 200, 500}) {
    if (f < second) steps.append(f);
  }
  for (long s : {1, 2, 5, 10, 15, 30, 60, 120, 300, 600, 900, 1800, 3600}) steps.append(s * second);
  if (!(px_per_frame > 0)) return steps.last();
  for (long step : steps) {
    if (double(step) * px_per_frame >= kTimeGridMinPx) return step;
  }
  long step = steps.last();
  while (double(step) * px_per_frame < kTimeGridMinPx && step < (1L << 40)) step *= 2;
  return step;
}

FitRect FitRange(double min_time, double max_time, double min_value, double max_value) {
  FitRect r{min_time, max_time, min_value, max_value};
  pad_span(r.min_time, r.max_time, kFitMinTimeSpan, false);
  pad_span(r.min_value, r.max_value, kFitMinValueSpan, true);
  return r;
}

Handles EasyEase(const Handles& h, long time, std::optional<long> prev_time, std::optional<long> next_time,
                 EaseSide side) {
  Handles r = h;
  if (side != EaseSide::kOut) {
    if (prev_time) r.pre_x = -double(time - *prev_time) / 3.0;
    r.pre_y = 0;
  }
  if (side != EaseSide::kIn) {
    if (next_time) r.post_x = double(*next_time - time) / 3.0;
    r.post_y = 0;
  }
  return r;
}

}  // namespace amber::graph
