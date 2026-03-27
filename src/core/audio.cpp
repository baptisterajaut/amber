#include "audio.h"

#include <QtMath>

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
