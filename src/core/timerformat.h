#ifndef TIMERFORMAT_H
#define TIMERFORMAT_H

#include <QString>

namespace amber {

// Display formats of the Timer effect. Values are saved in projects (Format combo data): append only.
enum class TimerFormat { MMSS = 0, HHMMSS = 1, Seconds = 2, MMSSHundredths = 3 };

// Frames since the clip's visible start. `timecode` is what the renderer passes to effects, i.e.
// (max(0, playhead - timeline_in(true)) + clip_in(true)) / fps: the opening transition length cancels out once
// the non-transition `clip_in` is subtracted. Clamped to [0, last_frame] (0 during an opening transition,
// last_frame during a closing one).
long clip_elapsed_frames(double timecode, double fps, long clip_in, long last_frame);

// Seconds shown by the timer. Count up: offset + elapsed. Count down: offset + time left until the last frame,
// so the countdown shows its full value on the first frame and `offset` (0 by default) on the last one.
double timer_seconds(bool countdown, long elapsed_frames, long last_frame, double fps, double offset);

// Formats `seconds` rounded to the displayed unit: down for a count up, up (`round_up`) for a count down, so a
// countdown shows 1 until it actually reaches 0. A 1e-6 s epsilon absorbs float error at unit boundaries.
// Negative values: rounded signed, then drawn as "-" + the absolute value. MM:SS lets minutes exceed 59.
QString format_timer(double seconds, TimerFormat f, bool round_up);

// Progress of a clip, 0 on its first frame and 1 on its last (1 for a one-frame clip). Clamped to [0, 1].
double progress_fraction(long elapsed_frames, long last_frame);

}  // namespace amber

#endif  // TIMERFORMAT_H
