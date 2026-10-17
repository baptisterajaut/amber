#ifndef SCRUBRESOLUTION_H
#define SCRUBRESOLUTION_H

#include <QtGlobal>

namespace amber {

// Preference "Half resolution while scrubbing" (config ScrubHalfResolution)
enum class ScrubHalfRes { Auto = 0, Always = 1, Never = 2 };

// Delay after the last scrub render before the frame is re-rendered at the base divider
constexpr int kScrubSettleMs = 150;

// Decides the preview divider of scrub renders. Pure logic: the viewer passes in the facts (mode, base
// divider, measured frame times) and owns the timers. One instance per viewer.
class ScrubResolution {
 public:
  // Divider for the next render. Non-scrub renders always get `base`. A reduced divider is base * 2 capped at
  // 8, and never makes the frame's short side smaller than 64 px (`short_side` = the sequence's smaller side).
  int divider(int base, ScrubHalfRes mode, bool scrub_render, int short_side) const;

  // Feeds one measured frame (GPU + readback ms) of the current gesture. A serial already seen is ignored
  // (requests are coalesced, so the same measurement can be read twice). Only frames rendered at the base
  // divider count: two consecutive ones over the frame budget (1000 / frame_rate ms) latch reduced() for the
  // rest of the gesture; a frame within budget resets the count.
  void record_frame(quint64 serial, double gpu_ms, int rendered_divider, int base, double frame_rate);

  // Marks a serial as seen without counting it: called when a gesture starts, so the record of a frame
  // rendered before the gesture (the previous settle render, a Save Frame) is never taken as a scrub frame.
  void skip_serial(quint64 serial) { last_serial_ = serial; }

  // The scrub gesture ended (settle): clears the latch and the slow-frame count.
  void end_gesture();

  bool reduced() const { return reduced_; }

 private:
  bool reduced_{false};
  int slow_count_{0};
  quint64 last_serial_{0};
};

}  // namespace amber

#endif  // SCRUBRESOLUTION_H
