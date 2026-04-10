/***

    Olive - Non-Linear Video Editor
    Copyright (C) 2019  Olive Team

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.

***/

#include "cacher.h"

#ifndef STDC_FORMAT_MACROS
// For some reason the Windows AppVeyor build fails to find PRIx64 without this definition and including
// <inttypes.h> Maybe something to do with the GCC version being used? Either way, that's why it's here.
#define STDC_FORMAT_MACROS 1
#endif

#include <cinttypes>

#include <QAudioSink>
#include <QDateTime>
#include <QFileInfo>
#include <QtMath>
#include <cmath>

#include "global/config.h"
#include "global/debug.h"
#include "project/projectelements.h"
#include "rendering/audio.h"
#include "rendering/renderfunctions.h"

const AVPixelFormat kDestPixFmt = AV_PIX_FMT_RGBA;
const AVSampleFormat kDestSampleFmt = AV_SAMPLE_FMT_S16;

void Cacher::SetRetrievedFrame(AVFrame* f) {
  retrieve_lock_.lock();
  if (retrieved_frame == nullptr) {
    retrieved_frame = f;
    retrieve_wait_.wakeAll();
  }
  retrieve_lock_.unlock();
}

void Cacher::WakeMainThread() {
  main_thread_lock_.lock();
  main_thread_woken_ = true;
  main_thread_wait_.wakeAll();
  main_thread_lock_.unlock();
}

Cacher::Cacher(Clip* c)
    : clip(c),

      is_valid_state_(false) {
  if (!c) {
    qWarning() << "Cacher::Cacher: clip is null";
  }
}

void Cacher::openWorkerToneClip() {
  if (!clip->sequence) {
    qWarning() << "Cacher::openWorkerToneClip: clip sequence is null";
    return;
  }
  frame_ = av_frame_alloc();
  frame_->format = kDestSampleFmt;
  av_channel_layout_from_mask(&frame_->ch_layout, clip->sequence->audio_layout);
  frame_->sample_rate = current_audio_freq();
  frame_->nb_samples = 2048;
  if (av_frame_get_buffer(frame_, 0)) {
    qCritical() << "Could not allocate buffer for tone clip";
  }
  audio_reset_ = true;
}

// ---------------------------------------------------------------------------
// openWorkerTryHwAccel — attempt hardware-accelerated decode setup
// ---------------------------------------------------------------------------
void Cacher::openWorkerTryHwAccel() {
  static const AVHWDeviceType hw_types[] = {
#if defined(__linux__)
      AV_HWDEVICE_TYPE_VAAPI,
#elif defined(_WIN32)
      AV_HWDEVICE_TYPE_D3D11VA,
#elif defined(__APPLE__)
      AV_HWDEVICE_TYPE_VIDEOTOOLBOX,
#endif
      AV_HWDEVICE_TYPE_NONE};

  for (int i = 0; hw_types[i] != AV_HWDEVICE_TYPE_NONE; i++) {
    if (av_hwdevice_ctx_create(&hw_device_ctx, hw_types[i], nullptr, nullptr, 0) == 0) {
      codecCtx->hw_device_ctx = av_buffer_ref(hw_device_ctx);
      qInfo() << "Hardware decoding enabled:" << av_hwdevice_get_type_name(hw_types[i]);
      return;
    }
  }
  qInfo() << "Hardware decoding unavailable, falling back to software";
}

// ---------------------------------------------------------------------------
// openWorkerOpenCodec
// ---------------------------------------------------------------------------
bool Cacher::openWorkerOpenCodec(Footage* m, const FootageStream* ms) {
  if (!m) {
    qWarning() << "Cacher::openWorkerOpenCodec: Footage is null";
    return false;
  }
  if (!ms) {
    qWarning() << "Cacher::openWorkerOpenCodec: FootageStream is null";
    return false;
  }

  // Resolve file path (proxy or original)
  QByteArray ba;
  if (m->proxy && !m->proxy_path.isEmpty() && QFileInfo::exists(m->proxy_path)) {
    ba = m->proxy_path.toUtf8();
  } else {
    ba = m->url.toUtf8();
  }
  const char* filename = ba.constData();

  // For image sequences that don't start at 0, set the index where it does start
  AVDictionary* format_opts = nullptr;
  if (m->start_number > 0) {
    av_dict_set(&format_opts, "start_number", QString::number(m->start_number).toUtf8(), 0);
  }

  formatCtx = nullptr;
  int errCode = avformat_open_input(&formatCtx, filename, nullptr, &format_opts);
  av_dict_free(&format_opts);
  if (errCode != 0) {
    char err[1024];
    av_strerror(errCode, err, 1024);
    qCritical() << "Could not open" << filename << "-" << err;
    return false;
  }

  errCode = avformat_find_stream_info(formatCtx, nullptr);
  if (errCode < 0) {
    char err[1024];
    av_strerror(errCode, err, 1024);
    qCritical() << "Could not open" << filename << "-" << err;
    avformat_close_input(&formatCtx);
    return false;
  }

  av_dump_format(formatCtx, 0, filename, 0);

  stream = formatCtx->streams[ms->file_index];
  codec = avcodec_find_decoder(stream->codecpar->codec_id);
  if (codec == nullptr) {
    qCritical() << "Could not find decoder for codec ID" << stream->codecpar->codec_id;
    avformat_close_input(&formatCtx);
    return false;
  }

  codecCtx = avcodec_alloc_context3(codec);
  if (codecCtx == nullptr) {
    qCritical() << "Could not allocate codec context";
    avformat_close_input(&formatCtx);
    return false;
  }
  avcodec_parameters_to_context(codecCtx, stream->codecpar);

  opts = nullptr;
  hw_device_ctx = nullptr;

  // Enable multithreading on decoding
  av_dict_set(&opts, "threads", "auto", 0);

  // Enable extra optimization code on h264
  if (stream->codecpar->codec_id == AV_CODEC_ID_H264) {
    av_dict_set(&opts, "tune", "fastdecode,zerolatency", 0);
  }

  // Try hardware-accelerated decoding if enabled and this is a video stream
  if (amber::CurrentConfig.hardware_decoding && stream->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
    openWorkerTryHwAccel();
  }

  // Open codec
  if (avcodec_open2(codecCtx, codec, &opts) < 0) {
    qCritical() << "Could not open codec";
    av_dict_free(&opts);
    avcodec_free_context(&codecCtx);
    avformat_close_input(&formatCtx);
    return false;
  }
  av_dict_free(&opts);

  return true;
}

void Cacher::openWorkerVideoFilter(const FootageStream* ms) {
  if (!ms) {
    qWarning() << "Cacher::openWorkerVideoFilter: FootageStream is null";
    return;
  }
  char filter_args[512];
  snprintf(filter_args, sizeof(filter_args), "video_size=%dx%d:pix_fmt=%d:time_base=%d/%d:pixel_aspect=%d/%d",
           stream->codecpar->width, stream->codecpar->height, stream->codecpar->format, stream->time_base.num,
           stream->time_base.den, stream->codecpar->sample_aspect_ratio.num, stream->codecpar->sample_aspect_ratio.den);

  bool ok = true;

  if (avfilter_graph_create_filter(&buffersrc_ctx, avfilter_get_by_name("buffer"), "in", filter_args, nullptr,
                                   filter_graph) < 0) {
    qCritical() << "Could not create video buffer source";
    ok = false;
  }

  if (ok && avfilter_graph_create_filter(&buffersink_ctx, avfilter_get_by_name("buffersink"), "out", nullptr, nullptr,
                                         filter_graph) < 0) {
    qCritical() << "Could not create video buffer sink";
    ok = false;
  }

  AVFilterContext* last_filter = buffersrc_ctx;

  char yadif_args[100];
  if (ok && ms->video_interlacing != VIDEO_PROGRESSIVE) {
    AVFilterContext* yadif_filter;
    snprintf(yadif_args, sizeof(yadif_args), "mode=3:parity=%d",
             ((ms->video_interlacing == VIDEO_TOP_FIELD_FIRST) ? 0 : 1));
    if (avfilter_graph_create_filter(&yadif_filter, avfilter_get_by_name("yadif"), "yadif", yadif_args, nullptr,
                                     filter_graph) < 0) {
      qCritical() << "Could not create yadif filter";
      ok = false;
    } else {
      avfilter_link(last_filter, 0, yadif_filter, 0);
      last_filter = yadif_filter;
    }
  }

  if (ok) {
    char fmt_args[100];
    if (clip->NeedsCpuRgba()) {
      snprintf(fmt_args, sizeof(fmt_args), "pix_fmts=%s", av_get_pix_fmt_name(kDestPixFmt));
    } else {
      snprintf(fmt_args, sizeof(fmt_args), "pix_fmts=yuv420p|nv12|%s", av_get_pix_fmt_name(kDestPixFmt));
    }

    AVFilterContext* format_conv;
    if (avfilter_graph_create_filter(&format_conv, avfilter_get_by_name("format"), "fmt", fmt_args, nullptr,
                                     filter_graph) < 0) {
      qCritical() << "Could not create format filter";
      ok = false;
    } else {
      avfilter_link(last_filter, 0, format_conv, 0);
      avfilter_link(format_conv, 0, buffersink_ctx, 0);
    }
  }

  if (ok && avfilter_graph_config(filter_graph, nullptr) < 0) {
    qCritical() << "Could not configure video filter graph";
    ok = false;
  }

  if (!ok) {
    avfilter_graph_free(&filter_graph);
    filter_graph = nullptr;
    buffersrc_ctx = nullptr;
    buffersink_ctx = nullptr;
  }
}

// ---------------------------------------------------------------------------
// openWorkerBuildAtempoChain — build pitch-preserving speed filter chain
// ---------------------------------------------------------------------------
bool Cacher::openWorkerBuildAtempoChain(double playback_speed, AVFilterContext*& last_filter, bool& ok) {
  char speed_param[10];
  double base = (playback_speed > 1.0) ? 2.0 : 0.5;
  double speedlog = log(playback_speed) / log(base);
  int whole2 = qFloor(speedlog);
  speedlog -= whole2;

  if (whole2 > 0) {
    snprintf(speed_param, sizeof(speed_param), "%f", base);
    for (int i = 0; i < whole2; i++) {
      AVFilterContext* tempo_filter = nullptr;
      if (avfilter_graph_create_filter(&tempo_filter, avfilter_get_by_name("atempo"), "atempo", speed_param, nullptr,
                                       filter_graph) < 0) {
        qCritical() << "Could not create atempo filter in chain";
        ok = false;
        return false;
      }
      avfilter_link(last_filter, 0, tempo_filter, 0);
      last_filter = tempo_filter;
    }
  }

  snprintf(speed_param, sizeof(speed_param), "%f", qPow(base, speedlog));
  AVFilterContext* tempo_filter = nullptr;
  if (avfilter_graph_create_filter(&tempo_filter, avfilter_get_by_name("atempo"), "atempo", speed_param, nullptr,
                                   filter_graph) < 0) {
    qCritical() << "Could not create final atempo filter";
    ok = false;
    return false;
  }
  avfilter_link(last_filter, 0, tempo_filter, 0);
  last_filter = tempo_filter;
  return true;
}

// ---------------------------------------------------------------------------
// openWorkerAudioFilter
// ---------------------------------------------------------------------------
void Cacher::openWorkerAudioFilter(Footage* m) {
  if (!m) {
    qWarning() << "Cacher::openWorkerAudioFilter: Footage is null";
    return;
  }
  if (codecCtx->ch_layout.order == AV_CHANNEL_ORDER_UNSPEC) {
    av_channel_layout_default(&codecCtx->ch_layout, stream->codecpar->ch_layout.nb_channels);
  }

  // Set up cache
  queue_.append(av_frame_alloc());

  {
    AVFrame* reverse_frame = av_frame_alloc();
    reverse_frame->format = kDestSampleFmt;
    reverse_frame->nb_samples = current_audio_freq() * 10;
    av_channel_layout_from_mask(&reverse_frame->ch_layout, clip->sequence->audio_layout);
    av_frame_get_buffer(reverse_frame, 0);
    memset(reverse_frame->data[0], 0, reverse_frame->linesize[0]);
    queue_.append(reverse_frame);
  }

  char filter_args[512];
  {
    char ch_layout_str[64];
    av_channel_layout_describe(&codecCtx->ch_layout, ch_layout_str, sizeof(ch_layout_str));
    snprintf(filter_args, sizeof(filter_args), "time_base=%d/%d:sample_rate=%d:sample_fmt=%s:channel_layout=%s",
             stream->time_base.num, stream->time_base.den, stream->codecpar->sample_rate,
             av_get_sample_fmt_name(codecCtx->sample_fmt), ch_layout_str);
  }

  bool ok = true;

  if (avfilter_graph_create_filter(&buffersrc_ctx, avfilter_get_by_name("abuffer"), "in", filter_args, nullptr,
                                   filter_graph) < 0) {
    qCritical() << "Could not create audio buffer source";
    ok = false;
  }

  // Compute target sample rate early -- needed for buffersink options
  int target_sample_rate = current_audio_freq();
  double playback_speed = 1.0;
  if (ok) {
    playback_speed = clip->speed().value * m->speed;
    if (!qFuzzyIsNull(playback_speed) && !qFuzzyCompare(playback_speed, 1.0) && !clip->speed().maintain_audio_pitch) {
      target_sample_rate = qRound64(target_sample_rate / playback_speed);
    }
  }

  // Create abuffersink (no constraints — aformat filter handles conversion)
  if (ok) {
    if (avfilter_graph_create_filter(&buffersink_ctx, avfilter_get_by_name("abuffersink"), "out", nullptr, nullptr,
                                     filter_graph) < 0) {
      qCritical() << "Could not create audio buffer sink";
      ok = false;
    }
  }

  // Explicit aformat filter: converts sample format, channel layout, and sample rate.
  AVFilterContext* aformat_ctx = nullptr;
  if (ok) {
    char aformat_args[128];
    snprintf(aformat_args, sizeof(aformat_args), "sample_fmts=%s:channel_layouts=stereo:sample_rates=%d",
             av_get_sample_fmt_name(kDestSampleFmt), target_sample_rate);
    if (avfilter_graph_create_filter(&aformat_ctx, avfilter_get_by_name("aformat"), "aformat", aformat_args, nullptr,
                                     filter_graph) < 0) {
      qCritical() << "Could not create aformat filter";
      ok = false;
    }
  }

  // Build filter chain: abuffer → [atempo chain] → aformat → abuffersink
  if (ok) {
    AVFilterContext* last_filter = buffersrc_ctx;

    bool need_atempo =
        !qFuzzyCompare(playback_speed, 1.0) && !qFuzzyIsNull(playback_speed) && clip->speed().maintain_audio_pitch;
    if (need_atempo) {
      openWorkerBuildAtempoChain(playback_speed, last_filter, ok);
    }

    if (ok) {
      avfilter_link(last_filter, 0, aformat_ctx, 0);
      avfilter_link(aformat_ctx, 0, buffersink_ctx, 0);
    }
  }

  if (ok && avfilter_graph_config(filter_graph, nullptr) < 0) {
    qCritical() << "Could not configure audio filter graph";
    ok = false;
  }

  if (!ok) {
    avfilter_graph_free(&filter_graph);
    filter_graph = nullptr;
    buffersrc_ctx = nullptr;
    buffersink_ctx = nullptr;
  }

  audio_reset_ = true;
}

void Cacher::OpenWorker() {
  qint64 time_start = QDateTime::currentMSecsSinceEpoch();

  // Set some defaults for the audio cacher
  if (clip->track() >= 0) {
    audio_reset_ = false;
    frame_sample_index_ = -1;
    audio_buffer_write = 0;
  }
  reached_end = false;

  if (clip->media() == nullptr) {
    if (clip->track() >= 0) {
      openWorkerToneClip();
    }
  } else if (clip->media()->get_type() == MEDIA_TYPE_FOOTAGE) {
    Footage* m = clip->media()->to_footage();
    const FootageStream* ms = clip->media_stream();

    if (!openWorkerOpenCodec(m, ms)) {
      return;
    }

    // Allocate filter graph
    filter_graph = avfilter_graph_alloc();
    if (filter_graph == nullptr) {
      qCritical() << "Could not create filtergraph";
    }

    if (stream->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
      openWorkerVideoFilter(ms);
    } else if (stream->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
      openWorkerAudioFilter(m);
    }

    pkt = av_packet_alloc();
    frame_ = av_frame_alloc();
  }

  qInfo() << "Clip opened on track" << clip->track() << "(took" << (QDateTime::currentMSecsSinceEpoch() - time_start)
          << "ms)";

  is_valid_state_ = true;
}

void Cacher::CacheWorker() {
  if (clip->track() < 0) {
    // clip is a video track, start caching video
    CacheVideoWorker();
  } else {
    // clip is audio
    CacheAudioWorker();
  }
}

void Cacher::CloseWorker() {
  retrieved_frame = nullptr;
  queue_.lock();
  queue_.clear();
  queue_.unlock();

  if (frame_ != nullptr) {
    av_frame_free(&frame_);
    frame_ = nullptr;
  }

  if (pkt != nullptr) {
    av_packet_free(&pkt);
    pkt = nullptr;
  }

  if (clip->media() != nullptr && clip->media()->get_type() == MEDIA_TYPE_FOOTAGE) {
    if (filter_graph != nullptr) {
      avfilter_graph_free(&filter_graph);
      filter_graph = nullptr;
    }

    if (codecCtx != nullptr) {
      avcodec_free_context(&codecCtx);
      codecCtx = nullptr;
    }

    if (hw_device_ctx != nullptr) {
      av_buffer_unref(&hw_device_ctx);
      hw_device_ctx = nullptr;
    }

    if (opts != nullptr) {
      av_dict_free(&opts);
    }

    // protection for get_timebase()
    stream = nullptr;

    if (formatCtx != nullptr) {
      avformat_close_input(&formatCtx);
    }
  }

  qInfo() << "Clip closed on track" << clip->track();
}

void Cacher::run() {
  clip->cache_lock.lock();

  OpenWorker();

  clip->state_change_lock.unlock();

  while (caching_) {
    if (!queued_) {
      wait_cond_.wait(&clip->cache_lock);
    }
    queued_ = false;
    if (!caching_) {
      break;
    } else if (is_valid_state_) {
      CacheWorker();
    } else {
      // main thread waits until cacher starts fully, but the cacher can't run, so we just wake it up here
      WakeMainThread();
    }
  }

  is_valid_state_ = false;

  CloseWorker();

  clip->state_change_lock.unlock();

  clip->cache_lock.unlock();
}

void Cacher::Open() {
  wait();

  // set variable defaults for caching
  caching_ = true;
  queued_ = false;

  start((clip->track() < 0) ? QThread::HighPriority : QThread::TimeCriticalPriority);
}

// ---------------------------------------------------------------------------
// cacheFindFrameInQueue — search queue for matching frame
// ---------------------------------------------------------------------------
bool Cacher::cacheFindFrameInQueue(int64_t target_pts) {
  for (int i = 0; i < queue_.size(); i++) {
    if (queue_.at(i)->pts == target_pts) {
      retrieved_frame = queue_.at(i);
      return true;
    }
    if (i > 0 && queue_.at(i - 1)->pts < target_pts && queue_.at(i)->pts > target_pts) {
      // Close timestamp — assume rounding error, use the frame just before
      retrieved_frame = queue_.at(i - 1);
      return true;
    }
  }
  return false;
}

// ---------------------------------------------------------------------------
// Cache
// ---------------------------------------------------------------------------
void Cacher::Cache(long playhead, bool scrubbing, QVector<Clip*>& nests, int playback_speed) {
  if (!is_valid_state_) {
    return;
  }

  if (clip->media_stream() != nullptr && clip->media_stream()->infinite_length) {
    queue_.lock();
    if (queue_.size() > 0) {
      retrieved_frame = queue_.at(0);
    }
    queue_.unlock();
    if (retrieved_frame != nullptr) {
      return;
    }
  }

  playhead_ = playhead;
  nests_ = nests;
  scrubbing_ = scrubbing;
  playback_speed_ = playback_speed;
  queued_ = true;

  bool wait_for_cacher_to_respond = true;

  if (clip->media() != nullptr) {
    // see if we already have this frame
    retrieve_lock_.lock();
    queue_.lock();
    retrieved_frame = nullptr;
    int64_t target_pts = seconds_to_timestamp(clip, playhead_to_clip_seconds(clip, playhead_));
    if (cacheFindFrameInQueue(target_pts)) {
      wait_for_cacher_to_respond = false;
    }
    queue_.unlock();
    retrieve_lock_.unlock();
  }

  // Audio scrub: fire-and-forget — never block the UI thread.
  bool audio_scrub = (scrubbing && clip->track() >= 0);

  if (wait_for_cacher_to_respond && !audio_scrub) {
    main_thread_lock_.lock();
    main_thread_woken_ = false;
  }

  // wake up cacher
  wait_cond_.wakeAll();

  // wait for cacher to respond (video and audio playback only, not audio scrub)
  if (wait_for_cacher_to_respond && !audio_scrub) {
    interrupt_ = true;
    if (!main_thread_woken_) {
      main_thread_wait_.wait(&main_thread_lock_, 2000);
    }
  }

  if (wait_for_cacher_to_respond && !audio_scrub) {
    main_thread_lock_.unlock();
  }
}

AVFrame* Cacher::Retrieve() {
  if (!caching_) {
    return nullptr;
  }

  // for thread-safety, we lock a mutex to ensure this thread is never woken by anything out of sync

  retrieve_lock_.lock();

  // check if there's a frame ready to be shown by the cacher

  // When scrubbing, use a shorter timeout to keep the UI responsive
  const int timeout_ms = scrubbing_ ? 250 : 500;
  const int max_attempts = scrubbing_ ? 1 : 4;

  int attempts = 0;
  while (retrieved_frame == nullptr) {
    if (clip->cache_lock.tryLock()) {
      // If the queue could lock, the cacher isn't running which means no frame is coming. This is an error.
      qCritical() << "Cacher frame was null while the cacher wasn't running on clip" << clip->name();
      clip->cache_lock.unlock();
      break;

    } else {
      // cacher is running, wait for it to give a frame (with timeout to avoid deadlock
      // when the cacher finishes without producing a frame, e.g. missing/corrupt media)
      if (!retrieve_wait_.wait(&retrieve_lock_, timeout_ms)) {
        if (++attempts >= max_attempts) {
          if (scrubbing_) {
            qDebug() << "Scrub: skipping frame on clip" << clip->name();
          } else {
            qWarning() << "Timed out waiting for frame from cacher on clip" << clip->name();
          }
          break;
        }
      }
    }
  }

  retrieve_lock_.unlock();

  return retrieved_frame;
}

void Cacher::Close(bool wait_for_finish) {
  caching_ = false;
  wait_cond_.wakeAll();

  if (wait_for_finish) {
    wait();
  }
}

void Cacher::ResetAudio() {
  audio_write_lock.lock();
  audio_reset_ = true;
  frame_sample_index_ = -1;
  audio_buffer_write = 0;
  audio_write_lock.unlock();
}

int Cacher::media_width() {
  if (stream == nullptr) return 0;
  return stream->codecpar->width;
}

int Cacher::media_height() {
  if (stream == nullptr) return 0;
  return stream->codecpar->height;
}

AVRational Cacher::media_time_base() {
  if (stream == nullptr) return {0, 1};
  return stream->time_base;
}

ClipQueue* Cacher::queue() { return &queue_; }
