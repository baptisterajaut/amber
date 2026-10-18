#include "audio.h"

#include <QtMath>
#include <cstdint>

qint8 audio_ibuffer[audio_ibuffer_size];
std::atomic<qint64> audio_ibuffer_read{0};
std::atomic<long> audio_ibuffer_frame{0};
std::atomic<unsigned> audio_scrub_id{0};
std::atomic<bool> audio_scrub_data_ready{false};

int scrub_grain_samples(int sample_rate) { return (sample_rate * 80 + 999) / 1000; }

int scrub_grain_bytes(int sample_rate) { return scrub_grain_samples(sample_rate) * 4; }
std::atomic<bool> audio_rendering{false};
std::atomic<int> audio_rendering_rate{0};
QMutex audio_write_lock;

double log_volume(double linear) {
  // expects a value between 0 and 1 (or more if amplifying)
  return (qExp(linear) - 1) / (M_E - 1);
}

static qint16 scale_sample(qint16 sample, double gain) {
  // truncates toward zero, as VolumeEffect
  const qint32 scaled = qint32(sample * gain);
  return qint16(qBound<qint32>(INT16_MIN, scaled, INT16_MAX));
}

void amber::apply_stereo_gain(quint8* samples, int nb_bytes, double gain_l, double gain_r) {
  if (gain_l == 1.0 && gain_r == 1.0) return;
  for (int i = 0; i + 3 < nb_bytes; i += 4) {
    const qint16 left = scale_sample(qint16(((samples[i + 1] & 0xFF) << 8) | (samples[i] & 0xFF)), gain_l);
    const qint16 right = scale_sample(qint16(((samples[i + 3] & 0xFF) << 8) | (samples[i + 2] & 0xFF)), gain_r);
    samples[i] = quint8(left);
    samples[i + 1] = quint8(left >> 8);
    samples[i + 2] = quint8(right);
    samples[i + 3] = quint8(right >> 8);
  }
}
