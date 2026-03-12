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
#include "rendering/audio.h"
#include "rendering/renderfunctions.h"
#include "global/debug.h"

// Enable verbose audio messages - good for debugging reversed audio
//#define AUDIOWARNINGS

double bytes_to_seconds(qint64 nb_bytes, int nb_channels, int sample_rate) {
  return (double(nb_bytes >> 1) / nb_channels / sample_rate);
}

void apply_audio_effects(Clip* clip, double timecode_start, AVFrame* frame, int nb_bytes, QVector<Clip*> nests) {
  // perform all audio effects
  double timecode_end;
  timecode_end = timecode_start + bytes_to_seconds(nb_bytes, frame->ch_layout.nb_channels, frame->sample_rate);

  for (const auto & effect : clip->effects) {
    Effect* e = effect.get();
    if (e->IsEnabled()) {
      e->process_audio(timecode_start, timecode_end, frame->data[0], nb_bytes, 2);
    }
  }
  if (clip->opening_transition != nullptr) {
    if (clip->media() != nullptr && clip->media()->get_type() == MEDIA_TYPE_FOOTAGE) {
      double transition_start = (clip->clip_in(true) / clip->sequence->frame_rate);
      double transition_end = (clip->clip_in(true) + clip->opening_transition->get_length()) / clip->sequence->frame_rate;
      if (timecode_end < transition_end) {
        double adjustment = transition_end - transition_start;
        double adjusted_range_start = (timecode_start - transition_start) / adjustment;
        double adjusted_range_end = (timecode_end - transition_start) / adjustment;
        clip->opening_transition->process_audio(adjusted_range_start, adjusted_range_end, frame->data[0], nb_bytes, kTransitionOpening);
      }
    }
  }
  if (clip->closing_transition != nullptr) {
    if (clip->media() != nullptr && clip->media()->get_type() == MEDIA_TYPE_FOOTAGE) {
      long length_with_transitions = clip->timeline_out(true) - clip->timeline_in(true);
      double transition_start = (clip->clip_in(true) + length_with_transitions - clip->closing_transition->get_length()) / clip->sequence->frame_rate;
      double transition_end = (clip->clip_in(true) + length_with_transitions) / clip->sequence->frame_rate;
      if (timecode_start > transition_start) {
        double adjustment = transition_end - transition_start;
        double adjusted_range_start = (timecode_start - transition_start) / adjustment;
        double adjusted_range_end = (timecode_end - transition_start) / adjustment;
        clip->closing_transition->process_audio(adjusted_range_start, adjusted_range_end, frame->data[0], nb_bytes, kTransitionClosing);
      }
    }
  }

  if (!nests.isEmpty()) {
    Clip* next_nest = nests.last();
    nests.removeLast();
    apply_audio_effects(next_nest,
                        timecode_start + (double(clip->timeline_in(true)-clip->clip_in(true))/clip->sequence->frame_rate),
                        frame,
                        nb_bytes,
                        nests);
  }
}

#define AUDIO_BUFFER_PADDING 2048
void Cacher::CacheAudioWorker() {
  // main thread waits until cacher starts fully, wake it up here
  WakeMainThread();

  bool audio_just_reset = false;

  // for audio clips, something may have triggered an audio reset (common if the user seeked)
  if (audio_reset_) {
    Reset();
    audio_reset_ = false;
    audio_just_reset = true;
  }

  long timeline_in = clip->timeline_in(true);
  long timeline_out = clip->timeline_out(true);
  long target_frame = audio_target_frame;

  bool temp_reverse = (playback_speed_ < 0);
  bool reverse_audio = IsReversed();

  long frame_skip = 0;
  double last_fr = clip->sequence->frame_rate;
  if (!nests_.isEmpty()) {
    for (int i=nests_.size()-1;i>=0;i--) {
      timeline_in = rescale_frame_number(timeline_in, last_fr, nests_.at(i)->sequence->frame_rate) + nests_.at(i)->timeline_in(true) - nests_.at(i)->clip_in(true);
      timeline_out = rescale_frame_number(timeline_out, last_fr, nests_.at(i)->sequence->frame_rate) + nests_.at(i)->timeline_in(true) - nests_.at(i)->clip_in(true);
      target_frame = rescale_frame_number(target_frame, last_fr, nests_.at(i)->sequence->frame_rate) + nests_.at(i)->timeline_in(true) - nests_.at(i)->clip_in(true);

      timeline_out = qMin(timeline_out, nests_.at(i)->timeline_out(true));

      frame_skip = rescale_frame_number(frame_skip, last_fr, nests_.at(i)->sequence->frame_rate);

      long validator = nests_.at(i)->timeline_in(true) - timeline_in;
      if (validator > 0) {
        frame_skip += validator;
        //timeline_in = nests_.at(i)->timeline_in(true);
      }

      last_fr = nests_.at(i)->sequence->frame_rate;
    }
  }

  if (temp_reverse) {
    long seq_end = olive::ActiveSequence->getEndFrame();
    timeline_in = seq_end - timeline_in;
    timeline_out = seq_end - timeline_out;
    target_frame = seq_end - target_frame;

    long temp = timeline_in;
    timeline_in = timeline_out;
    timeline_out = temp;
  }

  while (true) {
    AVFrame* frame;
    int nb_bytes = INT_MAX;

    if (clip->media() == nullptr) {
      frame = frame_;
      nb_bytes = frame->nb_samples * av_get_bytes_per_sample(static_cast<AVSampleFormat>(frame->format)) * frame->ch_layout.nb_channels;
      while ((frame_sample_index_ == -1 || frame_sample_index_ >= nb_bytes) && nb_bytes > 0) {
        // create "new frame"
        memset(frame_->data[0], 0, nb_bytes);
        apply_audio_effects(clip, bytes_to_seconds(frame->pts, frame->ch_layout.nb_channels, frame->sample_rate), frame, nb_bytes, nests_);
        frame_->pts += nb_bytes;
        frame_sample_index_ = 0;
        if (audio_buffer_write == 0) {
          audio_buffer_write = get_buffer_offset_from_frame(last_fr, qMax(timeline_in, target_frame));
        }
        int offset = audio_ibuffer_read - audio_buffer_write;
        if (offset > 0) {
          audio_buffer_write += offset;
          frame_sample_index_ += offset;
        }
      }
    } else if (clip->media()->get_type() == MEDIA_TYPE_FOOTAGE) {
      // skip audio processing if filter graph failed to initialize
      if (filter_graph == nullptr) {
        break;
      }

      double timebase = av_q2d(stream->time_base);

      frame = queue_.at(0);

      // retrieve frame
      bool new_frame = false;
      while ((frame_sample_index_ == -1 || frame_sample_index_ >= nb_bytes) && nb_bytes > 0) {

        // no more audio left in frame, get a new one
        if (!reached_end) {
          int loop = 0;

          if (reverse_audio && !audio_just_reset) {
            avcodec_flush_buffers(codecCtx);
            reached_end = false;
            int64_t backtrack_seek = qMax(reverse_target_ - static_cast<int64_t>(av_q2d(av_inv_q(stream->time_base))),
                                          static_cast<int64_t>(0));
            av_seek_frame(formatCtx, stream->index, backtrack_seek, AVSEEK_FLAG_BACKWARD);
#ifdef AUDIOWARNINGS
            if (backtrack_seek == 0) {
              dout << "backtracked to 0";
            }
#endif
          }

          do {
            av_frame_unref(frame);

            int ret;

            while ((ret = av_buffersink_get_frame(buffersink_ctx, frame)) == AVERROR(EAGAIN)) {
              ret = RetrieveFrameFromDecoder(frame_);
              if (ret >= 0) {
                if ((ret = av_buffersrc_add_frame_flags(buffersrc_ctx, frame_, AV_BUFFERSRC_FLAG_KEEP_REF)) < 0) {
                  qCritical() << "Could not feed filtergraph -" << ret;
                  break;
                }
              } else {
                if (ret == AVERROR_EOF) {
#ifdef AUDIOWARNINGS
                  dout << "reached EOF while reading";
#endif
                  // TODO revise usage of reached_end in audio
                  if (!reverse_audio) {
                    reached_end = true;
                  } else {
                  }
                } else {
                  qWarning() << "Raw audio frame data could not be retrieved." << ret;
                  reached_end = true;
                }
                break;
              }
            }

            if (ret < 0) {
              if (ret != AVERROR_EOF) {
                qCritical() << "Could not pull from filtergraph";
                reached_end = true;
                break;
              } else {
#ifdef AUDIOWARNINGS
                dout << "reached EOF while pulling from filtergraph";
#endif
                if (!reverse_audio) break;
              }
            }

            if (reverse_audio) {
              if (loop > 1) {
                AVFrame* rev_frame = queue_.at(1);
                if (ret != AVERROR_EOF) {
                  if (loop == 2) {
#ifdef AUDIOWARNINGS
                    dout << "starting rev_frame";
#endif
                    rev_frame->nb_samples = 0;
                    rev_frame->pts = frame_->pts;
                  }
                  int offset = rev_frame->nb_samples * av_get_bytes_per_sample(static_cast<AVSampleFormat>(rev_frame->format)) * rev_frame->ch_layout.nb_channels;
#ifdef AUDIOWARNINGS
                  dout << "offset 1:" << offset;
                  dout << "retrieved samples:" << frame->nb_samples << "size:" << (frame->nb_samples * av_get_bytes_per_sample(static_cast<AVSampleFormat>(frame->format)) * frame->ch_layout.nb_channels);
#endif
                  memcpy(
                        rev_frame->data[0]+offset,
                      frame->data[0],
                      (frame->nb_samples * av_get_bytes_per_sample(static_cast<AVSampleFormat>(frame->format)) * frame->ch_layout.nb_channels)
                      );
#ifdef AUDIOWARNINGS
                  dout << "pts:" << frame_->pts << "dur:" << frame_->pkt_duration << "rev_target:" << reverse_target << "offset:" << offset << "limit:" << rev_frame->linesize[0];
#endif
                }

                rev_frame->nb_samples += frame->nb_samples;

                if ((frame_->pts >= reverse_target_) || (ret == AVERROR_EOF)) {
                  /*
#ifdef AUDIOWARNINGS
                  dout << "time for the end of rev cache" << rev_frame->nb_samples << clip->rev_target << frame_->pts << frame_->pkt_duration << frame_->nb_samples;
                  dout << "diff:" << (frame_->pkt_pts + frame_->pkt_duration) - reverse_target;
#endif
                  int cutoff = qRound64((((frame_->pkt_pts + frame_->pkt_duration) - reverse_target) * timebase) * audio_output->format().sampleRate());
                  if (cutoff > 0) {
#ifdef AUDIOWARNINGS
                    dout << "cut off" << cutoff << "samples (rate:" << audio_output->format().sampleRate() << ")";
#endif
                    rev_frame->nb_samples -= cutoff;
                  }
*/

#ifdef AUDIOWARNINGS
                  dout << "pre cutoff deets::: rev_frame.pts:" << rev_frame->pts << "rev_frame.nb_samples" << rev_frame->nb_samples << "rev_target:" << reverse_target;
#endif
                  double playback_speed_ = clip->speed().value * clip->media()->to_footage()->speed;
                  rev_frame->nb_samples = qRound64(double(reverse_target_ - rev_frame->pts) * timebase * (current_audio_freq() / playback_speed_));
#ifdef AUDIOWARNINGS
                  dout << "post cutoff deets::" << rev_frame->nb_samples;
#endif

                  int frame_size = rev_frame->nb_samples * rev_frame->ch_layout.nb_channels * av_get_bytes_per_sample(static_cast<AVSampleFormat>(rev_frame->format));
                  int half_frame_size = frame_size >> 1;

                  int sample_size = rev_frame->ch_layout.nb_channels*av_get_bytes_per_sample(static_cast<AVSampleFormat>(rev_frame->format));
                  char* temp_chars = new char[sample_size];
                  for (int i=0;i<half_frame_size;i+=sample_size) {
                    memcpy(temp_chars, &rev_frame->data[0][i], sample_size);

                    memcpy(&rev_frame->data[0][i], &rev_frame->data[0][frame_size-i-sample_size], sample_size);

                    memcpy(&rev_frame->data[0][frame_size-i-sample_size], temp_chars, sample_size);
                  }
                  delete [] temp_chars;

                  reverse_target_ = rev_frame->pts;
                  frame = rev_frame;
                  break;
                }
              }

              loop++;

#ifdef AUDIOWARNINGS
              dout << "loop" << loop;
#endif
            } else {
              frame->pts = frame_->pts;
              break;
            }
          } while (true);
        } else {
          // if there is no more data in the file, we flush the remainder out of swresample
          break;
        }

        new_frame = true;

        if (frame_sample_index_ < 0) {
          frame_sample_index_ = 0;
        } else {
          frame_sample_index_ -= nb_bytes;
        }

        nb_bytes = frame->nb_samples * av_get_bytes_per_sample(static_cast<AVSampleFormat>(frame->format)) * frame->ch_layout.nb_channels;

        if (audio_just_reset) {
          // get precise sample offset for the elected clip_in from this audio frame
          double target_sts = playhead_to_clip_seconds(clip, audio_target_frame);

          int64_t stream_start = qMax(static_cast<int64_t>(0), stream->start_time);
          double frame_sts = ((frame->pts - stream_start) * timebase);

          int nb_samples = qRound64((target_sts - frame_sts)*current_audio_freq());
          frame_sample_index_ = nb_samples * 4;
#ifdef AUDIOWARNINGS
          dout << "fsts:" << frame_sts << "tsts:" << target_sts << "nbs:" << nb_samples << "nbb:" << nb_bytes << "rev_targetToSec:" << (reverse_target * timebase);
          dout << "fsi-calc:" << frame_sample_index;
#endif
          if (reverse_audio) frame_sample_index_ = nb_bytes - frame_sample_index_;
          audio_just_reset = false;
        }

#ifdef AUDIOWARNINGS
        dout << "fsi-post-post:" << frame_sample_index;
#endif
        if (audio_buffer_write == 0) {
          audio_buffer_write = get_buffer_offset_from_frame(last_fr, qMax(timeline_in, target_frame));

          if (frame_skip > 0) {
            int target = get_buffer_offset_from_frame(last_fr, qMax(timeline_in + frame_skip, target_frame));
            frame_sample_index_ += (target - audio_buffer_write);
            audio_buffer_write = target;
          }
        }

        int offset = audio_ibuffer_read - audio_buffer_write;
        if (offset > 0) {
          audio_buffer_write += offset;
          frame_sample_index_ += offset;
        }

        // try to correct negative fsi
        if (frame_sample_index_ < 0) {
          audio_buffer_write -= frame_sample_index_;
          frame_sample_index_ = 0;
        }
      }

      if (reverse_audio) frame = queue_.at(1);

#ifdef AUDIOWARNINGS
      dout << "j" << frame_sample_index << nb_bytes;
#endif

      // apply any audio effects to the data
      if (nb_bytes == INT_MAX) nb_bytes = frame->nb_samples * av_get_bytes_per_sample(static_cast<AVSampleFormat>(frame->format)) * frame->ch_layout.nb_channels;
      if (new_frame) {
        apply_audio_effects(clip, bytes_to_seconds(audio_buffer_write, 2, current_audio_freq()) + audio_ibuffer_timecode + ((double)clip->clip_in(true)/clip->sequence->frame_rate) - ((double)timeline_in/last_fr), frame, nb_bytes, nests_);
      }
    }

    // mix audio into internal buffer
    if (frame->nb_samples == 0) {
      break;
    } else {
      qint64 buffer_timeline_out = get_buffer_offset_from_frame(clip->sequence->frame_rate, timeline_out);

      audio_write_lock.lock();

      int sample_skip = 4*qMax(0, qAbs(playback_speed_)-1);
      int sample_byte_size = av_get_bytes_per_sample(static_cast<AVSampleFormat>(frame->format));

      while (frame_sample_index_ < nb_bytes
             && audio_buffer_write < audio_ibuffer_read+(audio_ibuffer_size>>1)
             && audio_buffer_write < buffer_timeline_out) {
        for (int i=0;i<frame->ch_layout.nb_channels;i++) {
          int upper_byte_index = (audio_buffer_write+1)%audio_ibuffer_size;
          int lower_byte_index = (audio_buffer_write)%audio_ibuffer_size;
          qint16 old_sample = static_cast<qint16>((audio_ibuffer[upper_byte_index] & 0xFF) << 8 | (audio_ibuffer[lower_byte_index] & 0xFF));
          qint16 new_sample = static_cast<qint16>((frame->data[0][frame_sample_index_+1] & 0xFF) << 8 | (frame->data[0][frame_sample_index_] & 0xFF));
          qint16 mixed_sample = mix_audio_sample(old_sample, new_sample);

          audio_ibuffer[upper_byte_index] = quint8((mixed_sample >> 8) & 0xFF);
          audio_ibuffer[lower_byte_index] = quint8(mixed_sample & 0xFF);

          audio_buffer_write+=sample_byte_size;
          frame_sample_index_+=sample_byte_size;
        }

        frame_sample_index_ += sample_skip;

        if (audio_reset_) break;
      }

#ifdef AUDIOWARNINGS
      if (audio_buffer_write >= buffer_timeline_out) dout << "timeline out at fsi" << frame_sample_index << "of frame ts" << frame_->pts;
#endif

      audio_write_lock.unlock();

      if (audio_reset_) return;

      if (scrubbing_) {
        if (audio_thread != nullptr) audio_thread->notifyReceiver();
      }

      if (frame_sample_index_ >= nb_bytes) {
        frame_sample_index_ = -1;
      } else {
        // assume we have no more data to send
        break;
      }

      //			dout << "ended" << frame_sample_index << nb_bytes;
    }
    if (reached_end) {
      frame->nb_samples = 0;
    }
    if (scrubbing_) {
      break;
    }
  }

  // If there's a QObject waiting for audio to be rendered, wake it now
  WakeAudioWakeObject();
}

bool Cacher::IsReversed()
{
  // Here, the Clip reverse and reversed playback speed cancel each other out to produce normal playback
  return (clip->reversed() != playback_speed_ < 0);
}

void Cacher::Reset() {
  // if we seek to a whole other place in the timeline, we'll need to reset the cache with new values
  if (clip->media() == nullptr) {
    if (clip->track() >= 0) {
      // a null-media audio clip is usually an auto-generated sound clip such as Tone or Noise
      reached_end = false;
      audio_target_frame = playhead_;
      frame_sample_index_ = -1;
      frame_->pts = 0;
    }
  } else {

    const FootageStream* ms = clip->media_stream();
    if (stream->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
      // flush ffmpeg codecs
      avcodec_flush_buffers(codecCtx);
      reached_end = false;

      // seek (target_frame represents timeline timecode in frames, not clip timecode)

      int64_t timestamp = qRound64(playhead_to_clip_seconds(clip, playhead_) / av_q2d(stream->time_base));

      bool temp_reverse = (playback_speed_ < 0);
      if (clip->reversed() != temp_reverse) {
        reverse_target_ = timestamp;
        timestamp -= av_q2d(av_inv_q(stream->time_base));
#ifdef AUDIOWARNINGS
        dout << "seeking to" << timestamp << "(originally" << reverse_target << ")";
      } else {
        dout << "reset called; seeking to" << timestamp;
#endif
      }
      av_seek_frame(formatCtx, ms->file_index, timestamp, AVSEEK_FLAG_BACKWARD);
      audio_target_frame = playhead_;
      frame_sample_index_ = -1;
    }
  }
}
