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

}  // namespace amber::graph
