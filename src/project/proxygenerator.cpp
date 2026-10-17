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

#include "proxygenerator.h"

#include "core/path.h"
#include "project/previewgenerator.h"
#include "ui/mainwindow.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStatusBar>
#include <QtMath>

#include <QDebug>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
}

enum AVCodecID temp_enc_codec = AV_CODEC_ID_PRORES;

ProxyGenerator::ProxyGenerator() = default;

// Set up decoder, encoder, and sws_ctx for one video stream in the proxy pipeline.
// Returns false if setup failed (caller should continue to next stream with passthrough).
static bool setup_proxy_video_stream(int i, AVFormatContext* input_fmt_ctx, AVFormatContext* output_fmt_ctx,
                                     AVStream* out_stream, const ProxyInfo& info, const char* url,
                                     QVector<AVCodecContext*>& input_streams, QVector<AVCodecContext*>& output_streams,
                                     QVector<SwsContext*>& sws_contexts) {
  AVStream* in_stream = input_fmt_ctx->streams[i];
  const AVCodec* dec_codec = avcodec_find_decoder(in_stream->codecpar->codec_id);
  const AVCodec* enc_codec = avcodec_find_encoder(temp_enc_codec);
  if (!dec_codec || !enc_codec) return false;

  AVCodecContext* dec_ctx = avcodec_alloc_context3(dec_codec);
  if (!dec_ctx) {
    qCritical() << "Proxy: could not allocate decoder context";
    return false;
  }
  avcodec_parameters_to_context(dec_ctx, in_stream->codecpar);
  if (avcodec_open2(dec_ctx, dec_codec, nullptr) < 0) {
    qCritical() << "Proxy: could not open decoder";
    avcodec_free_context(&dec_ctx);
    return false;
  }
  input_streams[i] = dec_ctx;
  av_dump_format(input_fmt_ctx, i, url, 0);

  AVCodecContext* enc_ctx = avcodec_alloc_context3(enc_codec);
  if (!enc_ctx) {
    qCritical() << "Proxy: could not allocate encoder context";
    avcodec_free_context(&dec_ctx);
    input_streams[i] = nullptr;
    return false;
  }
  enc_ctx->codec_id = temp_enc_codec;
  enc_ctx->codec_type = AVMEDIA_TYPE_VIDEO;
  enc_ctx->width = qFloor(dec_ctx->width * info.size_multiplier);
  enc_ctx->height = qFloor(dec_ctx->height * info.size_multiplier);
  enc_ctx->sample_aspect_ratio = dec_ctx->sample_aspect_ratio;
#if LIBAVCODEC_VERSION_INT >= AV_VERSION_INT(61, 13, 100)
  const enum AVPixelFormat* pix_fmts = nullptr;
  int num_pix_fmts = 0;
  avcodec_get_supported_config(nullptr, enc_codec, AV_CODEC_CONFIG_PIX_FORMAT, 0, (const void**)&pix_fmts,
                               &num_pix_fmts);
  enc_ctx->pix_fmt = (pix_fmts && num_pix_fmts > 0) ? pix_fmts[0] : AV_PIX_FMT_YUV420P;
#else
  enc_ctx->pix_fmt = enc_codec->pix_fmts ? enc_codec->pix_fmts[0] : AV_PIX_FMT_YUV420P;
#endif
  enc_ctx->framerate = dec_ctx->framerate;
  enc_ctx->time_base = in_stream->time_base;
  out_stream->time_base = in_stream->time_base;
  if (output_fmt_ctx->oformat->flags & AVFMT_GLOBALHEADER) {
    enc_ctx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
  }
  AVDictionary* opts = nullptr;
  av_dict_set(&opts, "threads", "auto", 0);
  if (avcodec_open2(enc_ctx, enc_codec, &opts) < 0) {
    qCritical() << "Proxy: could not open encoder";
    av_dict_free(&opts);
    avcodec_free_context(&enc_ctx);
    avcodec_free_context(&dec_ctx);
    input_streams[i] = nullptr;
    return false;
  }
  avcodec_parameters_from_context(out_stream->codecpar, enc_ctx);
  output_streams[i] = enc_ctx;

  sws_contexts[i] = sws_getContext(in_stream->codecpar->width, in_stream->codecpar->height,
                                   static_cast<enum AVPixelFormat>(in_stream->codecpar->format), enc_ctx->width,
                                   enc_ctx->height, enc_ctx->pix_fmt, 0, nullptr, nullptr, nullptr);
  return true;
}

// Write every packet the encoder has ready.
static void write_proxy_packets(int stream_index, AVPacket* packet, AVFormatContext* output_fmt_ctx,
                                QVector<AVCodecContext*>& output_streams, bool& skip) {
  int recret;
  while ((recret = avcodec_receive_packet(output_streams.at(stream_index), packet)) >= 0 && !skip) {
    packet->stream_index = stream_index;
    av_interleaved_write_frame(output_fmt_ctx, packet);
    av_packet_unref(packet);
  }
}

// Process one decoded frame: pixel-convert if needed, send to encoder, write output packets.
static void encode_proxy_frame(int stream_index, AVFrame* dec_frame, AVPacket* packet, AVFormatContext* input_fmt_ctx,
                               AVFormatContext* output_fmt_ctx, QVector<AVCodecContext*>& input_streams,
                               QVector<AVCodecContext*>& output_streams, QVector<SwsContext*>& sws_contexts,
                               bool& skip) {
  dec_frame->pts = av_rescale_q(dec_frame->pts, input_fmt_ctx->streams[stream_index]->time_base,
                                output_fmt_ctx->streams[stream_index]->time_base);

  bool convert_pix_fmt = (output_streams.at(stream_index)->pix_fmt != input_streams.at(stream_index)->pix_fmt ||
                          output_streams.at(stream_index)->width != input_streams.at(stream_index)->width ||
                          output_streams.at(stream_index)->height != input_streams.at(stream_index)->height);

  AVFrame* enc_frame = dec_frame;
  if (convert_pix_fmt) {
    enc_frame = av_frame_alloc();
    enc_frame->width = output_streams.at(stream_index)->width;
    enc_frame->height = output_streams.at(stream_index)->height;
    enc_frame->format = output_streams.at(stream_index)->pix_fmt;
    av_frame_get_buffer(enc_frame, 0);
    sws_scale(sws_contexts.at(stream_index), dec_frame->data, dec_frame->linesize, 0, dec_frame->height,
              enc_frame->data, enc_frame->linesize);
    enc_frame->pts = dec_frame->pts;
  }

  avcodec_send_frame(output_streams.at(stream_index), enc_frame);
  if (convert_pix_fmt) av_frame_free(&enc_frame);

  write_proxy_packets(stream_index, packet, output_fmt_ctx, output_streams, skip);
}

// Open the input/output format contexts for proxy transcoding.
// Returns false on any error; on failure all already-opened contexts are freed.
static bool proxy_open_contexts(const QString& input_url, const QString& output_path, int start_number,
                                AVFormatContext*& input_fmt_ctx, AVFormatContext*& output_fmt_ctx) {
  AVDictionary* format_opts = nullptr;
  if (start_number > 0) {
    av_dict_set(&format_opts, "start_number", QString::number(start_number).toUtf8(), 0);
  }

  input_fmt_ctx = nullptr;
  if (avformat_open_input(&input_fmt_ctx, input_url.toUtf8(), nullptr, &format_opts) < 0) {
    qCritical() << "Proxy: could not open input" << input_url;
    return false;
  }

  output_fmt_ctx = nullptr;
  avformat_alloc_output_context2(&output_fmt_ctx, nullptr, nullptr, output_path.toUtf8());
  if (!output_fmt_ctx) {
    qCritical() << "Proxy: could not allocate output context";
    avformat_close_input(&input_fmt_ctx);
    return false;
  }

  if (avio_open(&output_fmt_ctx->pb, output_path.toUtf8(), AVIO_FLAG_WRITE) < 0) {
    qCritical() << "Proxy: could not open output" << output_path;
    avformat_close_input(&input_fmt_ctx);
    avformat_free_context(output_fmt_ctx);
    return false;
  }

  if (avformat_find_stream_info(input_fmt_ctx, nullptr) < 0) {
    qCritical() << "Proxy: could not find stream info" << input_url;
    avio_closep(&output_fmt_ctx->pb);
    QFile::remove(output_path);  // just created, empty
    avformat_close_input(&input_fmt_ctx);
    avformat_free_context(output_fmt_ctx);
    return false;
  }
  return true;
}

// What proxy_process_packet() stopped on
enum ProxyReadStatus { kProxyFrame, kProxyEof, kProxyError, kProxySkipped };

// Process one packet in the decode loop — passthrough or decode+encode for video.
// Returns kProxyFrame with a decoded frame in dec_frame, or why reading stopped.
static ProxyReadStatus proxy_process_packet(AVPacket* packet, AVFrame* dec_frame, AVFormatContext* input_fmt_ctx,
                                            AVFormatContext* output_fmt_ctx, QVector<AVCodecContext*>& input_streams,
                                            int& stream_index, bool& skip, double& current_progress) {
  while (!skip) {
    int read_ret = av_read_frame(input_fmt_ctx, packet);
    if (read_ret < 0) {
      if (read_ret != AVERROR_EOF) {
        qWarning() << "Proxy generation ended prematurely (read error" << read_ret << ")";
        return kProxyError;
      }
      return kProxyEof;
    }
    stream_index = packet->stream_index;
    // A stream that showed up after the header was written (AVFMTCTX_NOHEADER formats) has no output stream
    if (stream_index < 0 || stream_index >= input_streams.size()) {
      av_packet_unref(packet);
      continue;
    }
    AVCodecContext* dec_ctx = input_streams.at(stream_index);
    if (dec_ctx == nullptr) {
      // passthrough streams have no decoder: write the packet as is and keep reading
      av_packet_rescale_ts(packet, input_fmt_ctx->streams[stream_index]->time_base,
                           output_fmt_ctx->streams[stream_index]->time_base);
      av_interleaved_write_frame(output_fmt_ctx, packet);
      av_packet_unref(packet);
      continue;
    }
    avcodec_send_packet(dec_ctx, packet);
    current_progress = qCeil((double(packet->pts) / double(input_fmt_ctx->streams[stream_index]->duration)) * 100);
    av_packet_unref(packet);
    // EAGAIN: the decoder needs more input; any other error: drop this packet's output rather than encode a
    // frame that holds nothing
    if (avcodec_receive_frame(dec_ctx, dec_frame) == 0) return kProxyFrame;
  }
  return kProxySkipped;
}

// Main decode/encode loop for proxy transcoding. Returns true if the whole input was read and encoded.
static bool proxy_decode_loop(AVFormatContext* input_fmt_ctx, AVFormatContext* output_fmt_ctx,
                              QVector<AVCodecContext*>& input_streams, QVector<AVCodecContext*>& output_streams,
                              QVector<SwsContext*>& sws_contexts, bool& skip, double& current_progress) {
  AVPacket* packet = av_packet_alloc();
  AVFrame* dec_frame = av_frame_alloc();

  ProxyReadStatus status;
  int stream_index = 0;
  while ((status = proxy_process_packet(packet, dec_frame, input_fmt_ctx, output_fmt_ctx, input_streams, stream_index,
                                        skip, current_progress)) == kProxyFrame) {
    encode_proxy_frame(stream_index, dec_frame, packet, input_fmt_ctx, output_fmt_ctx, input_streams, output_streams,
                       sws_contexts, skip);
  }

  if (status == kProxyEof) {
    // Drain what the decoders and encoders still buffer (frame threading, B-frames): without this the proxy loses
    // its last frames
    for (int i = 0; i < input_streams.size(); i++) {
      if (input_streams.at(i) == nullptr) continue;
      avcodec_send_packet(input_streams.at(i), nullptr);
      while (!skip && avcodec_receive_frame(input_streams.at(i), dec_frame) == 0) {
        encode_proxy_frame(i, dec_frame, packet, input_fmt_ctx, output_fmt_ctx, input_streams, output_streams,
                           sws_contexts, skip);
      }
      avcodec_send_frame(output_streams.at(i), nullptr);
      write_proxy_packets(i, packet, output_fmt_ctx, output_streams, skip);
    }
  }

  av_packet_free(&packet);
  av_frame_free(&dec_frame);
  return status == kProxyEof && !skip;
}

void ProxyGenerator::transcode(const ProxyInfo& info) {
  Footage* footage = info.media->to_footage();
  current_progress = 0.0;

  AVFormatContext* input_fmt_ctx = nullptr;
  AVFormatContext* output_fmt_ctx = nullptr;
  if (!proxy_open_contexts(footage->url, info.path, footage->start_number, input_fmt_ctx, output_fmt_ctx)) {
    footage->proxy = false;  // clear ProxyDialog's "generating" mark, see the failure path below
    return;
  }

  QVector<AVCodecContext*> input_streams(input_fmt_ctx->nb_streams, nullptr);
  QVector<AVCodecContext*> output_streams(input_fmt_ctx->nb_streams, nullptr);
  QVector<SwsContext*> sws_contexts(input_fmt_ctx->nb_streams, nullptr);

  for (int i = 0; i < int(input_fmt_ctx->nb_streams); i++) {
    AVStream* in_stream = input_fmt_ctx->streams[i];
    AVStream* out_stream = avformat_new_stream(output_fmt_ctx, nullptr);
    out_stream->id = in_stream->id;

    bool video_ok = (in_stream->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) &&
                    setup_proxy_video_stream(i, input_fmt_ctx, output_fmt_ctx, out_stream, info, footage->url.toUtf8(),
                                             input_streams, output_streams, sws_contexts);
    if (!video_ok) avcodec_parameters_copy(out_stream->codecpar, in_stream->codecpar);
  }

  // A failure here only fails this proxy: the generator thread keeps serving the queue (cancelled is for cancel())
  bool success = false;
  if (avformat_write_header(output_fmt_ctx, nullptr) < 0) {
    qWarning() << "Failed to write video header";
  } else {
    success = proxy_decode_loop(input_fmt_ctx, output_fmt_ctx, input_streams, output_streams, sws_contexts, skip,
                                current_progress);
    // av_write_trailer() also reports a sticky I/O error (disk full) from any earlier write
    if (av_write_trailer(output_fmt_ctx) < 0) success = false;
  }

  for (int i = 0; i < int(input_fmt_ctx->nb_streams); i++) {
    if (input_streams[i] != nullptr) {
      sws_freeContext(sws_contexts[i]);
      avcodec_free_context(&input_streams[i]);
      avcodec_free_context(&output_streams[i]);
    }
  }

  avio_closep(&output_fmt_ctx->pb);
  avformat_free_context(output_fmt_ctx);
  avformat_close_input(&input_fmt_ctx);

  if (!success) {
    // Don't leave a truncated file behind
    QFile::remove(info.path);
    if (!skip) {
      // Skipped: cancelled, or re-queued (the new entry owns the footage's proxy state). Failed: clear the
      // "generating" mark ProxyDialog set (proxy on, empty path) so the footage isn't shown as a proxy in progress
      qWarning() << "Failed to create proxy for" << footage->url;
      footage->proxy = false;
    }
    return;
  }

  footage->proxy = true;
  footage->proxy_path = info.path;

  qInfo() << "Finished creating proxy for" << footage->url;
  QMetaObject::invokeMethod(amber::MainWindow->statusBar(), "showMessage", Qt::QueuedConnection,
                            Q_ARG(QString, tr("Finished generating proxy for \"%1\"").arg(footage->url)));
}

// main proxy generating loop
void ProxyGenerator::run() {
  // mutex used for thread safe signalling
  mutex.lock();

  while (!cancelled) {
    // wait for queue() to be called
    waitCond.wait(&mutex);

    // quit thread if cancel() was called
    if (cancelled) break;

    // loop through queue until the queue is empty
    while (proxy_queue.size() > 0) {
      // grab proxy info
      const ProxyInfo& info = proxy_queue.first();

      // create directory for info
      QFileInfo(info.path).dir().mkpath(".");

      // set skip to false
      skip = false;

      // transcode proxy
      transcode(info);

      // we're finished with this proxy, remove it
      proxy_queue.removeFirst();

      // quit loop if cancel() was called
      if (cancelled) break;
    }
  }

  mutex.unlock();
}

// called to add footage to generate proxies for
void ProxyGenerator::queue(const ProxyInfo& info) {
  mutex.lock();

  // remove any queued proxies with the same footage
  if (!proxy_queue.isEmpty() && proxy_queue.first().media == info.media) {
    // if the thread is currently processing a proxy with the same footage, abort it
    skip = true;
  }

  // scan through the rest of the queue for another proxy with the same footage (start with 1 since we already processed
  // first())
  for (int i = 1; i < proxy_queue.size(); i++) {
    if (proxy_queue.at(i).media == info.media) {
      // found a duplicate, assume the one we're queuing now overrides and delete it
      proxy_queue.removeAt(i);
      i--;
    }
  }

  // add proxy info to queue
  proxy_queue.append(info);

  mutex.unlock();

  // wake proxy thread loop if sleeping
  waitCond.wakeAll();
}

// to be called from another thread to terminate the proxy generator thread and free it
void ProxyGenerator::cancel() {
  // signal to thread to cancel
  cancelled = true;
  skip = true;

  // if signal is sleeping, wake it to cancel correctly
  waitCond.wakeAll();

  // wait for thread to finish
  wait();
}

double ProxyGenerator::get_proxy_progress(Media* m) {
  if (!proxy_queue.isEmpty() && proxy_queue.first().media == m) {
    return current_progress;
  }
  return 0.0;
}

// proxy generator is a global omnipotent entity
ProxyGenerator amber::proxy_generator;
