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
#include <QtMath>
#include <QStatusBar>

#include <QDebug>

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
}

enum AVCodecID temp_enc_codec = AV_CODEC_ID_PRORES;

ProxyGenerator::ProxyGenerator()  = default;

// Write every packet the encoder has ready.
static void write_proxy_packets(int stream_index, AVPacket* packet, AVFormatContext* output_fmt_ctx,
                                QVector<AVCodecContext*>& output_streams, bool& skip) {
  // return value for packet receiving
  int recret;

  // loop through receiving packets
  while ((recret = avcodec_receive_packet(output_streams.at(stream_index), packet)) >= 0 && !skip) {

    // set packet stream index to current stream index
    packet->stream_index = stream_index;

    // write frame to file
    av_interleaved_write_frame(output_fmt_ctx, packet);

    // unref old packet
    av_packet_unref(packet);

  }
}

// Process one decoded frame: pixel-convert if needed, send to encoder, write output packets.
static void encode_proxy_frame(int stream_index, AVFrame* dec_frame, AVPacket* packet, AVFormatContext* input_fmt_ctx,
                               AVFormatContext* output_fmt_ctx, QVector<AVCodecContext*>& input_streams,
                               QVector<AVCodecContext*>& output_streams, QVector<SwsContext*>& sws_contexts,
                               bool& skip) {
  // rescale input frame timestamp to output timestamp
  dec_frame->pts = av_rescale_q(dec_frame->pts,
                                input_fmt_ctx->streams[stream_index]->time_base,
                                output_fmt_ctx->streams[stream_index]->time_base);

  // determine if the pix_fmt, width, and/or height is different, so if we need to convert
  bool convert_pix_fmt = (output_streams.at(stream_index)->pix_fmt != input_streams.at(stream_index)->pix_fmt
      || output_streams.at(stream_index)->width != input_streams.at(stream_index)->width
      || output_streams.at(stream_index)->height != input_streams.at(stream_index)->height);

  // create reference to the frame to be sent to the encoder
  AVFrame* enc_frame = dec_frame;

  if (convert_pix_fmt) {
    // create sws frame for pixel format conversion
    enc_frame = av_frame_alloc();
    enc_frame->width = output_streams.at(stream_index)->width;
    enc_frame->height = output_streams.at(stream_index)->height;
    enc_frame->format = output_streams.at(stream_index)->pix_fmt;
    av_frame_get_buffer(enc_frame, 0);

    // convert pixel format to format expected by the encoder
    sws_scale(sws_contexts.at(stream_index), dec_frame->data, dec_frame->linesize, 0, dec_frame->height, enc_frame->data, enc_frame->linesize);

    // set same pts as dec_frame
    enc_frame->pts = dec_frame->pts;
  }

  // send frame to encoder
  avcodec_send_frame(output_streams.at(stream_index), enc_frame);

  if (convert_pix_fmt) {
    // free sws frame since we made one before
    av_frame_free(&enc_frame);
  }

  write_proxy_packets(stream_index, packet, output_fmt_ctx, output_streams, skip);
}

void ProxyGenerator::transcode(const ProxyInfo& info) {
  Footage* footage = info.media->to_footage();

  // set progress to 0
  current_progress = 0.0;

  // for image sequences that don't start at 0, set the index where it does start
  AVDictionary* format_opts = nullptr;
  if (footage->start_number > 0) {
    av_dict_set(&format_opts, "start_number", QString::number(footage->start_number).toUtf8(), 0);
  }

  // open input file
  AVFormatContext* input_fmt_ctx = nullptr;
  if (avformat_open_input(&input_fmt_ctx, footage->url.toUtf8(), nullptr, &format_opts) < 0) {
    qCritical() << "Proxy: could not open input" << footage->url;
    footage->proxy = false;  // clear ProxyDialog's "generating" mark, see the failure path below
    return;
  }

  // open output file
  AVFormatContext* output_fmt_ctx = nullptr;
  avformat_alloc_output_context2(&output_fmt_ctx, nullptr, nullptr, info.path.toUtf8());
  if (output_fmt_ctx == nullptr) {
    qCritical() << "Proxy: could not allocate output context";
    avformat_close_input(&input_fmt_ctx);
    footage->proxy = false;
    return;
  }

  // open output file writing handle
  if (avio_open(&output_fmt_ctx->pb, info.path.toUtf8(), AVIO_FLAG_WRITE) < 0) {
    qCritical() << "Proxy: could not open output" << info.path;
    avformat_close_input(&input_fmt_ctx);
    avformat_free_context(output_fmt_ctx);
    footage->proxy = false;
    return;
  }

  // get stream info from input file
  if (avformat_find_stream_info(input_fmt_ctx, nullptr) < 0) {
    qCritical() << "Proxy: could not find stream info" << footage->url;
    avio_closep(&output_fmt_ctx->pb);
    QFile::remove(info.path);  // just created, empty
    avformat_close_input(&input_fmt_ctx);
    avformat_free_context(output_fmt_ctx);
    footage->proxy = false;
    return;
  }

  // create array of input decoders
  QVector<AVCodecContext*> input_streams;
  input_streams.resize(input_fmt_ctx->nb_streams);
  input_streams.fill(nullptr);

  // create array of output encoders
  QVector<AVCodecContext*> output_streams;
  output_streams.resize(input_fmt_ctx->nb_streams);
  output_streams.fill(nullptr);

  // create array of swscale contexts for pixel format conversion
  QVector<SwsContext*> sws_contexts;
  sws_contexts.resize(input_fmt_ctx->nb_streams);
  sws_contexts.fill(nullptr);

  // loop through file to find compatible video streams
  for (int i=0;i<int(input_fmt_ctx->nb_streams);i++) {
    AVStream* in_stream = input_fmt_ctx->streams[i];

    // create new stream in output
    AVStream* out_stream = avformat_new_stream(output_fmt_ctx, nullptr);
    out_stream->id = in_stream->id;

    // find decoder for this codec
    const AVCodec* dec_codec = avcodec_find_decoder(in_stream->codecpar->codec_id);

    // find encoder for chosen proxy type
    const AVCodec* enc_codec = avcodec_find_encoder(temp_enc_codec);

    // we only transcode video streams, others we just passthrough
    if (in_stream->codecpar->codec_type == AVMEDIA_TYPE_VIDEO && dec_codec != nullptr) {

      // allocate decoding context for this stream
      AVCodecContext* dec_ctx = avcodec_alloc_context3(dec_codec);
      if (dec_ctx == nullptr) {
        qCritical() << "Proxy: could not allocate decoder context";
        continue;
      }

      // copy parameters from stream to decoding context
      avcodec_parameters_to_context(dec_ctx, in_stream->codecpar);

      // open decoder
      if (avcodec_open2(dec_ctx, dec_codec, nullptr) < 0) {
        qCritical() << "Proxy: could not open decoder";
        avcodec_free_context(&dec_ctx);
        continue;
      }

      // store decoding context in array
      input_streams[i] = dec_ctx;

      // retrieve more information about this stream
      av_dump_format(input_fmt_ctx, i, footage->url.toUtf8(), 0);

      // allocate encoding context for this stream
      AVCodecContext* enc_ctx = avcodec_alloc_context3(enc_codec);
      if (enc_ctx == nullptr) {
        qCritical() << "Proxy: could not allocate encoder context";
        avcodec_free_context(&dec_ctx);
        continue;
      }

      // copy properties from decoding context to encoding context
      enc_ctx->codec_id = temp_enc_codec;
      enc_ctx->codec_type = AVMEDIA_TYPE_VIDEO;
      enc_ctx->width = qFloor(dec_ctx->width*info.size_multiplier);
      enc_ctx->height = qFloor(dec_ctx->height*info.size_multiplier);
      enc_ctx->sample_aspect_ratio = dec_ctx->sample_aspect_ratio;
#if LIBAVCODEC_VERSION_INT >= AV_VERSION_INT(61, 13, 100)
      const enum AVPixelFormat *pix_fmts = nullptr;
      int num_pix_fmts = 0;
      avcodec_get_supported_config(nullptr, enc_codec, AV_CODEC_CONFIG_PIX_FORMAT, 0,
                                   (const void **)&pix_fmts, &num_pix_fmts);
      enc_ctx->pix_fmt = (pix_fmts && num_pix_fmts > 0) ? pix_fmts[0] : AV_PIX_FMT_YUV420P;
#else
      enc_ctx->pix_fmt = enc_codec->pix_fmts ? enc_codec->pix_fmts[0] : AV_PIX_FMT_YUV420P;
#endif
      enc_ctx->framerate = dec_ctx->framerate;
      enc_ctx->time_base = in_stream->time_base;
      out_stream->time_base = in_stream->time_base;

      // if format uses global headers, add flag to enc_ctx
      if (output_fmt_ctx->oformat->flags & AVFMT_GLOBALHEADER) {
        enc_ctx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
      }

      // set encoder options (mostly just multithreading)
      AVDictionary* opts = nullptr;
      av_dict_set(&opts, "threads", "auto", 0);

      // open encoder
      if (avcodec_open2(enc_ctx, enc_codec, &opts) < 0) {
        qCritical() << "Proxy: could not open encoder";
        av_dict_free(&opts);
        avcodec_free_context(&enc_ctx);
        avcodec_free_context(&dec_ctx);
        input_streams[i] = nullptr;
        continue;
      }

      // copy parameters from encoding context to stream
      avcodec_parameters_from_context(out_stream->codecpar, enc_ctx);

      // store encoding context in array
      output_streams[i] = enc_ctx;

      // create swscontext for this stream
      SwsContext* sws_ctx = sws_getContext(
            in_stream->codecpar->width,
            in_stream->codecpar->height,
            static_cast<enum AVPixelFormat>(in_stream->codecpar->format),
            enc_ctx->width,
            enc_ctx->height,
            enc_ctx->pix_fmt,
            0,
            nullptr,
            nullptr,
            nullptr
            );

      sws_contexts[i] = sws_ctx;
    } else {
      avcodec_parameters_copy(out_stream->codecpar, in_stream->codecpar);
    }
  }

  // write video header
  // A failure here only fails this proxy: the generator thread keeps serving the queue (cancelled is for cancel())
  bool success = false;
  int error_code = avformat_write_header(output_fmt_ctx, nullptr);
  if (error_code < 0) {
    qWarning() << "Failed to write video header";
  } else {
    // packet that av_read_frame will dump file packets into
    AVPacket *packet = av_packet_alloc();

    // frame that decoder will decode into
    AVFrame* dec_frame = av_frame_alloc();

    // set once the whole input was read (as opposed to a read error or skip)
    bool reached_eof = false;

    // main transcoding loop
    while (!skip) {

      // read packets until the decoder of a transcoded stream returns a frame
      int stream_index = 0;
      bool got_frame = false;
      while (!skip) {
        // read from input file
        int read_ret = av_read_frame(input_fmt_ctx, packet);

        // handle errors
        if (read_ret < 0) {

          // AVERROR_EOF means we've simply reached the end of the file, otherwise this is an error
          if (read_ret != AVERROR_EOF) {
            qWarning() << "Proxy generation for file" << footage->url << "ended prematurely";
          } else {
            reached_eof = true;
          }

          // either way, we shall abort reading
          break;
        }

        stream_index = packet->stream_index;

        // A stream that showed up after the header was written (AVFMTCTX_NOHEADER formats) has no output stream
        if (stream_index < 0 || stream_index >= input_streams.size()) {
          av_packet_unref(packet);
          continue;
        }

        // determine whether this frame is from a stream we're transcoding
        AVCodecContext* dec_ctx = input_streams.at(stream_index);
        if (dec_ctx == nullptr) {
          // if we didn't allocate a decoder for this earlier, we just pass it through and keep reading
          av_packet_rescale_ts(packet, input_fmt_ctx->streams[stream_index]->time_base, output_fmt_ctx->streams[stream_index]->time_base);
          av_interleaved_write_frame(output_fmt_ctx, packet);
          av_packet_unref(packet);
          continue;
        }

        // we're going to transcode this packet: send it to the decoder
        avcodec_send_packet(dec_ctx, packet);

        // use timestamp and stream duration to create a rough estimation of the progress through this file
        current_progress = qCeil((double(packet->pts)/double(input_fmt_ctx->streams[stream_index]->duration))*100);

        // free packet allocated by av_read_frame
        av_packet_unref(packet);

        // EAGAIN: the decoder needs more input; any other error: drop this packet's output rather than encode a
        // frame that holds nothing
        if (avcodec_receive_frame(dec_ctx, dec_frame) == 0) {
          got_frame = true;
          break;
        }
      }

      // error/eof/skip - cancel while loop
      if (!got_frame) {
        break;
      }

      encode_proxy_frame(stream_index, dec_frame, packet, input_fmt_ctx, output_fmt_ctx, input_streams, output_streams,
                         sws_contexts, skip);
    }

    if (reached_eof) {
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

    // free packet and dec_frame
    av_packet_free(&packet);
    av_frame_free(&dec_frame);

    success = reached_eof && !skip;

    // write video trailer. av_write_trailer() also reports a sticky I/O error (disk full) from any earlier write
    if (av_write_trailer(output_fmt_ctx) < 0) success = false;
  }

  // free stream contexts
  for (int i=0;i<int(input_fmt_ctx->nb_streams);i++) {
    if (input_streams[i] != nullptr) {
      // free swscale contexts
      sws_freeContext(sws_contexts[i]);

      // free input decoding context
      avcodec_free_context(&input_streams[i]);

      // free output encoding context
      avcodec_free_context(&output_streams[i]);
    }
  }

  // close output file handle
  avio_closep(&output_fmt_ctx->pb);

  // close output file
  avformat_free_context(output_fmt_ctx);

  // close input file
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

  // set footage to use newly generated proxy
  footage->proxy = true;
  footage->proxy_path = info.path;

  qInfo() << "Finished creating proxy for" << footage->url;
  QMetaObject::invokeMethod(amber::MainWindow->statusBar(),
                            "showMessage",
                            Qt::QueuedConnection,
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

      // grab proxy info (a copy: queue() may grow proxy_queue while the mutex is released below)
      const ProxyInfo info = proxy_queue.first();

      // set skip to false
      skip = false;

      // release the mutex during the transcode so queue() doesn't block the UI until it ends. The entry stays at
      // first() meanwhile: queue() never removes it, it sets skip to abort it when the same footage is re-queued.
      // CBA: skip is a plain bool read unlocked by the decode loop, as cancel() already relies on; std::atomic
      // would need the helpers' bool& parameters changed
      mutex.unlock();

      // create directory for info
      QFileInfo(info.path).dir().mkpath(".");

      // transcode proxy
      transcode(info);

      mutex.lock();

      // we're finished with this proxy, remove it
      proxy_queue.removeFirst();

      // quit loop if cancel() was called
      if (cancelled) break;

    }
  }

  mutex.unlock();
}

// called to add footage to generate proxies for
void ProxyGenerator::queue(const ProxyInfo &info) {
  mutex.lock();

  // remove any queued proxies with the same footage
  if (!proxy_queue.isEmpty()
      && proxy_queue.first().media == info.media) {
    // if the thread is currently processing a proxy with the same footage, abort it
    skip = true;
  }

  // scan through the rest of the queue for another proxy with the same footage (start with 1 since we already processed first())
  for (int i=1;i<proxy_queue.size();i++) {
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
  // signal to thread to cancel. Under the mutex: run() holds it from its `cancelled` check until waitCond.wait()
  // releases it, so the wake below can't land in between and be lost (the mutex is free during a transcode)
  mutex.lock();
  cancelled = true;
  skip = true;
  mutex.unlock();

  // if signal is sleeping, wake it to cancel correctly
  waitCond.wakeAll();

  // wait for thread to finish
  wait();
}

double ProxyGenerator::get_proxy_progress(Media* m) {
  // only held briefly by the generator thread now, so the UI can take it
  QMutexLocker locker(&mutex);
  if (!proxy_queue.isEmpty() && proxy_queue.first().media == m) {
    return current_progress;
  }
  return 0.0;
}

// proxy generator is a global omnipotent entity
ProxyGenerator amber::proxy_generator;
