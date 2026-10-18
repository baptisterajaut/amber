#include "audiodsp.h"

#include <QtMath>

#include <algorithm>
#include <cmath>

namespace amber {
namespace audiodsp {

namespace {

// One-pole smoothing coefficient for a time constant of t_ms at rate
double time_coef(double t_ms, int rate) {
  const double samples = t_ms * 0.001 * rate;
  return samples > 0 ? std::exp(-1.0 / samples) : 0.0;
}

// The decaying tails of filter memories, envelopes and echoes go denormal in silence, which is slow on x86: flush
// them to 0 well before (no FTZ/DAZ, the flags are per thread and would leak into the rest of the cacher)
constexpr double kDenormalFloor = 1e-20;
double flush_denormal(double v) { return std::fabs(v) < kDenormalFloor ? 0.0 : v; }

double clamp_freq(double freq, int rate) { return std::clamp(freq, 10.0, 0.45 * rate); }

BiquadCoeffs normalise(double b0, double b1, double b2, double a0, double a1, double a2) {
  BiquadCoeffs c;
  c.b0 = b0 / a0;
  c.b1 = b1 / a0;
  c.b2 = b2 / a0;
  c.a1 = a1 / a0;
  c.a2 = a2 / a0;
  return c;
}

}  // namespace

void s16_to_float(const quint8* in, float* out, int frames) {
  for (int i = 0; i < frames * 2; i++) {
    const qint16 s = qint16((in[i * 2 + 1] << 8) | in[i * 2]);
    out[i] = float(s) / 32768.0f;
  }
}

void float_to_s16(const float* in, quint8* out, int frames) {
  for (int i = 0; i < frames * 2; i++) {
    const long v = std::clamp(std::lrint(double(in[i]) * 32768.0), -32768L, 32767L);
    out[i * 2] = quint8(v & 0xFF);
    out[i * 2 + 1] = quint8((v >> 8) & 0xFF);
  }
}

BiquadCoeffs low_shelf(double freq, double gain_db, int rate) {
  const double A = std::pow(10.0, gain_db / 40.0);
  const double w0 = 2.0 * M_PI * clamp_freq(freq, rate) / rate;
  const double cw = std::cos(w0);
  const double alpha = std::sin(w0) / 2.0 * std::sqrt(2.0);  // S = 1
  const double sa = 2.0 * std::sqrt(A) * alpha;
  return normalise(A * ((A + 1) - (A - 1) * cw + sa), 2 * A * ((A - 1) - (A + 1) * cw),
                   A * ((A + 1) - (A - 1) * cw - sa), (A + 1) + (A - 1) * cw + sa, -2 * ((A - 1) + (A + 1) * cw),
                   (A + 1) + (A - 1) * cw - sa);
}

BiquadCoeffs peaking(double freq, double gain_db, double q, int rate) {
  const double A = std::pow(10.0, gain_db / 40.0);
  const double w0 = 2.0 * M_PI * clamp_freq(freq, rate) / rate;
  const double cw = std::cos(w0);
  const double alpha = std::sin(w0) / (2.0 * std::max(q, 0.01));
  return normalise(1 + alpha * A, -2 * cw, 1 - alpha * A, 1 + alpha / A, -2 * cw, 1 - alpha / A);
}

BiquadCoeffs high_shelf(double freq, double gain_db, int rate) {
  const double A = std::pow(10.0, gain_db / 40.0);
  const double w0 = 2.0 * M_PI * clamp_freq(freq, rate) / rate;
  const double cw = std::cos(w0);
  const double alpha = std::sin(w0) / 2.0 * std::sqrt(2.0);  // S = 1
  const double sa = 2.0 * std::sqrt(A) * alpha;
  return normalise(A * ((A + 1) + (A - 1) * cw + sa), -2 * A * ((A - 1) + (A + 1) * cw),
                   A * ((A + 1) + (A - 1) * cw - sa), (A + 1) - (A - 1) * cw + sa, 2 * ((A - 1) - (A + 1) * cw),
                   (A + 1) - (A - 1) * cw - sa);
}

void Biquad::reset() {
  s1_[0] = s1_[1] = 0;
  s2_[0] = s2_[1] = 0;
}

void Biquad::process(float* frames, int n) {
  for (int i = 0; i < n; i++) {
    for (int ch = 0; ch < 2; ch++) {
      const double x = frames[i * 2 + ch];
      const double y = c_.b0 * x + s1_[ch];
      s1_[ch] = flush_denormal(c_.b1 * x - c_.a1 * y + s2_[ch]);
      s2_[ch] = flush_denormal(c_.b2 * x - c_.a2 * y);
      frames[i * 2 + ch] = float(y);
    }
  }
}

void Compressor::set(double threshold_db, double ratio, double attack_ms, double release_ms, double makeup_db,
                     int rate) {
  threshold_db_ = threshold_db;
  ratio_ = std::max(ratio, 1.0);
  attack_coef_ = time_coef(attack_ms, rate);
  release_coef_ = time_coef(release_ms, rate);
  makeup_db_ = makeup_db;
}

void Compressor::process(float* frames, int n) {
  for (int i = 0; i < n; i++) {
    const double peak = std::max(std::fabs(frames[i * 2]), std::fabs(frames[i * 2 + 1]));
    level_ = std::max(peak, release_coef_ * level_);
    if (level_ < 1e-12) level_ = 0;
    const double in_db = 20.0 * std::log10(std::max(level_, 1e-9));
    const double target = in_db < threshold_db_ ? 0.0 : threshold_db_ + (in_db - threshold_db_) / ratio_ - in_db;
    const double coef = target < reduction_db_ ? attack_coef_ : release_coef_;
    reduction_db_ = coef * reduction_db_ + (1.0 - coef) * target;
    if (reduction_db_ > -1e-12) reduction_db_ = 0;
    const float gain = float(std::pow(10.0, (reduction_db_ + makeup_db_) / 20.0));
    frames[i * 2] *= gain;
    frames[i * 2 + 1] *= gain;
  }
}

void Limiter::set(double ceiling_db, double release_ms, int rate) {
  ceiling_ = std::min(1.0, std::pow(10.0, ceiling_db / 20.0));
  release_coef_ = time_coef(release_ms, rate);
}

void Limiter::process(float* frames, int n) {
  const float ceiling = float(ceiling_);
  for (int i = 0; i < n; i++) {
    const double peak = std::max(std::fabs(frames[i * 2]), std::fabs(frames[i * 2 + 1]));
    const double target = peak > ceiling_ ? ceiling_ / peak : 1.0;
    // Recover toward 1, but never above what this sample allows
    gain_ = std::min(target, 1.0 - release_coef_ * (1.0 - gain_));
    for (int ch = 0; ch < 2; ch++) {
      frames[i * 2 + ch] = std::clamp(float(frames[i * 2 + ch] * gain_), -ceiling, ceiling);
    }
  }
}

void DelayLine::reset(int rate) {
  // A seek or a scrub grain resets at the same rate: clear the buffer in place rather than reallocate 2 s of audio
  const int capacity = kMaxSeconds * rate + 1;
  if (capacity == capacity_) {
    std::fill(buffer_.begin(), buffer_.end(), 0.0f);
  } else {
    capacity_ = capacity;
    buffer_.assign(size_t(capacity_) * 2, 0.0f);
  }
  write_ = 0;
}

void DelayLine::set(double time_ms, double feedback, double mix, int rate) {
  delay_ = int(std::lround(time_ms * rate / 1000.0));
  delay_ = std::clamp(delay_, 1, std::max(1, capacity_ - 1));
  feedback_ = float(std::clamp(feedback, 0.0, kMaxFeedback));
  mix_ = float(std::clamp(mix, 0.0, 1.0));
}

void DelayLine::process(float* frames, int n) {
  if (capacity_ < 2) return;
  for (int i = 0; i < n; i++) {
    const int read = (write_ - delay_ + capacity_) % capacity_;
    for (int ch = 0; ch < 2; ch++) {
      const float dry = frames[i * 2 + ch];
      const float delayed = buffer_[size_t(read) * 2 + ch];
      frames[i * 2 + ch] = dry * (1.0f - mix_) + delayed * mix_;
      buffer_[size_t(write_) * 2 + ch] = float(flush_denormal(dry + delayed * feedback_));
    }
    write_ = (write_ + 1) % capacity_;
  }
}

namespace {

// Freeverb's tuning.h, at 44.1 kHz
constexpr int kCombTuning[Reverb::kCombs] = {1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617};
constexpr int kAllpassTuning[Reverb::kAllpasses] = {556, 441, 341, 225};
constexpr int kStereoSpread = 23;  // the right channel's lines are this much longer
constexpr float kFixedGain = 0.015f;
// Freeverb's default wet gain, initialwet 1/3 * scalewet 3. Its full scale (3) puts a 100 % wet default room 3 to 6 dB
// above the dry signal on white noise (peaks 4 to 6 at full scale); at 1 it sits 4 to 7 dB below it.
constexpr double kWetGain = 1;
constexpr double kScaleDamp = 0.4;
constexpr double kScaleRoom = 0.28;
constexpr double kOffsetRoom = 0.7;
constexpr float kAllpassFeedback = 0.5f;

// Allocates a zeroed line of len frames, or zeroes it in place when it already has that length
void clear_line(std::vector<float>& buffer, int len) {
  if (int(buffer.size()) == len) {
    std::fill(buffer.begin(), buffer.end(), 0.0f);
  } else {
    buffer.assign(size_t(len), 0.0f);
  }
}

}  // namespace

void Reverb::reset(int rate) {
  const double scale = rate / 44100.0;
  for (int ch = 0; ch < 2; ch++) {
    const int spread = ch ? kStereoSpread : 0;
    for (int k = 0; k < kCombs; k++) {
      clear_line(combs_[ch][k].buffer, std::max(1, int(std::lround((kCombTuning[k] + spread) * scale))));
      combs_[ch][k].index = 0;
      combs_[ch][k].store = 0;
    }
    for (int k = 0; k < kAllpasses; k++) {
      clear_line(allpasses_[ch][k].buffer, std::max(1, int(std::lround((kAllpassTuning[k] + spread) * scale))));
      allpasses_[ch][k].index = 0;
    }
  }
  rate_ = rate;
}

void Reverb::set(double room, double damp, double width, double mix) {
  feedback_ = float(std::clamp(room, 0.0, 1.0) * kScaleRoom + kOffsetRoom);
  damp_ = float(std::clamp(damp, 0.0, 1.0) * kScaleDamp);
  width = std::clamp(width, 0.0, 1.0);
  wet1_ = float(kWetGain * (width / 2 + 0.5));
  wet2_ = float(kWetGain * ((1 - width) / 2));
  mix_ = float(std::clamp(mix, 0.0, 1.0));
}

void Reverb::process(float* frames, int n) {
  if (rate_ == 0) return;
  for (int i = 0; i < n; i++) {
    const float input = (frames[i * 2] + frames[i * 2 + 1]) * kFixedGain;
    float tank[2];
    for (int ch = 0; ch < 2; ch++) {
      float out = 0;
      for (Comb& c : combs_[ch]) {
        const float y = c.buffer[size_t(c.index)];
        c.store = float(flush_denormal(y * (1.0f - damp_) + c.store * damp_));
        c.buffer[size_t(c.index)] = float(flush_denormal(input + c.store * feedback_));
        if (++c.index == int(c.buffer.size())) c.index = 0;
        out += y;
      }
      for (Allpass& a : allpasses_[ch]) {
        const float b = a.buffer[size_t(a.index)];
        a.buffer[size_t(a.index)] = float(flush_denormal(out + b * kAllpassFeedback));
        if (++a.index == int(a.buffer.size())) a.index = 0;
        out = b - out;
      }
      tank[ch] = out;
    }
    const float wet_l = tank[0] * wet1_ + tank[1] * wet2_;
    const float wet_r = tank[1] * wet1_ + tank[0] * wet2_;
    frames[i * 2] = frames[i * 2] * (1.0f - mix_) + wet_l * mix_;
    frames[i * 2 + 1] = frames[i * 2 + 1] * (1.0f - mix_) + wet_r * mix_;
  }
}

void Chorus::reset(int rate) {
  const int capacity = int(std::ceil(kBufferMs * rate / 1000.0)) + 2;
  if (capacity == capacity_) {
    std::fill(buffer_.begin(), buffer_.end(), 0.0f);
  } else {
    capacity_ = capacity;
    buffer_.assign(size_t(capacity_) * 2, 0.0f);
  }
  write_ = 0;
  phase_ = 0;
}

void Chorus::set(double rate_hz, double depth_ms, double mix, int rate) {
  phase_step_ = 2.0 * M_PI * std::max(rate_hz, 0.0) / rate;
  base_ = kBaseMs * rate / 1000.0;
  depth_ = std::clamp(depth_ms, 0.0, kMaxDepthMs) * rate / 1000.0;
  mix_ = float(std::clamp(mix, 0.0, 1.0));
}

void Chorus::process(float* frames, int n) {
  if (capacity_ < 2) return;
  const double max_delay = capacity_ - 2;
  for (int i = 0; i < n; i++) {
    for (int ch = 0; ch < 2; ch++) {
      const float dry = frames[i * 2 + ch];
      buffer_[size_t(write_) * 2 + ch] = dry;
      // Right channel a quarter turn ahead: the two sweeps never line up, which is the stereo width
      const double lfo = (1.0 + std::sin(phase_ + (ch ? M_PI / 2 : 0.0))) / 2.0;
      const double delay = std::min(base_ + depth_ * lfo, max_delay);
      const int whole = int(delay);
      const float frac = float(delay - whole);
      const int a = (write_ - whole + capacity_) % capacity_;
      const int b = (a - 1 + capacity_) % capacity_;
      const float delayed = buffer_[size_t(a) * 2 + ch] * (1.0f - frac) + buffer_[size_t(b) * 2 + ch] * frac;
      frames[i * 2 + ch] = dry * (1.0f - mix_) + delayed * mix_;
    }
    write_ = (write_ + 1) % capacity_;
    phase_ += phase_step_;
    if (phase_ >= 2.0 * M_PI) phase_ -= 2.0 * M_PI;
  }
}

}  // namespace audiodsp
}  // namespace amber
