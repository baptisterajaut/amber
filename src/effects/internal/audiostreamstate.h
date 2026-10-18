#ifndef AUDIOSTREAMSTATE_H
#define AUDIOSTREAMSTATE_H

#include <QHash>
#include <QMutex>
#include <QMutexLocker>

#include <cmath>
#include <memory>

namespace amber {

// Which audio stream an Effect::process_audio() call belongs to, and the rate its samples play at. Set by the
// outermost apply_audio_effects() call (engine/cacher_audio.cpp) for the whole chain, nest clips included: stream is
// the leaf clip, so an effect on a nested-sequence clip sees each inner clip as its own stream. Each clip decodes on
// its own cacher thread, hence thread_local.
struct AudioProcessContext {
  const void* stream = nullptr;
  int sample_rate = 48000;
};

namespace detail {
inline thread_local const AudioProcessContext* audio_context = nullptr;
}  // namespace detail

// The context of the current process_audio() call. Outside the cacher (tests calling process_audio() directly) it is
// stream nullptr at 48000 Hz.
inline const AudioProcessContext& current_audio_context() {
  static const AudioProcessContext kNone;
  return detail::audio_context ? *detail::audio_context : kNone;
}

// RAII: installs ctx for this thread unless a context is already set (the nest recursion keeps the leaf's)
class AudioProcessScope {
 public:
  explicit AudioProcessScope(const AudioProcessContext* ctx) : installed_(detail::audio_context == nullptr) {
    if (installed_) detail::audio_context = ctx;
  }
  ~AudioProcessScope() {
    if (installed_) detail::audio_context = nullptr;
  }
  AudioProcessScope(const AudioProcessScope&) = delete;
  AudioProcessScope& operator=(const AudioProcessScope&) = delete;

 private:
  bool installed_;
};

// Per-stream state of a stateful audio effect (filter memory, envelope, delay buffer). One Effect instance can see
// several interleaved streams (the inner clips of a nested sequence, each on its own thread), so state kept per
// instance would mix them. acquire() returns the current stream's state and sets *reset when the stream is new, when
// its timecode jumps (seek, scrub grain, the rewind at playback start) or when its rate changes. A new stream's state
// is T{}; on a reset the caller restarts its state itself, so a delay line can keep its buffer.
// CBA: nested clips are processed per inner stream; true bus effects need a nest mixdown
//
// The cacher's timecode_end isn't used: it is derived from the frame's own rate, which is the source rate before a
// clip's speed change (half the output rate at 2x) and 0 for a reversed clip. The stream's timecode_start advances at
// the output rate, so the expected next start is timecode_start + frames / sample_rate. A reversed clip's
// timecode_start also advances forward (measured: 39808-frame chunks, 0.829333 s apart), so it keeps its state.
template <typename T>
class StreamStates {
 public:
  static constexpr double kMaxGap = 0.002;  // seconds
  static constexpr int kMaxStreams = 64;

  // Held by the caller for the length of the chunk: an eviction from another thread can't free it mid-use
  std::shared_ptr<T> acquire(const AudioProcessContext& ctx, double timecode_start, int frames, bool* reset) {
    std::shared_ptr<Entry> e;
    {
      QMutexLocker locker(&mutex_);
      auto it = entries_.find(ctx.stream);
      if (it == entries_.end()) {
        // Deleted clips leave stale keys behind: past kMaxStreams, drop the least recently used one. A live stream
        // evicted that way (more than 64 at once) just starts over.
        // CBA: linear scan for the oldest entry, only on a new stream past 64
        if (entries_.size() >= kMaxStreams) {
          auto oldest = entries_.begin();
          for (auto o = entries_.begin(); o != entries_.end(); ++o) {
            if (o.value()->last_used < oldest.value()->last_used) oldest = o;
          }
          entries_.erase(oldest);
        }
        it = entries_.insert(ctx.stream, std::make_shared<Entry>());
      }
      it.value()->last_used = ++tick_;
      e = it.value();
    }
    // The stream's own thread is the only user of its entry from here
    *reset = !e->started || e->sample_rate != ctx.sample_rate || std::fabs(timecode_start - e->next_start) > kMaxGap;
    if (*reset) {
      e->started = true;
      e->sample_rate = ctx.sample_rate;
    }
    e->next_start = timecode_start + double(frames) / ctx.sample_rate;
    return std::shared_ptr<T>(e, &e->state);
  }

 private:
  struct Entry {
    T state{};
    bool started = false;
    double next_start = 0;
    int sample_rate = 0;
    quint64 last_used = 0;  // under mutex_
  };
  QMutex mutex_;
  quint64 tick_ = 0;
  QHash<const void*, std::shared_ptr<Entry>> entries_;
};

}  // namespace amber

#endif  // AUDIOSTREAMSTATE_H
