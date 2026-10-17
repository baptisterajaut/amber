#include "core/fpsmeter.h"

namespace amber {

void FpsMeter::add_frame(qint64 t_ms) {
  if (first_ms_ < 0) first_ms_ = t_ms;
  if (!stamps_.empty()) prev_ms_ = stamps_.back();
  stamps_.push_back(t_ms);
  while (stamps_.front() < t_ms - kFpsWindowMs) stamps_.pop_front();
}

bool FpsMeter::rate(double* fps) const {
  if (stamps_.empty()) return false;
  if (stamps_.size() == 1) {
    // The previous frame fell out of the window: the gap itself is the rate (below 1 fps)
    if (prev_ms_ < 0) return false;
    *fps = 1000.0 / double(stamps_.back() - prev_ms_);
    return true;
  }
  const qint64 span = stamps_.back() - stamps_.front();
  if (span <= 0) return false;
  *fps = double(stamps_.size() - 1) * 1000.0 / double(span);
  return true;
}

bool FpsMeter::window_full() const { return !stamps_.empty() && stamps_.back() - first_ms_ >= kFpsWindowMs; }

void FpsMeter::reset() {
  stamps_.clear();
  first_ms_ = -1;
  prev_ms_ = -1;
}

bool below_target(double fps, double target) { return target > 0 && fps < target * 0.95; }

}  // namespace amber
