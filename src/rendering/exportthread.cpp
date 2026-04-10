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

#include "exportthread.h"

extern "C" {
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavutil/opt.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
}

#include <QApplication>
#include <QDateTime>
#include <QPainter>
#include <QtMath>

#include "core/appcontext.h"
#include "engine/sequence.h"
#include "global/debug.h"
#include "rendering/audio.h"
#include "rendering/renderfunctions.h"
#include "rendering/renderthread.h"

#ifdef __GLIBC__
#include <malloc.h>
#endif

ExportThread::ExportThread(Sequence* seq, const ExportParams& params, const VideoCodecParams& vparams, QObject* parent)
    : QThread(parent), seq_(seq), params_(params), vcodec_params_(vparams), interrupt_(false) {}

void ExportThread::setGlFallbackSurface(QOffscreenSurface* surface) { gl_fallback_surface_ = surface; }

bool ExportThread::Encode(AVFormatContext* ofmt_ctx, AVCodecContext* codec_ctx, AVFrame* frame, AVPacket* packet,
                          AVStream* stream) {
  ret = avcodec_send_frame(codec_ctx, frame);
  if (ret < 0) {
    qCritical() << "Failed to send frame to encoder." << ret;
    export_error = tr("failed to send frame to encoder (%1)").arg(QString::number(ret));
    return false;
  }

  while (ret >= 0) {
    ret = avcodec_receive_packet(codec_ctx, packet);
    if (ret == AVERROR(EAGAIN)) {
      return true;
    } else if (ret < 0) {
      if (ret != AVERROR_EOF) {
        qCritical() << "Failed to receive packet from encoder." << ret;
        export_error = tr("failed to receive packet from encoder (%1)").arg(QString::number(ret));
      }
      return false;
    }

    packet->stream_index = stream->index;

    av_packet_rescale_ts(packet, codec_ctx->time_base, stream->time_base);

    ret = av_interleaved_write_frame(ofmt_ctx, packet);
    av_packet_unref(packet);
    if (ret < 0) {
      qCritical() << "Failed to write packet." << ret;
      return false;
    }
  }
  return true;
}

bool ExportThread::SetupVideo() {
  // if video is disabled, no setup necessary
  if (!params_.video_enabled) return true;

  // find video encoder
  vcodec = avcodec_find_encoder(static_cast<enum AVCodecID>(params_.video_codec));
  if (!vcodec) {
    qCritical() << "Could not find video encoder";
    export_error = tr("could not video encoder for %1").arg(QString::number(params_.video_codec));
    return false;
  }

  // create video stream
  video_stream = avformat_new_stream(fmt_ctx, vcodec);
  if (!video_stream) {
    qCritical() << "Could not allocate video stream";
    export_error = tr("could not allocate video stream");
    return false;
  }
  video_stream->id = 0;

  // allocate context
  //	vcodec_ctx = video_stream->codec;
  vcodec_ctx = avcodec_alloc_context3(vcodec);
  if (!vcodec_ctx) {
    qCritical() << "Could not allocate video encoding context";
    export_error = tr("could not allocate video encoding context");
    return false;
  }

  // setup context
  vcodec_ctx->codec_id = static_cast<enum AVCodecID>(params_.video_codec);
  vcodec_ctx->codec_type = AVMEDIA_TYPE_VIDEO;
  vcodec_ctx->width = params_.video_width;
  vcodec_ctx->height = params_.video_height;
  vcodec_ctx->sample_aspect_ratio = {1, 1};
  vcodec_ctx->pix_fmt = static_cast<AVPixelFormat>(vcodec_params_.pix_fmt);
  vcodec_ctx->framerate = av_d2q(params_.video_frame_rate, INT_MAX);
  if (params_.video_compression_type == COMPRESSION_TYPE_CBR) {
    vcodec_ctx->bit_rate = qRound(params_.video_bitrate * 1000000);
  }
  vcodec_ctx->time_base = av_inv_q(vcodec_ctx->framerate);
  video_stream->time_base = vcodec_ctx->time_base;

  if (fmt_ctx->oformat->flags & AVFMT_GLOBALHEADER) {
    vcodec_ctx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
  }

  // Some codecs require special settings so we set that up here
  switch (vcodec_ctx->codec_id) {
    /// H.264 specific settings
    case AV_CODEC_ID_H264:
    case AV_CODEC_ID_H265:
      switch (params_.video_compression_type) {
        case COMPRESSION_TYPE_CFR:
          av_opt_set(vcodec_ctx->priv_data, "crf", QString::number(static_cast<int>(params_.video_bitrate)).toUtf8(),
                     AV_OPT_SEARCH_CHILDREN);
          break;
      }
      break;

    default:
      break;
  }

  // Set export to be multithreaded
  AVDictionary* opts = nullptr;
  if (vcodec_params_.threads == 0) {
    av_dict_set(&opts, "threads", "auto", 0);
  } else {
    av_dict_set(&opts, "threads", QString::number(vcodec_params_.threads).toUtf8(), 0);
  }

  // Open video encoder
  ret = avcodec_open2(vcodec_ctx, vcodec, &opts);
  av_dict_free(&opts);
  if (ret < 0) {
    qCritical() << "Could not open output video encoder." << ret;
    export_error = tr("could not open output video encoder (%1)").arg(QString::number(ret));
    return false;
  }

  // Copy video encoder parameters to output stream
  ret = avcodec_parameters_from_context(video_stream->codecpar, vcodec_ctx);
  if (ret < 0) {
    qCritical() << "Could not copy video encoder parameters to output stream." << ret;
    export_error = tr("could not copy video encoder parameters to output stream (%1)").arg(QString::number(ret));
    return false;
  }

  // Create raw AVFrame that will contain the RGBA buffer straight from compositing
  if (seq_ == nullptr) {
    export_error = tr("no active sequence");
    return false;
  }
  video_frame = av_frame_alloc();
  video_frame->format = AV_PIX_FMT_RGBA;
  video_frame->width = seq_->width;
  video_frame->height = seq_->height;
  ret = av_frame_get_buffer(video_frame, 0);
  if (ret < 0) {
    qCritical() << "Could not allocate video frame buffer." << ret;
    export_error = tr("could not allocate video frame buffer (%1)").arg(QString::number(ret));
    return false;
  }

  video_pkt = av_packet_alloc();

  // Set up conversion context
  sws_ctx = sws_getContext(seq_->width, seq_->height, AV_PIX_FMT_RGBA, params_.video_width, params_.video_height,
                           vcodec_ctx->pix_fmt, SWS_BILINEAR, nullptr, nullptr, nullptr);

  return true;
}

bool ExportThread::SetupAudio() {
  // if video is disabled, no setup necessary
  if (!params_.audio_enabled) return true;

  // Find encoder for this codec
  acodec = avcodec_find_encoder(static_cast<AVCodecID>(params_.audio_codec));
  if (!acodec) {
    qCritical() << "Could not find audio encoder";
    export_error = tr("could not audio encoder for %1").arg(QString::number(params_.audio_codec));
    return false;
  }

  // Allocate audio stream
  audio_stream = avformat_new_stream(fmt_ctx, acodec);
  if (audio_stream == nullptr) {
    qCritical() << "Could not allocate audio stream";
    export_error = tr("could not allocate audio stream");
    return false;
  }

  // Set audio stream's ID to 1
  audio_stream->id = 1;

  // set sample rate to use for project
  audio_rendering_rate = params_.audio_sampling_rate;

  // Allocate encoding context
  acodec_ctx = avcodec_alloc_context3(acodec);
  if (!acodec_ctx) {
    qCritical() << "Could not find allocate audio encoding context";
    export_error = tr("could not allocate audio encoding context");
    return false;
  }

  // Set up encoding context
  acodec_ctx->codec_id = static_cast<AVCodecID>(params_.audio_codec);
  acodec_ctx->codec_type = AVMEDIA_TYPE_AUDIO;
  acodec_ctx->sample_rate = params_.audio_sampling_rate;
  av_channel_layout_from_mask(&acodec_ctx->ch_layout,
                              AV_CH_LAYOUT_STEREO);  // change this to support surround/mono sound in the future (this
                                                     // is what the user sets the output audio to)
#if LIBAVCODEC_VERSION_INT >= AV_VERSION_INT(61, 0, 0)
  const enum AVSampleFormat* sample_fmts = nullptr;
  int num_sample_fmts = 0;
  avcodec_get_supported_config(nullptr, acodec, AV_CODEC_CONFIG_SAMPLE_FORMAT, 0, (const void**)&sample_fmts,
                               &num_sample_fmts);
  acodec_ctx->sample_fmt = (sample_fmts && num_sample_fmts > 0) ? sample_fmts[0] : AV_SAMPLE_FMT_S16;
#else
  acodec_ctx->sample_fmt = acodec->sample_fmts ? acodec->sample_fmts[0] : AV_SAMPLE_FMT_S16;
#endif
  acodec_ctx->bit_rate = params_.audio_bitrate * 1000;

  acodec_ctx->time_base.num = 1;
  acodec_ctx->time_base.den = params_.audio_sampling_rate;
  audio_stream->time_base = acodec_ctx->time_base;

  if (fmt_ctx->oformat->flags & AVFMT_GLOBALHEADER) {
    acodec_ctx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
  }

  // Open encoder
  ret = avcodec_open2(acodec_ctx, acodec, nullptr);
  if (ret < 0) {
    qCritical() << "Could not open output audio encoder." << ret;
    export_error = tr("could not open output audio encoder (%1)").arg(QString::number(ret));
    return false;
  }

  // Copy parameters from the codec context (set up above) to the output stream
  ret = avcodec_parameters_from_context(audio_stream->codecpar, acodec_ctx);
  if (ret < 0) {
    qCritical() << "Could not copy audio encoder parameters to output stream." << ret;
    export_error = tr("could not copy audio encoder parameters to output stream (%1)").arg(QString::number(ret));
    return false;
  }

  // init audio resampler context
  {
    AVChannelLayout in_ch_layout = {};
    av_channel_layout_from_mask(&in_ch_layout, seq_->audio_layout);
    swr_alloc_set_opts2(&swr_ctx, &acodec_ctx->ch_layout, acodec_ctx->sample_fmt, acodec_ctx->sample_rate,
                        &in_ch_layout, AV_SAMPLE_FMT_S16, acodec_ctx->sample_rate, 0, nullptr);
    av_channel_layout_uninit(&in_ch_layout);
  }
  ret = swr_init(swr_ctx);
  if (ret < 0) {
    qCritical() << "Could not initialize audio resampler." << ret;
    export_error = tr("could not initialize audio resampler (%1)").arg(QString::number(ret));
    return false;
  }

  // initialize raw audio frame
  audio_frame = av_frame_alloc();
  audio_frame->sample_rate = acodec_ctx->sample_rate;
  audio_frame->nb_samples = acodec_ctx->frame_size;

  if (audio_frame->nb_samples == 0) {
    // FIXME: Magic number. I don't know what to put here and truthfully I don't even know if it matters.
    audio_frame->nb_samples = 256;
  }

  // TODO change this to support surround/mono sound in the future (this is whatever format they're held in the internal
  // buffer)
  av_channel_layout_from_mask(&audio_frame->ch_layout, AV_CH_LAYOUT_STEREO);

  audio_frame->format = AV_SAMPLE_FMT_S16;
  av_frame_make_writable(audio_frame);
  ret = av_frame_get_buffer(audio_frame, 0);
  if (ret < 0) {
    qCritical() << "Could not allocate audio buffer." << ret;
    export_error = tr("could not allocate audio buffer (%1)").arg(QString::number(ret));
    return false;
  }
  aframe_bytes = av_samples_get_buffer_size(nullptr, audio_frame->ch_layout.nb_channels, audio_frame->nb_samples,
                                            static_cast<AVSampleFormat>(audio_frame->format), 0);

  audio_pkt = av_packet_alloc();

  // init converted audio frame
  swr_frame = av_frame_alloc();
  av_channel_layout_copy(&swr_frame->ch_layout, &acodec_ctx->ch_layout);
  swr_frame->sample_rate = acodec_ctx->sample_rate;
  swr_frame->format = acodec_ctx->sample_fmt;
  swr_frame->nb_samples = acodec_ctx->frame_size;
  av_frame_get_buffer(swr_frame, 0);

  av_frame_make_writable(swr_frame);

  return true;
}

bool ExportThread::SetupContainer() {
  // Set up output context (using the filename as the format specification)

  avformat_alloc_output_context2(&fmt_ctx, nullptr, nullptr, c_filename);
  if (fmt_ctx == nullptr) {
    // Failed to create the output format context. Exit the export and throw an error.

    qCritical() << "Could not create output context";
    export_error = tr("could not create output format context");
    return false;
  }

  ret = avio_open(&fmt_ctx->pb, c_filename, AVIO_FLAG_WRITE);
  if (ret < 0) {
    // Failed to get a valid write handle for the exported file. Exit the export and throw an error.

    qCritical() << "Could not open output file." << ret;
    export_error = tr("could not open output file (%1)").arg(QString::number(ret));
    return false;
  }

  return true;
}

// Render one video frame and encode it. Returns false on unrecoverable error (caller should goto cleanup_renderer).
bool ExportThread::EncodeVideoFrame(RenderThread* renderer, double timecode_secs) {
  do {
    render_complete_ = false;
    renderer->start_render(seq_, 1, nullptr, video_frame->data[0], video_frame->linesize[0] / 4, 0);
    while (!render_complete_ && !interrupt_) {
      waitCond.wait(&mutex);
    }
    if (interrupt_) return false;
  } while (renderer->did_texture_fail());

  if (interrupt_) return false;

  // Alloc/free sws_frame every frame — required to avoid GIF getting stuck on first frame
  // (see https://stackoverflow.com/a/38997739)
  sws_frame = av_frame_alloc();
  sws_frame->format = vcodec_ctx->pix_fmt;
  sws_frame->width = params_.video_width;
  sws_frame->height = params_.video_height;
  av_frame_get_buffer(sws_frame, 0);

  sws_scale(sws_ctx, video_frame->data, video_frame->linesize, 0, video_frame->height, sws_frame->data,
            sws_frame->linesize);
  sws_frame->pts = qRound(timecode_secs / av_q2d(vcodec_ctx->time_base));

  bool ok = Encode(fmt_ctx, vcodec_ctx, sws_frame, video_pkt, video_stream);
  av_frame_free(&sws_frame);
  sws_frame = nullptr;
  return ok;
}

// Drain audio buffer and encode frames up to timecode_secs. Returns false on error (caller should goto
// cleanup_renderer).
bool ExportThread::EncodeAudioFrames(long& file_audio_samples, double timecode_secs) {
  while (waiting_for_audio_ && !interrupt_) {
    waitCond.wait(&mutex);
  }

  audio_write_lock.lock();
  while (!interrupt_ && file_audio_samples <= (timecode_secs * params_.audio_sampling_rate)) {
    int adjusted_read = audio_ibuffer_read.load() % audio_ibuffer_size;
    int copylen = qMin(aframe_bytes, audio_ibuffer_size - adjusted_read);
    memcpy(audio_frame->data[0], audio_ibuffer + adjusted_read, copylen);
    memset(audio_ibuffer + adjusted_read, 0, copylen);
    audio_ibuffer_read.fetch_add(copylen);

    if (copylen < aframe_bytes) {
      int remainder_len = aframe_bytes - copylen;
      memcpy(audio_frame->data[0] + copylen, audio_ibuffer, remainder_len);
      memset(audio_ibuffer, 0, remainder_len);
      audio_ibuffer_read.fetch_add(remainder_len);
    }

    swr_convert_frame(swr_ctx, swr_frame, audio_frame);
    swr_frame->pts = file_audio_samples;

    if (!Encode(fmt_ctx, acodec_ctx, swr_frame, audio_pkt, audio_stream)) {
      audio_write_lock.unlock();
      return false;
    }
    file_audio_samples += swr_frame->nb_samples;
  }
  audio_write_lock.unlock();
  return true;
}

// Encode all frames in the sequence range. Returns false if encoding failed, sets interrupt_ on interrupt.
// renderer is always cleaned up (cancelled + deleted) on both success and failure paths.
bool ExportThread::EncodeAllFrames(RenderThread* renderer, long& file_audio_samples) {
  qint64 total_time = 0;
  long frame_count = 1;

  while (seq_->playhead <= params_.end_frame && !interrupt_) {
    qint64 frame_start_time = QDateTime::currentMSecsSinceEpoch();

    if (params_.audio_enabled) {
      waiting_for_audio_ = true;
      SetAudioWakeObject(this);
      amber::rendering::compose_audio(seq_, false, 1, true);
    }

    double timecode_secs = double(seq_->playhead - params_.start_frame) / seq_->frame_rate;

    if (params_.video_enabled) {
      if (!EncodeVideoFrame(renderer, timecode_secs)) return false;
    }

    if (params_.audio_enabled) {
      if (!EncodeAudioFrames(file_audio_samples, timecode_secs)) return false;
    }

    qint64 frame_time = QDateTime::currentMSecsSinceEpoch() - frame_start_time;
    total_time += frame_time;
    long remaining_frames = params_.end_frame - seq_->playhead;
    qint64 avg_time = total_time / frame_count;
    qint64 eta = remaining_frames * avg_time;

    emit ProgressChanged(
        qRound((double(seq_->playhead - params_.start_frame) / double(params_.end_frame - params_.start_frame)) *
               100.0),
        eta);

    seq_->playhead++;
    frame_count++;
  }
  return true;
}

// Flush swresample residual audio after the encode loop. Returns false on encoder error.
bool ExportThread::FlushSwrAudio(long& file_audio_samples) {
  if (!params_.audio_enabled) return true;
  int flush_iter = 0;
  do {
    ret = swr_convert_frame(swr_ctx, swr_frame, nullptr);
    if (ret < 0 || swr_frame->nb_samples == 0) break;
    swr_frame->pts = file_audio_samples;
    if (!Encode(fmt_ctx, acodec_ctx, swr_frame, audio_pkt, audio_stream)) return false;
    file_audio_samples += swr_frame->nb_samples;
  } while (swr_frame->nb_samples > 0 && ++flush_iter < 1000);
  return true;
}

// Flush encoder drain (send nullptr frame) to emit any buffered packets.
void ExportThread::FlushEncoders() {
  if (params_.video_enabled) Encode(fmt_ctx, vcodec_ctx, nullptr, video_pkt, video_stream);
  if (params_.audio_enabled) Encode(fmt_ctx, acodec_ctx, nullptr, audio_pkt, audio_stream);
}

void ExportThread::Export() {
  QByteArray ba = params_.filename.toUtf8();
  c_filename = qstrdup(ba.constData());

  if (!SetupContainer()) return;
  if (!SetupVideo()) return;
  if (!SetupAudio()) return;

  ret = avformat_write_header(fmt_ctx, nullptr);
  if (ret < 0) {
    qCritical() << "Could not write output file header." << ret;
    export_error = tr("could not write output file header (%1)").arg(QString::number(ret));
    return;
  }

  long file_audio_samples = 0;

  RenderThread* renderer = new RenderThread();
  if (gl_fallback_surface_) renderer->setGlFallbackSurface(gl_fallback_surface_);
  renderer->start(QThread::HighestPriority);
  connect(renderer, &RenderThread::ready, this, &ExportThread::wake);

  bool frames_ok = EncodeAllFrames(renderer, file_audio_samples);

  renderer->cancel();
  delete renderer;

  // Clean up clip state.  Rendering state (audio_rendering flag + autorecovery
  // timer) is restored on the main thread in ExportDialog::export_thread_finished
  // — starting a QTimer from this thread triggers "Timers cannot be started
  // from another thread".
  close_active_clips(seq_);

  if (interrupt_ || !frames_ok) return;

  if (!FlushSwrAudio(file_audio_samples)) return;
  if (interrupt_) return;

  FlushEncoders();

  // Write container trailer
  ret = av_write_trailer(fmt_ctx);
  if (ret < 0) {
    qCritical() << "Could not write output file trailer." << ret;
    export_error = tr("could not write output file trailer (%1)").arg(QString::number(ret));
    return;
  }

  emit ProgressChanged(100, 0);
}

void ExportThread::Cleanup() {
  if (fmt_ctx != nullptr) {
    avio_closep(&fmt_ctx->pb);
    avformat_free_context(fmt_ctx);
  }

  if (acodec_ctx != nullptr) {
    avcodec_free_context(&acodec_ctx);
  }

  if (audio_frame != nullptr) {
    av_frame_free(&audio_frame);
  }

  if (audio_pkt != nullptr) {
    av_packet_free(&audio_pkt);
  }

  if (vcodec_ctx != nullptr) {
    avcodec_free_context(&vcodec_ctx);
  }

  if (video_frame != nullptr) {
    av_frame_free(&video_frame);
  }

  if (video_pkt != nullptr) {
    av_packet_free(&video_pkt);
  }

  if (sws_ctx != nullptr) {
    sws_freeContext(sws_ctx);
  }

  if (swr_ctx != nullptr) {
    swr_free(&swr_ctx);
  }

  if (swr_frame != nullptr) {
    av_frame_free(&swr_frame);
  }

  if (sws_frame != nullptr) {
    av_frame_free(&sws_frame);
  }

  delete[] c_filename;
}

void ExportThread::run() {
  // Pause/seek/audio_rendering_rate are set on the main thread in
  // ExportDialog before start() — calling UI methods (pausePlayback,
  // seekPlayhead) from this thread would trigger widget repaints on the
  // wrong thread, crashing the OpenGL backend.

  // Lock mutex (used for thread synchronizations)
  mutex.lock();

  // Run export function (which will return if there's a failure)
  Export();

  mutex.unlock();

  // Clean up anything that was allocated in Export() (whether it succeeded or not)
  Cleanup();

#ifdef __GLIBC__
  // Return freed heap pages to the OS. Export churns through many short-lived
  // allocations (QRhi resources, YUV plane copies) and glibc keeps those pages
  // in its free lists, inflating RSS after export.
  malloc_trim(0);
#endif
}

const QString& ExportThread::GetError() { return export_error; }

bool ExportThread::WasInterrupted() { return interrupt_; }

void ExportThread::Interrupt() {
  mutex.lock();
  interrupt_ = true;
  waitCond.wakeAll();
  mutex.unlock();
}

void ExportThread::play_wake() {
  mutex.lock();
  waiting_for_audio_ = false;
  waitCond.wakeAll();
  mutex.unlock();
}

void ExportThread::wake() {
  mutex.lock();
  render_complete_ = true;
  waitCond.wakeAll();
  mutex.unlock();
}
