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

#include <QtMath>

#include "global/config.h"
#include "global/debug.h"
#include "project/projectelements.h"
#include "rendering/renderfunctions.h"

static int64_t seek_landing_timestamp(AVFormatContext* formatCtx, int stream_index, int64_t target_pts);

// Where a seek that hit EOF goes next: just before the keyframe it landed on (container index), else the furthest frame
// decoded since the clip opened, else the stream's declared last frame. AV_NOPTS_VALUE when none is known.
int64_t Cacher::cacheVideoLastDecodable(int64_t seek_ts) {
  const int64_t landing = seek_landing_timestamp(formatCtx, clip->media_stream_index(), seek_ts);
  if (landing != AV_NOPTS_VALUE && landing > 0) return landing - 1;
  if (latest_decoded_pts_ != AV_NOPTS_VALUE) return latest_decoded_pts_;
  // The probe's rate (av_guess_frame_rate()): avg_frame_rate can be the time base, not a frame rate (raw DV: 60000/1)
  const AVRational rate = av_guess_frame_rate(formatCtx, stream, nullptr);
  if (stream->duration != AV_NOPTS_VALUE && rate.num > 0 && rate.den > 0) {
    const int64_t start = (stream->start_time != AV_NOPTS_VALUE) ? stream->start_time : 0;
    return start + stream->duration - av_rescale_q(1, av_inv_q(rate), stream->time_base);
  }
  return AV_NOPTS_VALUE;
}

// yadif outputs a frame once it holds the next one and keeps the one before: after a seek it would hand out fields from
// before it, with their old timestamps, ahead of the new position's. A fresh graph forgets them. Progressive graphs
// hold no frame and are kept.
void Cacher::cacheVideoResetDeinterlacer() {
  if (filter_graph == nullptr || avfilter_graph_get_filter(filter_graph, "yadif") == nullptr) return;
  avfilter_graph_free(&filter_graph);
  buffersrc_ctx = nullptr;
  buffersink_ctx = nullptr;
  filter_graph = avfilter_graph_alloc();
  if (filter_graph != nullptr) openWorkerVideoFilter(clip->media_stream());
}

// ---------------------------------------------------------------------------
// cacheVideoSeekToTarget
// ---------------------------------------------------------------------------
int Cacher::cacheVideoSeekToTarget(int64_t target_pts, int64_t second_pts, AVFrame*& decoded_frame,
                                   bool& seeked_to_zero) {
  int retrieve_code = 0;
  int64_t zero = 0;
  bool have_existing = false;

  // A seek landing past its target pays another, a second earlier. MPEG-PS/TS (no keyframe index; PS indexes every
  // packet as one) always land past it, on the keyframe after the packet the seek finds, DV a frame past. Start each
  // seek as far before the target as this clip's seeks have overshot so far: after its first, they land at or before.
  // CBA: an overshoot counts for at most a second, so one wild landing can't make every later seek decode far back; a
  // format overshooting more keeps paying the step back.
  int64_t seek_ts = qMax(zero, target_pts - seek_overshoot_);

  // Seek a second further back while the landing frame is past the target. EOF instead means nothing is decodable from
  // the landing point to the end: the target is past the media's real end (a proxy a frame short of its original, a
  // container that overstates its length). Jump once to where the media is known to decode, then step back doubling
  // the step: a few passes however far past the end, not one per second.
  int64_t eof_step = second_pts;
  bool jumped = false;
  do {
    if (have_existing) av_frame_free(&decoded_frame);

    seeked_to_zero = (seek_ts == 0);
    avcodec_flush_buffers(codecCtx);
    last_raw_video_pts_ = AV_NOPTS_VALUE;
    skip_to_keyframe_ = kSeekSkipFrames;
    cacheVideoResetDeinterlacer();
    av_seek_frame(formatCtx, clip->media_stream_index(), seek_ts, AVSEEK_FLAG_BACKWARD);

    retrieve_code = RetrieveFrameAndProcess(&decoded_frame);
    if (retrieve_code >= 0 && decoded_frame->pts != AV_NOPTS_VALUE && decoded_frame->pts > target_pts &&
        !seeked_to_zero) {
      seek_overshoot_ = qMax(seek_overshoot_, qMin(second_pts, decoded_frame->pts - seek_ts));
    }

    if (retrieve_code == AVERROR_EOF) {
      const int64_t known = jumped ? AV_NOPTS_VALUE : cacheVideoLastDecodable(seek_ts);
      jumped = true;
      if (known != AV_NOPTS_VALUE && known < seek_ts) {
        seek_ts = qMax(zero, known);
      } else {
        seek_ts = qMax(zero, seek_ts - eof_step);
        eof_step *= 2;
      }
    } else {
      seek_ts = qMax(zero, seek_ts - second_pts);
    }
    have_existing = true;
  } while ((retrieve_code == AVERROR_EOF ||
            (retrieve_code >= 0 && (decoded_frame->pts == AV_NOPTS_VALUE || decoded_frame->pts > target_pts))) &&
           !seeked_to_zero);

  queue_.lock();
  queue_.clear();
  queue_.unlock();

  return retrieve_code;
}

// Whether this pass may serve the request waiting now. One that came in while it ran (playhead_ moved on) waits for the
// next pass (CacheVideoWorker() runs again) when it needs that exact frame: an export, a paused viewer. The pass used
// to hand it its own queue's last frame, a frame or more early. Playback and scrubbing keep taking it: the nearest
// frame decoded so far beats waiting while the decode is behind (the next pass may seek and re-decode a GOP).
bool Cacher::passServesRequest() const {
  if (pass_playhead_ == playhead_) return true;
  return !clip->OpenedForExport() && (playback_speed_ != 0 || scrubbing_);
}

// ---------------------------------------------------------------------------
// cacheVideoMaybeSetRetrieved
// ---------------------------------------------------------------------------
void Cacher::cacheVideoMaybeSetRetrieved(AVFrame* decoded_frame, int64_t target_pts, bool& seeked_to_zero) {
  if (retrieved_frame != nullptr || !passServesRequest()) return;

  if (decoded_frame->pts == target_pts) {
    SetRetrievedFrame(decoded_frame);
  } else if (decoded_frame->pts > target_pts) {
    if (queue_.size() > 0) {
      SetRetrievedFrame(queue_.last());
    } else if (seeked_to_zero) {
      SetRetrievedFrame(decoded_frame);
      seeked_to_zero = false;
    }
  }
}

// ---------------------------------------------------------------------------
// cacheVideoTrimPreviousFrames
// ---------------------------------------------------------------------------
void Cacher::cacheVideoTrimPreviousFrames(int64_t target_pts, int64_t minimum_ts) {
  queue_.lock();
  int previous_frame_count = 0;
  if (queue_.last()->pts < target_pts) {
    previous_frame_count = queue_.size();
  } else {
    for (int i = 0; i < queue_.size(); i++) {
      if (queue_.at(i)->pts > target_pts) break;
      previous_frame_count++;
    }
  }
  while (previous_frame_count > minimum_ts) {
    queue_.removeFirst();
    previous_frame_count--;
  }
  queue_.unlock();
}

// ---------------------------------------------------------------------------
// cacheVideoEvictBeforeTimestamp
// ---------------------------------------------------------------------------
void Cacher::cacheVideoEvictBeforeTimestamp(int64_t minimum_ts) {
  queue_.lock();
  while (queue_.size() > 1 && queue_.first()->pts < minimum_ts && queue_.first() != retrieved_frame) {
    queue_.removeFirst();
  }
  queue_.unlock();
}

// ---------------------------------------------------------------------------
// cacheVideoStillImage — handle still-image clip caching
// ---------------------------------------------------------------------------
void Cacher::cacheVideoStillImage() {
  if (queue_.size() > 0) return;

  AVFrame* still_image_frame;
  if (RetrieveFrameAndProcess(&still_image_frame) >= 0) {
    queue_.lock();
    queue_.append(still_image_frame);
    queue_.unlock();
    SetRetrievedFrame(still_image_frame);
  } else {
    av_frame_free(&still_image_frame);
  }
}

// Resolve queue config for forward or reversed playback.
struct QueueConfig {
  int previous_queue_type;
  double previous_queue_size;
  int upcoming_queue_type;
  double upcoming_queue_size;
};

static QueueConfig resolve_queue_config(bool reversed) {
  QueueConfig cfg;
  if (reversed) {
    cfg.previous_queue_type = amber::CurrentConfig.upcoming_queue_type;
    cfg.previous_queue_size = amber::CurrentConfig.upcoming_queue_size;
    cfg.upcoming_queue_type = amber::CurrentConfig.previous_queue_type;
    cfg.upcoming_queue_size = amber::CurrentConfig.previous_queue_size;
  } else {
    cfg.previous_queue_type = amber::CurrentConfig.previous_queue_type;
    cfg.previous_queue_size = amber::CurrentConfig.previous_queue_size;
    cfg.upcoming_queue_type = amber::CurrentConfig.upcoming_queue_type;
    cfg.upcoming_queue_size = amber::CurrentConfig.upcoming_queue_size;
  }
  return cfg;
}

// Process one decoded frame inside the decode loop.
// Returns true if the loop should break.
bool Cacher::cacheVideoProcessDecodedFrame(AVFrame* decoded_frame, int retrieve_code, int64_t target_pts,
                                           bool& seeked_to_zero, int previous_queue_type, int upcoming_queue_type,
                                           int64_t minimum_ts, int64_t maximum_ts, int& frames_greater_than_target) {
  if (retrieve_code < 0 && retrieve_code != AVERROR_EOF) {
    qCritical() << "Failed to retrieve frame from buffersink." << retrieve_code;
    av_frame_free(&decoded_frame);
    return true;
  }

  // EOF hands back the frame RetrieveFrameAndProcess() allocated, which nothing filled: never queue or retrieve it. A
  // target past the media's last frame then gets the last decoded one, CacheVideoWorker()'s closest-frame fallback.
  if (retrieve_code == AVERROR_EOF) {
    av_frame_free(&decoded_frame);
    // The media's real end, when frames were decoded since the last seek: later targets past it are served that frame
    // without seeking again (cacheTryQueueHit(), cacheVideoDynamicClip())
    queue_.lock();
    if (!queue_.isEmpty()) video_end_pts_ = latest_decoded_pts_;
    queue_.unlock();
    return true;
  }

  // No timestamp to place it in the queue: free it, the decode loop allocates a fresh frame on the next pass
  if (decoded_frame->pts == AV_NOPTS_VALUE) {
    av_frame_free(&decoded_frame);
    return false;
  }
  if (latest_decoded_pts_ == AV_NOPTS_VALUE || decoded_frame->pts > latest_decoded_pts_) {
    latest_decoded_pts_ = decoded_frame->pts;
  }

  // Discard frames before the minimum timestamp (seconds mode)
  if (previous_queue_type == amber::FRAME_QUEUE_TYPE_SECONDS && decoded_frame->pts < minimum_ts) {
    av_frame_free(&decoded_frame);
    return false;
  }

  cacheVideoMaybeSetRetrieved(decoded_frame, target_pts, seeked_to_zero);

  queue_.lock();
  queue_.append(decoded_frame);
  queue_.unlock();

  if (previous_queue_type == amber::FRAME_QUEUE_TYPE_FRAMES) {
    cacheVideoTrimPreviousFrames(target_pts, minimum_ts);
  } else {
    // SECONDS mode: frames older than the window were only discarded on arrival (above),
    // never evicted once queued — during continuous playback the queue grew unbounded
    cacheVideoEvictBeforeTimestamp(minimum_ts);
  }

  // Check if the upcoming queue is full
  if (upcoming_queue_type == amber::FRAME_QUEUE_TYPE_FRAMES) {
    if (decoded_frame->pts > target_pts) {
      frames_greater_than_target++;
      if (frames_greater_than_target >= maximum_ts) return true;
    }
  } else if (decoded_frame->pts > maximum_ts) {
    return true;
  }

  return false;
}

// Scan the queue for earliest/latest pts and count frames above target.
struct QueueStats {
  int64_t earliest_pts;
  int64_t latest_pts;
  int frames_greater_than_target;
};

static QueueStats scan_queue(ClipQueue& queue_, int64_t target_pts) {
  QueueStats stats{INT64_MAX, INT64_MIN, 0};
  queue_.lock();
  for (int i = 0; i < queue_.size(); i++) {
    stats.earliest_pts = qMin(stats.earliest_pts, queue_.at(i)->pts);
    stats.latest_pts = qMax(stats.latest_pts, queue_.at(i)->pts);
    if (queue_.at(i)->pts > target_pts) stats.frames_greater_than_target++;
  }
  queue_.unlock();
  return stats;
}

// Returns true if the upcoming queue is already full (no need to decode more frames).
static bool upcoming_queue_is_full(const QueueConfig& cfg, int64_t latest_pts, int frames_greater_than_target,
                                   int64_t maximum_ts) {
  if (cfg.upcoming_queue_type == amber::FRAME_QUEUE_TYPE_FRAMES) {
    return frames_greater_than_target >= maximum_ts;
  }
  return latest_pts > maximum_ts;
}

// Returns the timestamp of the keyframe a BACKWARD seek to target_pts would land on,
// or AV_NOPTS_VALUE if the container index can't tell.
static int64_t seek_landing_timestamp(AVFormatContext* formatCtx, int stream_index, int64_t target_pts) {
  AVStream* st = formatCtx->streams[stream_index];
  int entry = av_index_search_timestamp(st, target_pts, AVSEEK_FLAG_BACKWARD);
  if (entry < 0) return AV_NOPTS_VALUE;
#if LIBAVFORMAT_VERSION_INT >= AV_VERSION_INT(58, 78, 100)
  const AVIndexEntry* e = avformat_index_get_entry(st, entry);
  return (e != nullptr) ? e->timestamp : AV_NOPTS_VALUE;
#else
  return st->index_entries[entry].timestamp;
#endif
}

// ---------------------------------------------------------------------------
// cacheVideoDynamicClip — handle non-still-image (dynamic) clip caching
// ---------------------------------------------------------------------------
void Cacher::cacheVideoDynamicClip() {
  bool reversed = IsReversed();
  int64_t target_pts = videoTargetPts(pass_playhead_);
  int64_t second_pts = seconds_to_timestamp(clip, 1);

  QueueStats stats = scan_queue(queue_, target_pts);

  AVFrame* decoded_frame = nullptr;
  bool have_existing_frame_to_use = false;
  bool seeked_to_zero = false;

  // Seek if target is outside the cached window
  bool need_seek = target_pts < stats.earliest_pts || target_pts > stats.latest_pts + second_pts || queue_.size() == 0;

  // Seek-storm guard (#57): when decode falls behind during forward playback, a BACKWARD
  // seek lands on the keyframe preceding the target. On long-GOP media that keyframe is
  // often one we already decoded past — the seek would throw the queue away and re-decode
  // the same GOP, repeatedly, without ever catching up. If the container index tells us
  // the landing keyframe is at or before our latest decoded frame, decoding forward is
  // strictly cheaper: keep going instead.
  if (need_seek && queue_.size() > 0 && target_pts > stats.latest_pts) {
    int64_t landing = seek_landing_timestamp(formatCtx, clip->media_stream_index(), target_pts);
    if (landing != AV_NOPTS_VALUE && landing <= stats.latest_pts) {
      need_seek = false;
    }
  }

  int seek_retrieve_code = 0;
  if (need_seek) {
    seek_retrieve_code = cacheVideoSeekToTarget(target_pts, second_pts, decoded_frame, seeked_to_zero);
    have_existing_frame_to_use = true;
    stats.frames_greater_than_target = 0;
    stats.latest_pts = INT64_MIN;
  }

  QueueConfig cfg = resolve_queue_config(reversed);

  // qRound64: these are stream timestamps — qRound returns int, which overflows past
  // 2^31 (~6.6 hours into a file with a 90kHz timebase)
  int64_t minimum_ts = (cfg.previous_queue_type == amber::FRAME_QUEUE_TYPE_FRAMES)
                           ? qCeil(cfg.previous_queue_size)
                           : qRound64(target_pts - second_pts * cfg.previous_queue_size);

  int64_t maximum_ts = (cfg.upcoming_queue_type == amber::FRAME_QUEUE_TYPE_FRAMES)
                           ? qCeil(cfg.upcoming_queue_size)
                           : qRound64(target_pts + second_pts * cfg.upcoming_queue_size);

  if (upcoming_queue_is_full(cfg, stats.latest_pts, stats.frames_greater_than_target, maximum_ts)) {
    if (have_existing_frame_to_use) av_frame_free(&decoded_frame);
    return;
  }

  interrupt_ = false;
  do {
    int retrieve_code = 0;
    if (!have_existing_frame_to_use) {
      retrieve_code = RetrieveFrameAndProcess(&decoded_frame);
    } else {
      // The seek already decoded this frame: carry its status too (EOF / error), not a blanket success
      retrieve_code = seek_retrieve_code;
      have_existing_frame_to_use = false;
    }

    if (cacheVideoProcessDecodedFrame(decoded_frame, retrieve_code, target_pts, seeked_to_zero, cfg.previous_queue_type,
                                      cfg.upcoming_queue_type, minimum_ts, maximum_ts,
                                      stats.frames_greater_than_target)) {
      break;
    }
  } while (!interrupt_);
}

// ---------------------------------------------------------------------------
// CacheVideoWorker
// ---------------------------------------------------------------------------
void Cacher::CacheVideoWorker() {
  // Skip video caching if filter graph failed to initialize
  if (filter_graph == nullptr) {
    WakeMainThread();
    return;
  }

  pass_playhead_ = playhead_;
  WakeMainThread();

  if (clip->media_stream() != nullptr && clip->media_stream()->infinite_length) {
    cacheVideoStillImage();
  } else {
    cacheVideoDynamicClip();
  }

  // If we couldn't find the exact target frame, fall back to the closest available frame
  if (retrieved_frame == nullptr && passServesRequest()) {
    if (!queue_.isEmpty()) {
      qDebug() << "Exact frame not found, using closest available frame from queue";
      SetRetrievedFrame(queue_.last());
    } else {
      qCritical() << "Couldn't retrieve an appropriate frame. This is an error and may mean this media is corrupt.";
      SetRetrievedFrame(nullptr);
    }
  }
}

// ---------------------------------------------------------------------------
// Helpers for RetrieveFrameFromDecoder
// ---------------------------------------------------------------------------

// Feed one packet (or EOF flush) to the decoder.
// Returns 0 on success, AVERROR_EOF if flush packet sent, or negative error code.
static int feedNextPacketToDecoder(AVFormatContext* formatCtx, AVCodecContext* codecCtx, AVPacket* pkt, Clip* clip) {
  // A packet the decoder refuses as invalid is skipped for the next one: after a seek into MPEG-PS the first MP2 packet
  // starts mid-frame, and giving up there left the clip silent from the seek on.
  // CBA: at most kMaxInvalidPackets in a row, then the error goes up as before (a stream that never decodes).
  constexpr int kMaxInvalidPackets = 64;
  int read_ret = 0;
  for (int invalid = 0;; invalid++) {
    do {
      if (pkt->buf != nullptr) av_packet_unref(pkt);
      read_ret = av_read_frame(formatCtx, pkt);
    } while (read_ret >= 0 && pkt->stream_index != clip->media_stream_index());
    if (read_ret < 0) break;

    int send_ret = avcodec_send_packet(codecCtx, pkt);
    if (send_ret == AVERROR_INVALIDDATA && invalid < kMaxInvalidPackets) continue;
    if (send_ret < 0) {
      qCritical() << "Failed to send packet to decoder." << send_ret;
      return send_ret;
    }
    return 0;
  }

  if (read_ret == AVERROR_EOF) {
    int send_ret = avcodec_send_packet(codecCtx, nullptr);
    if (send_ret < 0) {
      qCritical() << "Failed to send packet to decoder." << send_ret;
      return send_ret;
    }
    return AVERROR_EOF;
  }

  qCritical() << "Could not read frame." << read_ret;
  return read_ret;
}

// Transfer a hardware-accelerated frame to software memory in-place.
static int transferHwFrameToSoftware(AVFrame* f) {
  AVFrame* sw_frame = av_frame_alloc();
  if (av_hwframe_transfer_data(sw_frame, f, 0) < 0) {
    qWarning() << "Failed to transfer hw frame to software";
    av_frame_free(&sw_frame);
    return AVERROR(ENOTSUP);
  }
  av_frame_copy_props(sw_frame, f);
  av_frame_unref(f);
  av_frame_move_ref(f, sw_frame);
  av_frame_free(&sw_frame);
  return 0;
}

// ---------------------------------------------------------------------------
// RetrieveFrameFromDecoder
// ---------------------------------------------------------------------------
int Cacher::RetrieveFrameFromDecoder(AVFrame* f) {
  if (!f) {
    qWarning() << "Cacher::RetrieveFrameFromDecoder: frame is null";
    return -1;
  }
  int result = 0;
  int receive_ret;

  av_frame_unref(f);
  while ((receive_ret = avcodec_receive_frame(codecCtx, f)) == AVERROR(EAGAIN)) {
    int feed_ret = feedNextPacketToDecoder(formatCtx, codecCtx, pkt, clip);
    if (feed_ret < 0 && feed_ret != AVERROR_EOF) {
      return feed_ret;
    }
    // AVERROR_EOF from feed means flush packet was sent — keep draining
  }

  if (receive_ret < 0) {
    if (receive_ret != AVERROR_EOF) qCritical() << "Failed to receive packet from decoder." << receive_ret;
    result = receive_ret;
  }

  // If the frame is in hardware format, transfer to software
  if (result >= 0 && hw_device_ctx != nullptr && f->format != AV_PIX_FMT_NONE) {
    const AVPixFmtDescriptor* desc = av_pix_fmt_desc_get(static_cast<AVPixelFormat>(f->format));
    if (desc != nullptr && (desc->flags & AV_PIX_FMT_FLAG_HWACCEL)) {
      result = transferHwFrameToSoftware(f);
    }
  }

  return result;
}

static bool frame_is_key(const AVFrame* f) {
#if LIBAVUTIL_VERSION_INT >= AV_VERSION_INT(58, 29, 100)
  return (f->flags & AV_FRAME_FLAG_KEY) != 0;
#else
  return f->key_frame != 0;
#endif
}

int Cacher::RetrieveFrameAndProcess(AVFrame** f) {
  if (!f) {
    qWarning() << "Cacher::RetrieveFrameAndProcess: output frame pointer is null";
    return -1;
  }
  // error codes from FFmpeg
  int retrieve_code, read_code = 0, send_code;

  // frame for FFmpeg to decode into
  *f = av_frame_alloc();
  // No graph: rebuilding it after a seek failed (cacheVideoResetDeinterlacer())
  if (buffersink_ctx == nullptr) return AVERROR(EINVAL);

  // loop to pull frames from the AVFilter stack
  while ((retrieve_code = av_buffersink_get_frame(buffersink_ctx, *f)) == AVERROR(EAGAIN)) {
    // retrieve frame from decoder
    read_code = RetrieveFrameFromDecoder(frame_);

    if (read_code >= 0) {
      // we retrieved a decoded video frame, which we will send to the AVFilter stack to convert to RGBA (with other
      // adjustments if necessary)

      // After a seek, MPEG-4 Part 2 (in MPEG-PS/TS, which seek between keyframes) hands out the frames before the
      // next keyframe decoded from the missing reference: grey. MPEG-2, H.264 and HEVC decoders hold those back.
      // CBA: at most kSeekSkipFrames, then frames go through as before: a stream without keyframes (intra refresh)
      // still decodes, a GOP longer than that shows its first frames grey. Raise it if such GOPs turn up.
      if (skip_to_keyframe_ > 0) {
        if (!frame_is_key(frame_)) {
          skip_to_keyframe_--;
          av_frame_unref(frame_);
          continue;
        }
        skip_to_keyframe_ = 0;
      }

      // A frame whose packet had no pts of its own (MPEG-PS: one pts per packet, a packet can start several frames)
      // has only the decoder's guess, and the last one out of the decoder not even that: the frame before's plus a
      // frame. Without a timestamp the frame has no place in the queue and is dropped.
      if (frame_->pts == AV_NOPTS_VALUE) frame_->pts = frame_->best_effort_timestamp;
      if (frame_->pts == AV_NOPTS_VALUE && last_raw_video_pts_ != AV_NOPTS_VALUE) {
        const AVRational rate = av_guess_frame_rate(formatCtx, stream, nullptr);
        if (rate.num > 0 && rate.den > 0) {
          frame_->pts = last_raw_video_pts_ + av_rescale_q(1, av_inv_q(rate), stream->time_base);
        }
      }
      last_raw_video_pts_ = frame_->pts;

      // On the first successful decode after (re)opening the filter graph, lock the buffersrc
      // to the actual frame layout. For software decoding this matches what we seeded from
      // stream->codecpar in openWorkerVideoFilter(). For hardware decoding, the post-transfer
      // software format (NV12, P010, ...) differs from codecpar's hwaccel format, so without
      // this FFmpeg logs "Changing video frame properties on the fly is not supported" for
      // every single frame. hw_frames_ctx is left null because frames are already transferred
      // to system memory in RetrieveFrameFromDecoder before reaching this point.
      if (!buffersrc_params_set_ && buffersrc_ctx != nullptr) {
        AVBufferSrcParameters* bsp = av_buffersrc_parameters_alloc();
        if (bsp != nullptr) {
          bsp->format = frame_->format;
          bsp->width = frame_->width;
          bsp->height = frame_->height;
          bsp->sample_aspect_ratio = frame_->sample_aspect_ratio;
          bsp->time_base = stream->time_base;
          bsp->hw_frames_ctx = nullptr;
          int set_ret = av_buffersrc_parameters_set(buffersrc_ctx, bsp);
          av_free(bsp);
          if (set_ret < 0) {
            qWarning() << "Failed to set buffersrc parameters from first frame:" << set_ret;
          }
        }
        buffersrc_params_set_ = true;
      }

      if ((send_code = av_buffersrc_add_frame_flags(buffersrc_ctx, frame_, AV_BUFFERSRC_FLAG_KEEP_REF)) < 0) {
        qCritical() << "Failed to add frame to buffer source." << send_code;
        break;
      }

      // we don't need the original frame to we free it here
      av_frame_unref(frame_);

    } else {
      // AVERROR_EOF means we've reached the end of the file, not technically an error, but it's useful to know that
      // there are no more frames in this file
      if (read_code != AVERROR_EOF) {
        qCritical() << "Failed to read frame." << read_code;
      } else if (avfilter_graph_get_filter(filter_graph, "yadif") != nullptr) {
        // yadif lets a frame out once it holds the next one: the last frame waits for the end of the stream. That
        // closes the graph, which the next seek rebuilds (cacheVideoResetDeinterlacer()).
        av_buffersrc_add_frame_flags(buffersrc_ctx, nullptr, 0);
        retrieve_code = av_buffersink_get_frame(buffersink_ctx, *f);
        if (retrieve_code >= 0) read_code = 0;
      }
      break;
    }
  }

  if (read_code == AVERROR_EOF) {
    return AVERROR_EOF;
  }
  // yadif at field rate (mode=3) outputs in half the stream's time base, pts doubled: back to the stream's, which
  // every target, queue lookup and seek compares against. A no-op without yadif (same time base).
  if (retrieve_code >= 0 && (*f)->pts != AV_NOPTS_VALUE) {
    (*f)->pts = av_rescale_q((*f)->pts, av_buffersink_get_time_base(buffersink_ctx), stream->time_base);
  }
  return retrieve_code;
}
