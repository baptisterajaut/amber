#include "audio.h"

#include <QtMath>

qint8 audio_ibuffer[audio_ibuffer_size];
qint64 audio_ibuffer_read = 0;
long audio_ibuffer_frame = 0;
double audio_ibuffer_timecode = 0;
std::atomic<bool> audio_scrub{false};
bool audio_rendering = false;
int audio_rendering_rate = 0;
QMutex audio_write_lock;

double log_volume(double linear) {
  // expects a value between 0 and 1 (or more if amplifying)
  return (qExp(linear) - 1) / (M_E - 1);
}
