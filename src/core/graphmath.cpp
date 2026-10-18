#include "graphmath.h"

#include <cmath>

namespace amber::graph {

double ClampToRange(double value, double minimum, double maximum) {
  if (!std::isnan(minimum) && value < minimum) value = minimum;
  if (!std::isnan(maximum) && value > maximum) value = maximum;
  return value;
}

}  // namespace amber::graph
