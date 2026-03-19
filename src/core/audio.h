#ifndef CORE_AUDIO_H
#define CORE_AUDIO_H

#include <QMutex>
#include <atomic>

double log_volume(double linear);

constexpr int audio_ibuffer_size = 192000;
extern qint8 audio_ibuffer[audio_ibuffer_size];
extern qint64 audio_ibuffer_read;
extern long audio_ibuffer_frame;
extern double audio_ibuffer_timecode;
extern std::atomic<bool> audio_scrub;
extern bool audio_rendering;
extern int audio_rendering_rate;
extern QMutex audio_write_lock;

#endif  // CORE_AUDIO_H
