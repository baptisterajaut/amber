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
#include <libavfilter/avfilter.h>
#include <libavfilter/buffersink.h>
#include <libavfilter/buffersrc.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavutil/error.h>
#include <libavutil/opt.h>
#include <libavutil/pixdesc.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
}

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QtMath>

#include "core/alpha.h"
#include "core/appcontext.h"
#include "engine/sequence.h"
#include "global/config.h"
#include "global/debug.h"
#include "global/projectio.h"
#include "rendering/audio.h"
#include "rendering/encoderoptions.h"
#include "rendering/renderfunctions.h"
#include "rendering/renderthread.h"

#ifdef __GLIBC__
#include <malloc.h>
#endif

namespace {

void add_options(AVDictionary** dict, const QVector<QPair<QString, QString>>& options) {
  for (const auto& o : options) av_dict_set(dict, o.first.toUtf8().constData(), o.second.toUtf8().constData(), 0);
}

// avcodec_open2() and avformat_write_header() leave the entries nobody used in the dictionary
void warn_unused_options(AVDictionary* dict, const char* what) {
  const AVDictionaryEntry* e = nullptr;
  while ((e = av_dict_get(dict, "", e, AV_DICT_IGNORE_SUFFIX)) != nullptr) {
    qWarning() << what << "ignored option" << e->key << "=" << e->value;
  }
}

// FFmpeg's text for an error code, for the user: "No such file or directory" for AVERROR(ENOENT) (in the system's
// language for an errno), "Invalid argument"... The code itself goes to the log.
QString av_error_text(int code) {
  char text[AV_ERROR_MAX_STRING_SIZE] = {};
  av_strerror(code, text, sizeof(text));  // an unknown code still gets "Error number N occurred"
  return QString::fromUtf8(text);
}

}  // namespace

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
    // Pass-1 statistics, read as FFmpeg's CLI does (ffmpeg_enc.c): after every packet and on the drain's EOF.
    // libavcodec empties stats_out before each call (encode.c), so nothing is read twice.
    if (pass_ == 1 && (ret >= 0 || ret == AVERROR_EOF) && codec_ctx->stats_out != nullptr) {
      pass1_stats_.append(codec_ctx->stats_out);
    }
    if (ret == AVERROR(EAGAIN)) {
      return true;
    } else if (ret < 0) {
      if (ret != AVERROR_EOF) {
        qCritical() << "Failed to receive packet from encoder." << ret;
        export_error = tr("failed to receive packet from encoder (%1)").arg(QString::number(ret));
      }
      return false;
    }

    // The analysis pass writes nothing
    if (pass_ == 1) {
      av_packet_unref(packet);
      continue;
    }

    packet->stream_index = stream->index;
    // The encoder leaves video durations at 0, so the muxer stores the last frame as zero-length and players
    // that honour the mp4 edit list drop it. One tick of the 1/framerate video time base is one frame.
    if (packet->duration == 0 && codec_ctx->codec_type == AVMEDIA_TYPE_VIDEO) packet->duration = 1;

    av_packet_rescale_ts(packet, codec_ctx->time_base, stream->time_base);

    ret = av_interleaved_write_frame(ofmt_ctx, packet);
    av_packet_unref(packet);
    if (ret < 0) {
      qCritical() << "Failed to write packet." << ret;
      export_error = tr("failed to write packet to output file (%1)").arg(QString::number(ret));
      return false;
    }
  }
  return true;
}

bool ExportThread::SetupVideo() {
  // if video is disabled, no setup necessary
  if (!params_.video_enabled) return true;

  // find video encoder
  vcodec = amber::find_encoder(params_.video_encoder, static_cast<AVCodecID>(params_.video_codec));
  if (!vcodec) {
    qCritical() << "Could not find video encoder" << params_.video_encoder;
    export_error = params_.video_encoder.isEmpty()
                       ? tr("could not video encoder for %1").arg(QString::number(params_.video_codec))
                       : tr("could not find video encoder %1").arg(params_.video_encoder);
    return false;
  }

  amber::EncoderInfo vinfo;
  const bool has_vinfo = amber::find_encoder_info(QString::fromUtf8(vcodec->name), &vinfo);

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
  sws_pix_fmt_ = vcodec_ctx->pix_fmt;
  // Always the sequence rate: EncodeAllFrames() stamps one frame per sequence frame, so any other rate gives
  // duplicate pts (export fails) or uneven frame timing. A codec that can't encode it fails in avcodec_open2() below.
  vcodec_ctx->framerate = av_d2q(seq_->frame_rate, INT_MAX);
  if (params_.video_compression_type == COMPRESSION_TYPE_CBR ||
      params_.video_compression_type == COMPRESSION_TYPE_TARGETBR) {
    vcodec_ctx->bit_rate = qRound(params_.video_bitrate * 1000000);
  }
  vcodec_ctx->time_base = av_inv_q(vcodec_ctx->framerate);

  // Describe the colours we actually write. sws_getContext() below defaults to the BT.601
  // matrix and limited range for RGBA->YUV, and without these tags the file says nothing:
  // players then assume BT.709 for HD, decode with the wrong matrix and show reds/magentas
  // too bright and greens/cyans too dark compared to the editor (#74). Metadata only - not
  // a single encoded pixel changes.
  vcodec_ctx->colorspace = AVCOL_SPC_SMPTE170M;
  vcodec_ctx->color_range = AVCOL_RANGE_MPEG;
  vcodec_ctx->color_primaries = AVCOL_PRI_BT709;
  vcodec_ctx->color_trc = AVCOL_TRC_BT709;
  video_stream->time_base = vcodec_ctx->time_base;

  if (fmt_ctx->oformat->flags & AVFMT_GLOBALHEADER) {
    vcodec_ctx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
  }

  // Two-pass: the analysis pass writes statistics, the real one reads them back
  if (pass_ == 1) {
    vcodec_ctx->flags |= AV_CODEC_FLAG_PASS1;
  } else if (pass_ == 2) {
    vcodec_ctx->flags |= AV_CODEC_FLAG_PASS2;
    // Set and freed by the user (avcodec.h), read until the context is freed: pass1_stats_ stays untouched until then
    if (!pass1_stats_.isEmpty()) vcodec_ctx->stats_in = pass1_stats_.data();
  }

  // A hardware encoder takes frames already on its device: pix_fmt becomes the device's (AV_PIX_FMT_VAAPI) and
  // vcodec_params_.pix_fmt the software format they are uploaded from
  if (has_vinfo && vinfo.hw_device_type != AV_HWDEVICE_TYPE_NONE &&
      !SetupHardwareFrames(vinfo.hw_device_type, vinfo.hw_sw_formats)) {
    return false;
  }

  // Set export to be multithreaded
  AVDictionary* opts = nullptr;
  if (vcodec_params_.threads == 0) {
    av_dict_set(&opts, "threads", "auto", 0);
  } else {
    av_dict_set(&opts, "threads", QString::number(vcodec_params_.threads).toUtf8(), 0);
  }

  // Quality-based: the encoder's quality option gets the value. An H.264/H.265 encoder without a descriptor gets crf,
  // as before descriptors. A descriptor's encoder gets no bit rate: libvpx-vp9 and libaom only encode at constant
  // quality without one (constrained quality with one). Their codec defaults already say b=0; this makes sure.
  if (params_.video_compression_type == COMPRESSION_TYPE_CFR) {
    QString key;
    if (has_vinfo) {
      key = vinfo.quality_key;
      if (!key.isEmpty()) {
        vcodec_ctx->bit_rate = 0;
        add_options(&opts, vinfo.quality_mode_options);
      }
    } else if (vcodec->id == AV_CODEC_ID_H264 || vcodec->id == AV_CODEC_ID_HEVC) {
      key = QStringLiteral("crf");
    }
    if (!key.isEmpty()) {
      av_dict_set(&opts, key.toUtf8().constData(),
                  QString::number(static_cast<int>(params_.video_bitrate)).toUtf8().constData(), 0);
    }
  }
  if (pass_ != 0 && !stats_file_.isEmpty()) {
    av_dict_set(&opts, stats_key_.toUtf8().constData(), stats_file_.toUtf8().constData(), 0);
  }
  add_options(&opts, params_.video_options);

  // Open video encoder
  ret = avcodec_open2(vcodec_ctx, vcodec, &opts);
  if (ret >= 0) warn_unused_options(opts, "video encoder");  // a failed open evaluated nothing
  av_dict_free(&opts);
  if (ret < 0) {
    qCritical() << "Could not open output video encoder." << ret;
    export_error = tr("could not open output video encoder: %1").arg(av_error_text(ret));
    // The usual cause is a size outside the hardware's range (radeonsi: 128x128 and up), which FFmpeg only logs
    if (hw_frames_ctx_ != nullptr) {
      export_error =
          tr("hardware encoder failed: %1 (%2)")
              .arg(tr("could not open the encoder at %1x%2").arg(params_.video_width).arg(params_.video_height),
                   av_error_text(ret));
    }
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
  if (vcodec_ctx->pix_fmt == AV_PIX_FMT_PAL8) {
    if (!SetupPaletteGraph()) return false;
  } else {
    sws_ctx = sws_getContext(seq_->width, seq_->height, AV_PIX_FMT_RGBA, params_.video_width, params_.video_height,
                             sws_pix_fmt_, SWS_BILINEAR, nullptr, nullptr, nullptr);
    if (sws_ctx == nullptr) {
      qCritical() << "Could not create pixel format conversion context";
      export_error =
          tr("could not convert to pixel format %1").arg(QString::fromUtf8(av_get_pix_fmt_name(sws_pix_fmt_)));
      return false;
    }
  }

  // The composite is premultiplied alpha (renderfunctions.cpp), but swscale and the encoders take RGBA as straight.
  // A format without alpha drops it, leaving the composite over black, which is what an opaque file should show.
  // CBA: scaling to another size filters straight colour, so semi-transparent edges can darken slightly; scale the
  // premultiplied frame first if that ever shows
  const AVPixFmtDescriptor* desc = av_pix_fmt_desc_get(sws_pix_fmt_);
  unpremultiply_ = desc != nullptr && (desc->flags & AV_PIX_FMT_FLAG_ALPHA);

  return true;
}

// Creates the hardware device and a pool of frames of one of sw_formats (vcodec_params_.pix_fmt if it is one, else
// the first) at the export size, and points the encoder at it. Cleanup() frees both, on failure too.
bool ExportThread::SetupHardwareFrames(AVHWDeviceType type, const QVector<int>& sw_formats) {
  auto fail = [this](const QString& what, int code) {
    qCritical() << "Hardware encoder:" << what << code;
    export_error = tr("hardware encoder failed: %1 (%2)").arg(what, QString::number(code));
    return false;
  };
  if (sw_formats.isEmpty()) return fail(tr("no pixel format"), AVERROR(EINVAL));
  int sw_format = vcodec_params_.pix_fmt;
  if (!sw_formats.contains(sw_format)) {
    qWarning() << "Hardware encoder: pixel format" << sw_format << "not available, using" << sw_formats.first();
    sw_format = sw_formats.first();
  }

  // The default device, as the decoder's hardware acceleration (Cacher::openWorkerTryHwAccel)
  ret = av_hwdevice_ctx_create(&hw_device_ctx_, type, nullptr, nullptr, 0);
  if (ret < 0) {
    return fail(tr("could not open the %1 device").arg(QString::fromUtf8(av_hwdevice_get_type_name(type)).toUpper()),
                ret);
  }
  hw_frames_ctx_ = av_hwframe_ctx_alloc(hw_device_ctx_);
  if (hw_frames_ctx_ == nullptr) return fail(tr("could not allocate the frame pool"), AVERROR(ENOMEM));
  auto* frames = reinterpret_cast<AVHWFramesContext*>(hw_frames_ctx_->data);
  frames->format = amber::hardware_frame_format(type);
  frames->sw_format = static_cast<AVPixelFormat>(sw_format);
  frames->width = params_.video_width;
  frames->height = params_.video_height;
  // initial_pool_size 0: a pool that grows as the encoder holds frames, as FFmpeg's hwupload filter makes it
  ret = av_hwframe_ctx_init(hw_frames_ctx_);
  if (ret < 0) {
    return fail(tr("could not create %1 frames")
                    .arg(QString::fromUtf8(av_get_pix_fmt_name(static_cast<AVPixelFormat>(sw_format)))),
                ret);
  }
  vcodec_ctx->hw_frames_ctx = av_buffer_ref(hw_frames_ctx_);
  if (vcodec_ctx->hw_frames_ctx == nullptr) return fail(tr("could not allocate the frame pool"), AVERROR(ENOMEM));
  vcodec_ctx->pix_fmt = frames->format;
  sws_pix_fmt_ = frames->sw_format;
  return true;
}

// swscale can't output PAL8, and the fixed palettes it can (rgb8, the GIF default) band badly. Build an optimal palette
// for every frame with palettegen and map the frame onto it with paletteuse; pixels under half alpha take the
// palette's transparent entry. Cleanup() frees the graph, on failure too.
// CBA: per-frame palettes, a two-pass global palette (less flicker) if anyone asks
bool ExportThread::SetupPaletteGraph() {
  palette_graph = avfilter_graph_alloc();
  bool ok = palette_graph != nullptr;

  char args[256];
  snprintf(args, sizeof(args), "video_size=%dx%d:pix_fmt=%d:time_base=%d/%d:pixel_aspect=1/1", seq_->width,
           seq_->height, AV_PIX_FMT_RGBA, vcodec_ctx->time_base.num, vcodec_ctx->time_base.den);
  ok = ok && avfilter_graph_create_filter(&palette_src, avfilter_get_by_name("buffer"), "in", args, nullptr,
                                          palette_graph) >= 0;
  ok = ok && avfilter_graph_create_filter(&palette_sink, avfilter_get_by_name("buffersink"), "out", nullptr, nullptr,
                                          palette_graph) >= 0;

  QByteArray chain;
  if (params_.video_width != seq_->width || params_.video_height != seq_->height) {
    chain = QString("scale=%1:%2:flags=bilinear,").arg(params_.video_width).arg(params_.video_height).toUtf8();
  }
  chain += "split[a][b];[a]palettegen=stats_mode=single:reserve_transparent=1[p];[b][p]paletteuse=new=1";

  // The chain's open input is fed by the buffer source, its open output feeds the sink
  AVFilterInOut* outputs = avfilter_inout_alloc();
  AVFilterInOut* inputs = avfilter_inout_alloc();
  ok = ok && outputs != nullptr && inputs != nullptr;
  if (ok) {
    outputs->name = av_strdup("in");
    outputs->filter_ctx = palette_src;
    outputs->pad_idx = 0;
    outputs->next = nullptr;
    inputs->name = av_strdup("out");
    inputs->filter_ctx = palette_sink;
    inputs->pad_idx = 0;
    inputs->next = nullptr;
    ok = avfilter_graph_parse_ptr(palette_graph, chain.constData(), &inputs, &outputs, nullptr) >= 0;
  }
  avfilter_inout_free(&inputs);
  avfilter_inout_free(&outputs);
  ok = ok && avfilter_graph_config(palette_graph, nullptr) >= 0;

  if (!ok) {
    qCritical() << "Could not set up the palette filter graph";
    export_error =
        tr("could not convert to pixel format %1").arg(QString::fromUtf8(av_get_pix_fmt_name(AV_PIX_FMT_PAL8)));
    return false;
  }
  return true;
}

bool ExportThread::SetupAudio() {
  // if video is disabled, no setup necessary
  if (!params_.audio_enabled) return true;

  // Find encoder for this codec
  acodec = amber::find_encoder(params_.audio_encoder, static_cast<AVCodecID>(params_.audio_codec));
  if (!acodec) {
    qCritical() << "Could not find audio encoder" << params_.audio_encoder;
    export_error = params_.audio_encoder.isEmpty()
                       ? tr("could not audio encoder for %1").arg(QString::number(params_.audio_codec))
                       : tr("could not find audio encoder %1").arg(params_.audio_encoder);
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
#if LIBAVCODEC_VERSION_INT >= AV_VERSION_INT(61, 13, 100)
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
  AVDictionary* aopts = nullptr;
  add_options(&aopts, params_.audio_options);
  ret = avcodec_open2(acodec_ctx, acodec, &aopts);
  if (ret >= 0) warn_unused_options(aopts, "audio encoder");
  av_dict_free(&aopts);
  if (ret < 0) {
    qCritical() << "Could not open output audio encoder." << ret;
    export_error = tr("could not open output audio encoder: %1").arg(av_error_text(ret));
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
  // The analysis pass only needs the context, to set the encoder up exactly as for the file (global header)
  if (pass_ == 1) return true;

  // image2 (an image sequence) opens each frame's file itself: opening the pattern would leave an empty file under the
  // pattern's own name, "name%05d.png"
  if (fmt_ctx->oformat->flags & AVFMT_NOFILE) return true;

  ret = avio_open(&fmt_ctx->pb, c_filename, AVIO_FLAG_WRITE);
  if (ret < 0) {
    // Failed to get a valid write handle for the exported file. Exit the export and throw an error.

    qCritical() << "Could not open output file." << ret;
    export_error =
        tr("could not open output file \"%1\": %2").arg(QDir::toNativeSeparators(params_.filename), av_error_text(ret));
    return false;
  }
  container_opened_ = true;

  return true;
}

// Render one video frame and encode it. Returns false on unrecoverable error (caller should goto cleanup_renderer).
bool ExportThread::EncodeVideoFrame(RenderThread* renderer, double timecode_secs) {
  // Renders again while a clip's texture wasn't ready (did_texture_fail())
  for (int failed_renders = 0;;) {
    render_complete_ = false;
    renderer->start_render(seq_, 1, nullptr, video_frame->data[0], video_frame->linesize[0] / 4, 0);
    // Bounded: Interrupt() wakes without the mutex, so its wake can land between this check and the wait; the
    // timeout re-checks interrupt_ (a lost wake costs at most 100 ms of cancel latency)
    while (!render_complete_ && !interrupt_) {
      waitCond.wait(&mutex, 100);
    }
    if (interrupt_) return false;
    if (renderer->rhi_failed()) {
      export_error = tr("Could not initialize the GPU renderer.");
      return false;
    }
    // The GPU frame failed (device lost, out of memory): video_frame still holds the previous frame, or uninitialized
    // memory on the first one, so it must not be encoded. One retry in case it was transient, then the export fails.
    if (renderer->frame_failed()) {
      if (++failed_renders < 2) continue;
      qCritical() << "Rendering frame" << seq_->playhead << "failed";
      export_error = tr("rendering frame %1 failed").arg(seq_->playhead);
      return false;
    }
    if (!renderer->did_texture_fail()) break;
  }

  if (interrupt_) return false;

  if (unpremultiply_) {
    amber::unpremultiply_rgba8(video_frame->data[0], video_frame->width, video_frame->height, video_frame->linesize[0]);
  }

  if (palette_graph != nullptr) {
    video_frame->pts = qRound(timecode_secs / av_q2d(vcodec_ctx->time_base));
    ret = av_buffersrc_add_frame_flags(palette_src, video_frame, AV_BUFFERSRC_FLAG_KEEP_REF);
    if (ret < 0) {
      qCritical() << "Failed to send frame to the palette filter graph." << ret;
      export_error = tr("failed to send frame to the palette filter (%1)").arg(QString::number(ret));
      return false;
    }
    if (!EncodePaletteFrames()) return false;
    // The graph keeps a reference to video_frame's buffer and may hold the frame past the drain above: making
    // video_frame writable again gives the next render its own buffer (a no-op once the graph has let go)
    ret = av_frame_make_writable(video_frame);
    if (ret < 0) {
      qCritical() << "Could not reallocate video frame buffer." << ret;
      export_error = tr("could not allocate video frame buffer (%1)").arg(QString::number(ret));
      return false;
    }
    return true;
  }

  // Alloc/free sws_frame every frame — required to avoid GIF getting stuck on first frame
  // (see https://stackoverflow.com/a/38997739)
  sws_frame = av_frame_alloc();
  sws_frame->format = sws_pix_fmt_;
  sws_frame->width = params_.video_width;
  sws_frame->height = params_.video_height;
  av_frame_get_buffer(sws_frame, 0);

  sws_scale(sws_ctx, video_frame->data, video_frame->linesize, 0, video_frame->height, sws_frame->data,
            sws_frame->linesize);
  sws_frame->pts = qRound(timecode_secs / av_q2d(vcodec_ctx->time_base));

  AVFrame* frame = sws_frame;
  if (hw_frames_ctx_ != nullptr) {
    // Up to the device: a pool frame, then the copy. The encoder keeps its own reference.
    hw_frame = av_frame_alloc();
    ret = hw_frame != nullptr ? av_hwframe_get_buffer(hw_frames_ctx_, hw_frame, 0) : AVERROR(ENOMEM);
    if (ret >= 0) ret = av_hwframe_transfer_data(hw_frame, sws_frame, 0);
    if (ret < 0) {
      qCritical() << "Could not upload the frame to the hardware encoder." << ret;
      export_error = tr("hardware encoder failed: %1 (%2)").arg(tr("could not upload a frame"), QString::number(ret));
      return false;  // Cleanup() frees both frames
    }
    hw_frame->pts = sws_frame->pts;
    frame = hw_frame;
  }

  bool ok = Encode(fmt_ctx, vcodec_ctx, frame, video_pkt, video_stream);
  av_frame_free(&hw_frame);
  av_frame_free(&sws_frame);
  sws_frame = nullptr;
  return ok;
}

// Encode every frame the palette graph has ready (it can hold frames back). Returns false on error.
bool ExportThread::EncodePaletteFrames() {
  while (true) {
    AVFrame* frame = av_frame_alloc();
    ret = av_buffersink_get_frame(palette_sink, frame);
    if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
      av_frame_free(&frame);
      return true;
    } else if (ret < 0) {
      av_frame_free(&frame);
      qCritical() << "Failed to receive frame from the palette filter graph." << ret;
      export_error = tr("failed to receive frame from the palette filter (%1)").arg(QString::number(ret));
      return false;
    }
    frame->pts = av_rescale_q(frame->pts, av_buffersink_get_time_base(palette_sink), vcodec_ctx->time_base);
    bool ok = Encode(fmt_ctx, vcodec_ctx, frame, video_pkt, video_stream);
    av_frame_free(&frame);
    if (!ok) return false;
  }
}

// Push EOF through the palette graph and encode the frames it still holds. Returns false on error.
bool ExportThread::FlushPaletteGraph() {
  if (palette_graph == nullptr) return true;
  ret = av_buffersrc_add_frame_flags(palette_src, nullptr, 0);
  if (ret < 0) {
    qCritical() << "Failed to flush the palette filter graph." << ret;
    export_error = tr("failed to send frame to the palette filter (%1)").arg(QString::number(ret));
    return false;
  }
  return EncodePaletteFrames();
}

// Drain audio buffer and encode frames up to timecode_secs. Returns false on error (caller should goto
// cleanup_renderer).
bool ExportThread::EncodeAudioFrames(long& file_audio_samples, double timecode_secs, int audio_cachers) {
  // Reading before every triggered cacher has mixed loses that clip's first samples (or replays them a ring lap
  // later). Release the export mutex meanwhile so Interrupt() doesn't block the GUI thread.
  // CBA: a cacher that never signals costs the full timeout on every frame and delays a cancel by as much; poll
  // interrupt_ in slices if that ever shows up. After a timeout the stalled pass's late signal lands after the next
  // frame's reset and releases that frame early (the original bug, for a frame or two); a cumulative target would
  // fix it.
  mutex.unlock();
  bool mixed = WaitForAudioFills(audio_cachers, 5000);
  mutex.lock();
  if (!mixed) {
    qWarning() << "Export: timed out waiting for" << audio_cachers << "audio clip(s) to mix, audio may have gaps";
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
  const bool audio = params_.audio_enabled && pass_ != 1;  // the analysis pass is video only

  // end_frame is exclusive (the frame after the last clip, or the out point): N sequence frames give N video frames
  while (seq_->playhead < params_.end_frame && !interrupt_) {
    qint64 frame_start_time = QDateTime::currentMSecsSinceEpoch();

    int audio_cachers = 0;
    if (audio) {
      ResetAudioFillCount();
      audio_cachers = amber::rendering::compose_audio(seq_, false, 1, true);
    }

    double timecode_secs = double(seq_->playhead - params_.start_frame) / seq_->frame_rate;

    if (params_.video_enabled) {
      if (!EncodeVideoFrame(renderer, timecode_secs)) return false;
    }

    if (audio) {
      // Audio up to the end of this frame, so the last frame's audio isn't cut and both streams end together
      double frame_end_secs = double(seq_->playhead + 1 - params_.start_frame) / seq_->frame_rate;
      if (!EncodeAudioFrames(file_audio_samples, frame_end_secs, audio_cachers)) return false;
    }

    qint64 frame_time = QDateTime::currentMSecsSinceEpoch() - frame_start_time;
    total_time += frame_time;
    long remaining_frames = params_.end_frame - seq_->playhead - 1;
    qint64 avg_time = total_time / frame_count;
    qint64 eta = remaining_frames * avg_time;

    double done = double(seq_->playhead + 1 - params_.start_frame) / double(params_.end_frame - params_.start_frame);
    // Two-pass: each pass is half of the bar. While analysing, the ETA adds pass 2 at pass 1's pace.
    // CBA: optimistic for libx264, whose first pass uses fast settings; a per-encoder pace ratio if it matters
    if (pass_ == 1) {
      done /= 2;
      eta += (params_.end_frame - params_.start_frame) * avg_time;
    } else if (pass_ == 2) {
      done = 0.5 + done / 2;
    }
    emit ProgressChanged(qRound(done * 100.0), eta);

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

// Flush encoder drain (send nullptr frame) to emit any buffered packets. Returns false on encoder/write error.
bool ExportThread::FlushEncoders() {
  // The palette graph's last frames go to the encoder before it's drained
  if (!FlushPaletteGraph()) return false;
  // Encode() also returns false on the AVERROR_EOF that ends a normal drain; only a set export_error is a failure
  if (params_.video_enabled) Encode(fmt_ctx, vcodec_ctx, nullptr, video_pkt, video_stream);
  if (params_.audio_enabled && pass_ != 1) Encode(fmt_ctx, acodec_ctx, nullptr, audio_pkt, audio_stream);
  return export_error.isEmpty();
}

// Renders and encodes the whole range with a renderer of its own, then closes the clips it opened. Rendering state
// (audio_rendering flag + autorecovery timer) is restored on the main thread by finish_export() — starting a QTimer
// from this thread triggers "Timers cannot be started from another thread". False on failure and on interrupt.
bool ExportThread::RenderPass(long& file_audio_samples) {
  RenderThread* renderer = new RenderThread();
  if (gl_fallback_surface_) renderer->setGlFallbackSurface(gl_fallback_surface_);
  renderer->start(QThread::HighestPriority);
  connect(renderer, &RenderThread::ready, this, &ExportThread::wake);

  bool frames_ok = EncodeAllFrames(renderer, file_audio_samples);

  renderer->cancel(seq_);  // closes the clips first: their textures belong to the renderer's QRhi
  delete renderer;
  return frames_ok && !interrupt_;
}

// params_.two_pass, when it can apply: Average bitrate video through an encoder whose FFmpeg wrapper implements
// two-pass. Sets stats_key_ for an encoder that keeps its stats in a file.
bool ExportThread::TwoPassApplies() {
  if (!params_.two_pass) return false;
  const AVCodec* codec = params_.video_enabled
                             ? amber::find_encoder(params_.video_encoder, static_cast<AVCodecID>(params_.video_codec))
                             : nullptr;
  amber::EncoderInfo info;
  if (params_.video_compression_type != COMPRESSION_TYPE_TARGETBR || codec == nullptr ||
      !amber::find_encoder_info(QString::fromUtf8(codec->name), &info) || !amber::two_pass_works(info)) {
    qInfo() << "Export: two-pass ignored, it needs Average bitrate video through an encoder that supports it";
    return false;
  }
  stats_key_ = info.two_pass_stats_key;
  return true;
}

// Two-pass, pass 1: every frame rendered and encoded as in pass 2, by an encoder opened with AV_CODEC_FLAG_PASS1.
// Its statistics are kept, its packets dropped; no audio, no file. Pass 2 then starts as a one-pass export does.
bool ExportThread::AnalysisPass() {
  if (!stats_key_.isEmpty()) {
    // Not the encoder's default name, which is in the working directory. run() removes the folder.
    stats_dir_ = std::make_unique<QTemporaryDir>();
    if (!stats_dir_->isValid()) {
      export_error = tr("two-pass analysis failed (%1)").arg(tr("could not create a temporary folder"));
      return false;
    }
    stats_file_ = stats_dir_->filePath("pass1.log");
  }

  pass_ = 1;
  emit PassChanged(1, 2);
  long no_audio = 0;
  // A cancel during the drain stops here, before pass 2 creates the output file
  bool ok = SetupContainer() && SetupVideo() && RenderPass(no_audio) && FlushEncoders() && !interrupt_;
  // Frees the pass-1 encoder, which is when libx264 and libx265 finish their stats file
  Cleanup();
  if (ok && stats_key_.isEmpty() && pass1_stats_.isEmpty()) {
    export_error = tr("the encoder wrote no statistics");
    ok = false;
  }
  if (!ok) {
    if (!interrupt_) {
      export_error =
          tr("two-pass analysis failed (%1)").arg(export_error.isEmpty() ? tr("unknown error") : export_error);
    }
    return false;
  }

  // As prepare_export() left them: RenderPass() has closed the clips already
  seq_->playhead = params_.start_frame;
  if (params_.audio_enabled) clear_audio_ibuffer(params_.start_frame);
  pass_ = 2;
  emit PassChanged(2, 2);
  return true;
}

// Returns true only when the file was fully written. False on any failure and on interrupt (an interrupt does not
// set export_error, so export_error alone can't tell).
bool ExportThread::Export() {
  QByteArray ba = params_.filename.toUtf8();
  c_filename = qstrdup(ba.constData());

  // Before the file exists: a failed or interrupted analysis leaves nothing behind
  if (TwoPassApplies() && !AnalysisPass()) return false;

  if (!SetupContainer()) return false;
  if (!SetupVideo()) return false;
  if (!SetupAudio()) return false;

  AVDictionary* mux_opts = nullptr;
  if (params_.faststart && amber::format_supports_faststart(fmt_ctx->oformat)) {
    // A second pass at the end of the export moves the moov atom in front of mdat
    av_dict_set(&mux_opts, "movflags", "+faststart", 0);
  }
  ret = avformat_write_header(fmt_ctx, &mux_opts);
  if (ret >= 0) warn_unused_options(mux_opts, "muxer");
  av_dict_free(&mux_opts);
  if (ret < 0) {
    qCritical() << "Could not write output file header." << ret;
    export_error = tr("could not write output file header (%1)").arg(QString::number(ret));
    return false;
  }

  long file_audio_samples = 0;
  if (!RenderPass(file_audio_samples)) return false;

  if (!FlushSwrAudio(file_audio_samples)) return false;
  if (interrupt_) return false;

  if (!FlushEncoders()) return false;
  // Interrupt() no longer waits for the drain: a cancel during it lands here, and run() removes the file (an image
  // sequence keeps the frames it wrote, as on any failure)
  if (interrupt_) return false;

  // Write container trailer
  ret = av_write_trailer(fmt_ctx);
  if (ret < 0) {
    qCritical() << "Could not write output file trailer." << ret;
    export_error = tr("could not write output file trailer (%1)").arg(QString::number(ret));
    return false;
  }

  emit ProgressChanged(100, 0);
  return true;
}

// Frees what the Setup*() functions allocated and resets the pointers: the analysis pass of a two-pass export runs it
// before pass 2 sets up again
void ExportThread::Cleanup() {
  if (fmt_ctx != nullptr) {
    // The final buffered write happens here: a full disk can fail it after a clean trailer
    int close_ret = avio_closep(&fmt_ctx->pb);
    if (close_ret < 0 && export_error.isEmpty() && !interrupt_) {
      qCritical() << "Could not close output file." << close_ret;
      export_error = tr("could not close output file (%1)").arg(QString::number(close_ret));
    }
    avformat_free_context(fmt_ctx);
    fmt_ctx = nullptr;
    video_stream = nullptr;  // owned by fmt_ctx
    audio_stream = nullptr;
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
    sws_ctx = nullptr;
  }

  if (palette_graph != nullptr) {
    avfilter_graph_free(&palette_graph);
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

  // After vcodec_ctx, which holds its own references to both
  av_frame_free(&hw_frame);
  av_buffer_unref(&hw_frames_ctx_);
  av_buffer_unref(&hw_device_ctx_);
}

void ExportThread::run() {
  // Playhead/audio_rendering_rate are set on the main thread by
  // prepare_export() before start(), pause/seek by the caller — calling UI
  // methods (pausePlayback, seekPlayhead) from this thread would trigger widget
  // repaints on the wrong thread, crashing the OpenGL backend.

  // Lock mutex (used for thread synchronizations)
  mutex.lock();

  // Run export function (returns false on failure and on interrupt)
  bool succeeded = Export();

  mutex.unlock();

  // Clean up anything that was allocated in Export() (whether it succeeded or not)
  Cleanup();
  delete[] c_filename;
  c_filename = nullptr;
  stats_dir_.reset();  // the pass-2 encoder, freed above, was the last to read the stats file
  if (!export_error.isEmpty()) succeeded = false;  // Cleanup() reports a failed close

  // Callers judge success by !WasInterrupted() && GetError().isEmpty(): a failure must never leave both clear
  if (!succeeded && !interrupt_ && export_error.isEmpty()) {
    export_error = tr("unknown error");  // shown as "Export failed - unknown error"
  }

  // Don't leave a truncated file behind. Cleanup() has already closed the AVIO handle, which Windows needs before
  // the file can be deleted.
  if (!succeeded && container_opened_) {
    QFile::remove(params_.filename);
  }

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
  // Never takes `mutex`: run() holds it across all of Export() but the waits, an encoder call or the final drain
  // included, which take seconds with lookahead encoders (libaom, two-pass) and froze the GUI thread calling this.
  // interrupt_ is atomic and every loop of the export checks it; the wake only shortens EncodeVideoFrame's wait.
  interrupt_ = true;
  waitCond.wakeAll();
}

void ExportThread::wake() {
  mutex.lock();
  render_complete_ = true;
  waitCond.wakeAll();
  mutex.unlock();
}

QOffscreenSurface* prepare_export(ExportThread* thread, Sequence* seq, const ExportParams& params) {
  if (params.audio_enabled) audio_rendering_rate = params.audio_sampling_rate;
  amber::project_io->setRenderingState(true);

  // EncodeAllFrames() renders from the playhead up to end_frame
  if (seq != nullptr) seq->playhead = params.start_frame;

  // Align the audio ring buffer origin with the export start frame so EncodeAudioFrames reads real samples
  // from offset 0 instead of silence (fixes silent audio on In/Out range exports — GitHub issue #25).
  if (params.audio_enabled) clear_audio_ibuffer(params.start_frame);

  close_active_clips(seq);
  amber::project_io->saveAutorecoveryFile();

  // Pre-create a GL fallback surface on the GUI thread (avoids "QWindow outside gui thread" on Wayland)
  QOffscreenSurface* gl_surface = nullptr;
  if (amber::CurrentRuntimeConfig.rhi_backend == RhiBackend::OpenGL) {
    gl_surface = QRhiGles2InitParams::newFallbackSurface();
    thread->setGlFallbackSurface(gl_surface);
  }
  return gl_surface;
}

void finish_export(QOffscreenSurface* gl_surface) {
  amber::project_io->setRenderingState(false);
  clear_audio_ibuffer();

  // The export thread has finished, so its renderer no longer uses the surface
  delete gl_surface;
}
