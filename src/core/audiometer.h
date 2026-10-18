#ifndef AUDIOMETER_H
#define AUDIOMETER_H

#include <QVector>
#include <QtGlobal>

namespace amber {

// Bottom of the audio monitor's scale: silence (and anything quieter) reads this
constexpr double kMeterFloorDb = -60.0;
// Fall rate of the level and of the peak hold once released (Premiere-like peak meter ballistics)
constexpr double kMeterFallDbPerSec = 20.0;
// How long the peak hold stays put before falling
constexpr qint64 kMeterHoldMs = 1500;
// Linear peak that latches the clip indicator. Playback peaks are |s|/32768 of int16 samples, so a full-scale
// positive sample (+32767) reads 32767/32768, just under 1.0: it must count as 0 dBFS too.
constexpr double kMeterClipLinear = 32767.0 / 32768.0;

// Linear peak (0..1) to dBFS, kMeterFloorDb for 0 and anything below the floor
double meter_db(double linear);

// The audio monitor's per-channel ballistics: level, peak hold and clip latch. Pure logic: the widget passes in a
// monotonic timestamp (ms) and owns the clock and the lock.
// CBA: peak meter only; RMS/LUFS and per-track meters if asked
class AudioMeterState {
 public:
  // New linear peaks (0..1), one per channel. A level rises instantly to a higher peak, otherwise keeps falling toward
  // it; a peak >= kMeterClipLinear (0 dBFS) latches the channel's clip. A channel count change starts over from the
  // floor.
  void push(const QVector<double>& linear_peaks, qint64 now_ms);

  // Lets the levels and holds fall up to now_ms
  void advance(qint64 now_ms);

  // No more audio: pushes silence so the meters fall smoothly to the floor. Clip latches stay.
  void clear(qint64 now_ms);

  void reset_clip();

  int channel_count() const { return int(channels_.size()); }
  // Per channel read-outs in dBFS (kMeterFloorDb and false for a channel out of range)
  double level_db(int ch) const;
  double peak_hold_db(int ch) const;
  bool clipped(int ch) const;

  // True while a level or a hold is still above the floor: the widget keeps animating
  bool animating() const;

 private:
  struct Channel {
    double level{kMeterFloorDb};
    double hold{kMeterFloorDb};
    double target{kMeterFloorDb};  // last pushed value: the level never falls below it
    qint64 hold_ms{0};             // when the hold was last reached
    bool clip{false};
  };

  QVector<Channel> channels_;
  qint64 last_ms_{-1};
};

}  // namespace amber

#endif  // AUDIOMETER_H
