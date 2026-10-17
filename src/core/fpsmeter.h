#ifndef FPSMETER_H
#define FPSMETER_H

#include <QtGlobal>

#include <deque>

namespace amber {

// Averaging window of the viewer's frame rate overlay
constexpr qint64 kFpsWindowMs = 1000;

// Rate at which the viewer displays new frames, over a sliding window. Pure logic: the viewer passes in a
// monotonic timestamp (ms) per displayed frame and owns the clock. One instance per viewer.
class FpsMeter {
 public:
  // Records a displayed frame and drops the frames more than kFpsWindowMs older than it
  void add_frame(qint64 t_ms);

  // (n - 1) / (t_last - t_first) over the window, in frames per second. A frame alone in the window after a
  // previous one (frames more than a window apart: slow playback) gives 1000 / gap instead, which window_full()
  // then covers too. False (rate untouched) with no previous frame, or 2 frames at the same millisecond.
  bool rate(double* fps) const;

  // True once the frames since the last reset span a whole window: until then the rate covers less than a
  // second and says nothing about whether playback keeps up
  bool window_full() const;

  // Playback stopped: the next play starts from an empty window
  void reset();

 private:
  std::deque<qint64> stamps_;
  qint64 first_ms_{-1};  // first frame since the last reset
  qint64 prev_ms_{-1};   // frame before the last one, kept when the prune drops it
};

// The overlay's red threshold: a measured rate below 95% of the target rate. False for a target <= 0.
bool below_target(double fps, double target);

}  // namespace amber

#endif  // FPSMETER_H
