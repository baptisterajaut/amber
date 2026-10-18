#ifndef CORE_AUDIODSP_H
#define CORE_AUDIODSP_H

#include <QtGlobal>

#include <vector>

namespace amber {
namespace audiodsp {

// DSP cores of the built-in audio effects (EQ, Compressor, Limiter, Delay). Pure, no widgets, no clip: each works in
// place on interleaved float stereo frames in [-1, 1) and keeps the state of one stream. The effect classes own one
// instance per stream (effects/internal/audiostreamstate.h) and convert from and to the cacher's S16.

// Interleaved S16 stereo (4 bytes per frame, little endian) to float stereo, x / 32768
void s16_to_float(const quint8* in, float* out, int frames);
// Back to S16, rounded and saturated at the int16 range
void float_to_s16(const float* in, quint8* out, int frames);

// RBJ Audio EQ Cookbook biquad, normalised (a0 = 1)
struct BiquadCoeffs {
  double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
};

// Shelves use slope S = 1. Frequencies are clamped to [10, 0.45 * rate].
BiquadCoeffs low_shelf(double freq, double gain_db, int rate);
BiquadCoeffs peaking(double freq, double gain_db, double q, int rate);
BiquadCoeffs high_shelf(double freq, double gain_db, int rate);

// Transposed Direct Form II, one state per channel. set() keeps the state, so a parameter change doesn't click.
class Biquad {
 public:
  void set(const BiquadCoeffs& c) { c_ = c; }
  void reset();
  void process(float* frames, int n);

 private:
  BiquadCoeffs c_;
  double s1_[2] = {0, 0};
  double s2_[2] = {0, 0};
};

// Feed-forward compressor, hard knee, stereo-linked peak detector (max(|L|, |R|)). The detector follows peaks at
// once and falls back with the release time; the gain reduction it asks for is smoothed with the attack (more
// reduction) and release (less) times, then the makeup gain is applied.
class Compressor {
 public:
  void set(double threshold_db, double ratio, double attack_ms, double release_ms, double makeup_db, int rate);
  void reset() { level_ = reduction_db_ = 0; }
  void process(float* frames, int n);

 private:
  double threshold_db_ = 0;
  double ratio_ = 1;
  double attack_coef_ = 0;
  double release_coef_ = 0;
  double makeup_db_ = 0;
  double level_ = 0;         // detector, linear peak
  double reduction_db_ = 0;  // smoothed gain reduction, <= 0
};

// Peak limiter with instant attack: the output never exceeds the ceiling. The gain recovers toward 1 with the release
// time.
// CBA: no lookahead; transients are hard-limited (slight distortion on very fast peaks)
class Limiter {
 public:
  void set(double ceiling_db, double release_ms, int rate);
  void reset() { gain_ = 1; }
  void process(float* frames, int n);

 private:
  double ceiling_ = 1;
  double release_coef_ = 0;
  double gain_ = 1;
};

// Stereo feedback delay, integer-sample delay time. The ring buffer holds kMaxSeconds at the stream's rate and is
// allocated by reset().
class DelayLine {
 public:
  static constexpr int kMaxSeconds = 2;
  static constexpr double kMaxFeedback = 0.95;  // the echoes always die out
  // Empties the buffer; reallocates only when the rate changes
  void reset(int rate);
  // mix in [0, 1], feedback in [0, kMaxFeedback]
  void set(double time_ms, double feedback, double mix, int rate);
  void process(float* frames, int n);

 private:
  std::vector<float> buffer_;  // interleaved stereo, capacity_ frames
  int capacity_ = 0;
  int write_ = 0;
  int delay_ = 1;
  float feedback_ = 0;
  float mix_ = 0;
};

}  // namespace audiodsp
}  // namespace amber

#endif  // CORE_AUDIODSP_H
