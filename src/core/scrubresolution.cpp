#include "core/scrubresolution.h"

#include <QtGlobal>

namespace amber {

int ScrubResolution::divider(int base, ScrubHalfRes mode, bool scrub_render, int short_side) const {
  base = qMax(1, base);
  if (!scrub_render) return base;
  bool half = mode == ScrubHalfRes::Always || (mode == ScrubHalfRes::Auto && reduced_);
  if (!half) return base;
  int d = qMin(base * 2, 8);
  if (d <= base || short_side / d < 64) return base;
  return d;
}

void ScrubResolution::record_frame(quint64 serial, double gpu_ms, int rendered_divider, int base,
                                   double frame_rate) {
  if (serial == 0 || serial == last_serial_) return;
  last_serial_ = serial;
  if (reduced_ || rendered_divider != qMax(1, base) || frame_rate <= 0) return;
  if (gpu_ms > 1000.0 / frame_rate) {
    if (++slow_count_ >= 2) reduced_ = true;
  } else {
    slow_count_ = 0;
  }
}

void ScrubResolution::end_gesture() {
  reduced_ = false;
  slow_count_ = 0;
}

}  // namespace amber
