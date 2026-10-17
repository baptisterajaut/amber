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
// Export in progress, video and audio alike: set for the whole export by prepare_export()/finish_export() through
// ProjectIO::setRenderingState(). Clips opened while it is set decode the original media instead of the proxy.
extern std::atomic<bool> audio_rendering;
extern std::atomic<int> audio_rendering_rate;
extern QMutex audio_write_lock;

#endif  // CORE_AUDIO_H
