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

#include "project/projectelements.h"
#include "rendering/renderfunctions.h"
#include "global/config.h"
#include "global/debug.h"

void Cacher::CacheVideoWorker() {

  // is this media a still image?
  if (clip->media_stream()->infinite_length) {

    // for efficiency, we do slightly different things for a still image

    // if we already queued a frame, we don't actually need to cache anything, so we only retrieve a frame if not
    if (queue_.size() == 0) {

      // retrieve a single frame

      // main thread waits until cacher starts fully, wake it up here
      WakeMainThread();

      AVFrame* still_image_frame;

      if (RetrieveFrameAndProcess(&still_image_frame) >= 0) {

        queue_.lock();
        queue_.append(still_image_frame);
        queue_.unlock();

        SetRetrievedFrame(still_image_frame);
      }

    }

  } else {
    // this media is not a still image and will require more complex caching

    // main thread waits until cacher starts fully, wake it up here
    WakeMainThread();

    // determine if this media is reversed, which will affect how the queue is constructed
    bool reversed = IsReversed();

    // get the timestamp we want in terms of the media's timebase
    int64_t target_pts = seconds_to_timestamp(clip, playhead_to_clip_seconds(clip, playhead_));

    // get the value of one second in terms of the media's timebase
    int64_t second_pts = seconds_to_timestamp(clip, 1); // FIXME: possibly magic number?

    // check which range of frames we have in the queue
    int64_t earliest_pts = INT64_MAX;
    int64_t latest_pts = INT64_MIN;
    int frames_greater_than_target = 0;

    for (int i=0;i<queue_.size();i++) {
      // cache earliest and latest timestamps in the queue
      earliest_pts = qMin(earliest_pts, queue_.at(i)->pts);
      latest_pts = qMax(latest_pts, queue_.at(i)->pts);

      // count upcoming frames
      if (queue_.at(i)->pts > target_pts) {
        frames_greater_than_target++;
      }
    }

    // If we have to seek ahead, we may want to re-use the frame we retrieved later in the pipeline.
    AVFrame* decoded_frame;
    bool have_existing_frame_to_use = false;
    bool seeked_to_zero = false;

    // check if the frame is within this queue or if we'll have to seek elsewhere to get it
    // (we check for one second of time after latest_pts, because if it's within that range it'll likely be faster to
    // play up to that frame than seek to it)
    if (target_pts < earliest_pts || target_pts > latest_pts + second_pts || queue_.size() == 0) {
      // we need to seek to retrieve this frame

      int retrieve_code;
      int64_t seek_ts = target_pts;
      int64_t zero = 0;

      // Some formats don't seek reliably to the last keyframe, as a result we need to seek in a loop to ensure we
      // get a frame prior to the timestamp
      do {

        // if we already allocated a frame here, we'd better free it
        if (have_existing_frame_to_use) {
          av_frame_free(&decoded_frame);
        }

        // If we already seeked to a timestamp of zero, there's no further we can go, so we have to exit the loop if so
        seeked_to_zero = (seek_ts == 0);

        avcodec_flush_buffers(codecCtx);
        av_seek_frame(formatCtx, clip->media_stream_index(), seek_ts, AVSEEK_FLAG_BACKWARD);

        retrieve_code = RetrieveFrameAndProcess(&decoded_frame);

        //qDebug() << "Target:" << target_pts << "Seek:" << seek_ts << "Frame:" << decoded_frame->pts;

        seek_ts = qMax(zero, seek_ts - second_pts);

        have_existing_frame_to_use = true;
      } while (retrieve_code >= 0
              && (decoded_frame->pts == AV_NOPTS_VALUE || decoded_frame->pts > target_pts)
              && !seeked_to_zero);

      // also we assume none of the frames in the queue are usable
      queue_.lock();
      queue_.clear();
      queue_.unlock();

      // reset upcoming frame count and latest pts for later calculations
      frames_greater_than_target = 0;
      latest_pts = INT64_MIN;
    }

    // get values on old frames to remove from the queue

    // for FRAME_QUEUE_TYPE_SECONDS, this is used to store the maximum timestamp
    // for FRAME_QUEUE_TYPE_FRAMES, this is used to store the maximum number of frames that can be added
    int64_t minimum_ts;

    // check if we can add more frames to this queue or not

    // for FRAME_QUEUE_TYPE_SECONDS, this is used to store the maximum timestamp
    // for FRAME_QUEUE_TYPE_FRAMES, this is used to store the maximum number of frames that can be added
    int64_t maximum_ts;

    // Get queue configuration
    int previous_queue_type, upcoming_queue_type;
    double previous_queue_size, upcoming_queue_size;

    // For reversed playback, we flip the queue stats as "upcoming" frames are going to be played before the "previous"
    // frames now
    if (reversed) {
      previous_queue_type = olive::CurrentConfig.upcoming_queue_type;
      previous_queue_size = olive::CurrentConfig.upcoming_queue_size;
      upcoming_queue_type = olive::CurrentConfig.previous_queue_type;
      upcoming_queue_size = olive::CurrentConfig.previous_queue_size;
    } else {
      previous_queue_type = olive::CurrentConfig.previous_queue_type;
      previous_queue_size = olive::CurrentConfig.previous_queue_size;
      upcoming_queue_type = olive::CurrentConfig.upcoming_queue_type;
      upcoming_queue_size = olive::CurrentConfig.upcoming_queue_size;
    }

    // Determine "previous" queue statistics
    if (previous_queue_type == olive::FRAME_QUEUE_TYPE_FRAMES) {
      // get the maximum number of previous frames that can be in the queue
      minimum_ts = qCeil(previous_queue_size);
    } else {
      // get the minimum frame timestamp that can be added to the queue
      minimum_ts = qRound(target_pts - second_pts * previous_queue_size);
    }

    // Determine "upcoming" queue statistics
    if (upcoming_queue_type == olive::FRAME_QUEUE_TYPE_FRAMES) {
      maximum_ts = qCeil(upcoming_queue_size);
    } else {
      // get the maximum frame timestamp that can be added to the queue
      maximum_ts = qRound(target_pts + second_pts * upcoming_queue_size);
    }

    // if we already have the maximum number of upcoming frames, don't bother running the retrieving any frames at all
    bool start_loop = true;
    if ((upcoming_queue_type == olive::FRAME_QUEUE_TYPE_FRAMES && frames_greater_than_target >= maximum_ts)
        || (upcoming_queue_type == olive::FRAME_QUEUE_TYPE_SECONDS && latest_pts > maximum_ts)) {
      start_loop = false;
    }


    if (start_loop) {

      interrupt_ = false;
      do {

        // retrieve raw RGBA frame from decoder + filter stack
        int retrieve_code = 0;

        // if we retrieved a perfectly good frame earlier by checking the seek, use that here
        if (!have_existing_frame_to_use) {
          retrieve_code = RetrieveFrameAndProcess(&decoded_frame);
        } else {
          have_existing_frame_to_use = false;
        }

        if (retrieve_code < 0 && retrieve_code != AVERROR_EOF) {

          // for some reason we were unable to retrieve a frame, likely a decoder error so we report it
          // again, an EOF isn't an "error" but will how we add frames (see below)

          qCritical() << "Failed to retrieve frame from buffersink." << retrieve_code;
          break;

        } else if (decoded_frame->pts != AV_NOPTS_VALUE) {

          // check if this frame exceeds the minimum timestamp
          if (previous_queue_type == olive::FRAME_QUEUE_TYPE_SECONDS
              && decoded_frame->pts < minimum_ts) {

            // if so, we don't need it
            av_frame_free(&decoded_frame);

          } else {

            if (retrieved_frame == nullptr) {
              if (decoded_frame->pts == target_pts) {

                // We retrieved the exact frame we're looking for

                SetRetrievedFrame(decoded_frame);

              } else if (decoded_frame->pts > target_pts) {

                if (queue_.size() > 0) {

                  SetRetrievedFrame(queue_.last());

                } else if (seeked_to_zero) {

                  // If this flag is set but we still got a frame after the target timestamp, it means this was somehow
                  // the earliest frame we could get
                  SetRetrievedFrame(decoded_frame);
                  seeked_to_zero = false;

                }

              }
            }

            // add the frame to the queue
            queue_.lock();
            queue_.append(decoded_frame);
            queue_.unlock();

            // check the amount of previous frames in the queue by using the current queue size for if we need to
            // remove any old entries (assumes the queue is chronological)
            if (previous_queue_type == olive::FRAME_QUEUE_TYPE_FRAMES) {

              int previous_frame_count = 0;

              if (decoded_frame->pts < target_pts) {
                // if this frame is before the target frame, make sure we don't add too many of them
                previous_frame_count = queue_.size();
              } else {
                // if this frame is after the target frame, clean up any previous frames before it
                // TODO is there a faster way to do this?

                for (int i=0;i<queue_.size();i++) {
                  if (queue_.at(i)->pts > target_pts) {
                    break;
                  } else {
                    previous_frame_count++;
                  }
                }

              }

              // remove frames while the amount of previous frames exceeds the maximum
              while (previous_frame_count > minimum_ts) {
                queue_.lock();
                queue_.removeFirst();
                queue_.unlock();
                previous_frame_count--;
              }

            }

            // check if the queue is full according to olive::CurrentConfig
            if (upcoming_queue_type == olive::FRAME_QUEUE_TYPE_FRAMES) {

              // if this frame is later than the target, it's an "upcoming" frame
              if (decoded_frame->pts > target_pts) {

                // we started a count of upcoming frames above, we can continue it here
                frames_greater_than_target++;

                // compare upcoming frame count with maximum upcoming frames (maximum_ts)
                if (frames_greater_than_target >= maximum_ts) {
                  break;
                }
              }

            } else if (decoded_frame->pts > maximum_ts) { // for `upcoming_queue_type == olive::FRAME_QUEUE_TYPE_SECONDS`
              break;
            }

          }



        } else {

          // Frame has no timestamp. For still images (JPEG, PNG, etc.) this is expected —
          // assign PTS 0 so the frame is usable. For video codecs this can happen after
          // seeking; in that case use the target PTS as a reasonable approximation.

          decoded_frame->pts = target_pts;

          if (retrieve_code == AVERROR_EOF) {
            // No more frames — this was the last one, use it directly
            if (retrieved_frame == nullptr) {
              SetRetrievedFrame(decoded_frame);
            }
            queue_.lock();
            queue_.append(decoded_frame);
            queue_.unlock();
            break;
          }

        }
      } while (!interrupt_);

    }

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

int Cacher::RetrieveFrameFromDecoder(AVFrame* f) {
  int result = 0;
  int receive_ret;

  // do we need to retrieve a new packet for a new frame?
  av_frame_unref(f);
  while ((receive_ret = avcodec_receive_frame(codecCtx, f)) == AVERROR(EAGAIN)) {
    int read_ret = 0;
    do {
      if (pkt->buf != nullptr) {
        av_packet_unref(pkt);
      }
      read_ret = av_read_frame(formatCtx, pkt);
    } while (read_ret >= 0 && pkt->stream_index != clip->media_stream_index());

    if (read_ret >= 0) {
      int send_ret = avcodec_send_packet(codecCtx, pkt);
      if (send_ret < 0) {
        qCritical() << "Failed to send packet to decoder." << send_ret;
        return send_ret;
      }
    } else {
      if (read_ret == AVERROR_EOF) {
        int send_ret = avcodec_send_packet(codecCtx, nullptr);
        if (send_ret < 0) {
          qCritical() << "Failed to send packet to decoder." << send_ret;
          return send_ret;
        }
      } else {
        qCritical() << "Could not read frame." << read_ret;
        return read_ret; // skips trying to find a frame at all
      }
    }
  }
  if (receive_ret < 0) {
    if (receive_ret != AVERROR_EOF) qCritical() << "Failed to receive packet from decoder." << receive_ret;
    result = receive_ret;
  }

  // If the frame is in hardware format, transfer to software
  if (result >= 0 && hw_device_ctx != nullptr && f->format != AV_PIX_FMT_NONE
      && av_pix_fmt_desc_get(static_cast<AVPixelFormat>(f->format))->flags & AV_PIX_FMT_FLAG_HWACCEL) {
    AVFrame* sw_frame = av_frame_alloc();
    if (av_hwframe_transfer_data(sw_frame, f, 0) < 0) {
      qWarning() << "Failed to transfer hw frame to software, disabling hwaccel for this clip";
      av_frame_free(&sw_frame);
    } else {
      av_frame_copy_props(sw_frame, f);
      av_frame_unref(f);
      av_frame_move_ref(f, sw_frame);
      av_frame_free(&sw_frame);
    }
  }

  return result;
}

int Cacher::RetrieveFrameAndProcess(AVFrame **f)
{
  // error codes from FFmpeg
  int retrieve_code, read_code, send_code;

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
