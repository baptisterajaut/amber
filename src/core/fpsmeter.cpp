#include "core/fpsmeter.h"

namespace amber {

void FpsMeter::add_frame(qint64 t_ms) {
  if (first_ms_ < 0) first_ms_ = t_ms;
  stamps_.push_back(t_ms);
  while (stamps_.front() < t_ms - kFpsWindowMs) stamps_.pop_front();
}

bool FpsMeter::rate(double* fps) const {
  if (stamps_.size() < 2) return false;
  const qint64 span = stamps_.back() - stamps_.front();
  if (span <= 0) return false;
  *fps = double(stamps_.size() - 1) * 1000.0 / double(span);
  return true;
}

bool FpsMeter::window_full() const { return !stamps_.empty() && stamps_.back() - first_ms_ >= kFpsWindowMs; }

void FpsMeter::reset() {
  stamps_.clear();
  first_ms_ = -1;
}

bool below_target(double fps, double target) { return target > 0 && fps < target * 0.95; }

}  // namespace amber
