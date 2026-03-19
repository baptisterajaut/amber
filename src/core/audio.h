#ifndef CORE_AUDIO_H
#define CORE_AUDIO_H

#include <QMutex>
#include <atomic>

double log_volume(double linear);

constexpr int audio_ibuffer_size = 192000;
extern qint8 audio_ibuffer[audio_ibuffer_size];
extern std::atomic<qint64> audio_ibuffer_read;
extern std::atomic<long> audio_ibuffer_frame;
extern std::atomic<unsigned> audio_scrub_id;
extern std::atomic<bool> audio_scrub_data_ready;

int scrub_grain_samples(int sample_rate);
int scrub_grain_bytes(int sample_rate);
extern bool audio_rendering;
extern int audio_rendering_rate;
extern QMutex audio_write_lock;

#endif  // CORE_AUDIO_H
