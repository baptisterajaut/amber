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

// ---------------------------------------------------------------------------
// cacheVideoSeekToTarget
// ---------------------------------------------------------------------------
int Cacher::cacheVideoSeekToTarget(int64_t target_pts, int64_t second_pts, AVFrame*& decoded_frame,
                                   bool& seeked_to_zero) {
  int retrieve_code = 0;
  int64_t seek_ts = target_pts;
  int64_t zero = 0;
  bool have_existing = false;

  do {
    if (have_existing) av_frame_free(&decoded_frame);

    seeked_to_zero = (seek_ts == 0);
    avcodec_flush_buffers(codecCtx);
    av_seek_frame(formatCtx, clip->media_stream_index(), seek_ts, AVSEEK_FLAG_BACKWARD);

    retrieve_code = RetrieveFrameAndProcess(&decoded_frame);

    seek_ts = qMax(zero, seek_ts - second_pts);
    have_existing = true;
  } while (retrieve_code >= 0 && (decoded_frame->pts == AV_NOPTS_VALUE || decoded_frame->pts > target_pts) &&
           !seeked_to_zero);

  queue_.lock();
  queue_.clear();
  queue_.unlock();

  return retrieve_code;
}

// ---------------------------------------------------------------------------
// cacheVideoMaybeSetRetrieved
// ---------------------------------------------------------------------------
void Cacher::cacheVideoMaybeSetRetrieved(AVFrame* decoded_frame, int64_t target_pts, bool& seeked_to_zero) {
  if (retrieved_frame != nullptr) return;

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
// cacheVideoHandleNoPtsFrame
// ---------------------------------------------------------------------------
bool Cacher::cacheVideoHandleNoPtsFrame(AVFrame* decoded_frame, int64_t target_pts, int retrieve_code) {
  decoded_frame->pts = target_pts;
  if (retrieve_code == AVERROR_EOF) {
    if (retrieved_frame == nullptr) SetRetrievedFrame(decoded_frame);
    queue_.lock();
    queue_.append(decoded_frame);
    queue_.unlock();
    return true;  // break the decode loop
  }
  return false;
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

  if (decoded_frame->pts == AV_NOPTS_VALUE) {
    return cacheVideoHandleNoPtsFrame(decoded_frame, target_pts, retrieve_code);
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

// ---------------------------------------------------------------------------
// cacheVideoDynamicClip — handle non-still-image (dynamic) clip caching
// ---------------------------------------------------------------------------
void Cacher::cacheVideoDynamicClip() {
  bool reversed = IsReversed();
  int64_t target_pts = seconds_to_timestamp(clip, playhead_to_clip_seconds(clip, playhead_));
  int64_t second_pts = seconds_to_timestamp(clip, 1);

  // Check which range of frames we have in the queue
  int64_t earliest_pts = INT64_MAX;
  int64_t latest_pts = INT64_MIN;
  int frames_greater_than_target = 0;

  queue_.lock();
  for (int i = 0; i < queue_.size(); i++) {
    earliest_pts = qMin(earliest_pts, queue_.at(i)->pts);
    latest_pts = qMax(latest_pts, queue_.at(i)->pts);
    if (queue_.at(i)->pts > target_pts) frames_greater_than_target++;
  }
  queue_.unlock();

  AVFrame* decoded_frame = nullptr;
  bool have_existing_frame_to_use = false;
  bool seeked_to_zero = false;

  // Seek if target is outside the cached window
  if (target_pts < earliest_pts || target_pts > latest_pts + second_pts || queue_.size() == 0) {
    cacheVideoSeekToTarget(target_pts, second_pts, decoded_frame, seeked_to_zero);
    have_existing_frame_to_use = true;
    frames_greater_than_target = 0;
    latest_pts = INT64_MIN;
  }

  QueueConfig cfg = resolve_queue_config(reversed);

  int64_t minimum_ts = (cfg.previous_queue_type == amber::FRAME_QUEUE_TYPE_FRAMES)
                           ? qCeil(cfg.previous_queue_size)
                           : qRound(target_pts - second_pts * cfg.previous_queue_size);

  int64_t maximum_ts = (cfg.upcoming_queue_type == amber::FRAME_QUEUE_TYPE_FRAMES)
                           ? qCeil(cfg.upcoming_queue_size)
                           : qRound(target_pts + second_pts * cfg.upcoming_queue_size);

  // Check if we already have enough upcoming frames
  bool start_loop =
      !((cfg.upcoming_queue_type == amber::FRAME_QUEUE_TYPE_FRAMES && frames_greater_than_target >= maximum_ts) ||
        (cfg.upcoming_queue_type == amber::FRAME_QUEUE_TYPE_SECONDS && latest_pts > maximum_ts));

  if (have_existing_frame_to_use && !start_loop) {
    av_frame_free(&decoded_frame);
    have_existing_frame_to_use = false;
  }

  if (!start_loop) return;

  interrupt_ = false;
  do {
    int retrieve_code = 0;
    if (!have_existing_frame_to_use) {
      retrieve_code = RetrieveFrameAndProcess(&decoded_frame);
    } else {
      have_existing_frame_to_use = false;
    }

    if (cacheVideoProcessDecodedFrame(decoded_frame, retrieve_code, target_pts, seeked_to_zero, cfg.previous_queue_type,
                                      cfg.upcoming_queue_type, minimum_ts, maximum_ts, frames_greater_than_target)) {
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

  WakeMainThread();

  if (clip->media_stream() != nullptr && clip->media_stream()->infinite_length) {
    cacheVideoStillImage();
  } else {
    cacheVideoDynamicClip();
  }

  // If we couldn't find the exact target frame, fall back to the closest available frame
  if (retrieved_frame == nullptr) {
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
  int read_ret = 0;
  do {
    if (pkt->buf != nullptr) av_packet_unref(pkt);
    read_ret = av_read_frame(formatCtx, pkt);
  } while (read_ret >= 0 && pkt->stream_index != clip->media_stream_index());

  if (read_ret >= 0) {
    int send_ret = avcodec_send_packet(codecCtx, pkt);
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
  if (result >= 0 && hw_device_ctx != nullptr && f->format != AV_PIX_FMT_NONE &&
      av_pix_fmt_desc_get(static_cast<AVPixelFormat>(f->format))->flags & AV_PIX_FMT_FLAG_HWACCEL) {
    result = transferHwFrameToSoftware(f);
  }

  return result;
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

  // loop to pull frames from the AVFilter stack
  while ((retrieve_code = av_buffersink_get_frame(buffersink_ctx, *f)) == AVERROR(EAGAIN)) {
    // retrieve frame from decoder
    read_code = RetrieveFrameFromDecoder(frame_);

    if (read_code >= 0) {
      // we retrieved a decoded video frame, which we will send to the AVFilter stack to convert to RGBA (with other
      // adjustments if necessary)

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
      }
      break;
    }
  }

  if (read_code == AVERROR_EOF) {
    return AVERROR_EOF;
  }
  return retrieve_code;
}
