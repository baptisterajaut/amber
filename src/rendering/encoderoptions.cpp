#include "rendering/encoderoptions.h"

extern "C" {
#include <libavformat/avformat.h>
#include <libavutil/frame.h>
#include <libavutil/hwcontext.h>
#include <libavutil/opt.h>
#include <libavutil/pixdesc.h>
#include <libswscale/swscale.h>
}

#include <QCoreApplication>
#include <QDebug>
#include <QElapsedTimer>
#include <QHash>
#include <QMutex>
#include <climits>
#include <cmath>
#include <cstring>

namespace amber {

namespace {

// x264 and x265 share their preset names (x264.h / x265.h x26x_preset_names; placebo left out)
EncoderOption x26x_preset() {
  EncoderOption o;
  o.key = "preset";
  o.label = QCoreApplication::translate(
      "EncoderOptions", "Speed preset:");  // "Preset:" is the export preset row at the top of the dialog
  o.tooltip = QCoreApplication::translate("EncoderOptions", "Slower presets make smaller files at the same quality");
  o.kind = EncoderOption::Choice;
  o.choices = {"ultrafast", "superfast", "veryfast", "faster", "fast", "medium", "slow", "slower", "veryslow"};
  o.choice_labels = {QCoreApplication::translate("EncoderOptions", "Ultrafast"),
                     QCoreApplication::translate("EncoderOptions", "Superfast"),
                     QCoreApplication::translate("EncoderOptions", "Very fast"),
                     QCoreApplication::translate("EncoderOptions", "Faster"),
                     QCoreApplication::translate("EncoderOptions", "Fast"),
                     QCoreApplication::translate("EncoderOptions", "Medium"),
                     QCoreApplication::translate("EncoderOptions", "Slow"),
                     QCoreApplication::translate("EncoderOptions", "Slower"),
                     QCoreApplication::translate("EncoderOptions", "Very slow")};
  o.default_value = "medium";
  return o;
}

EncoderOption int_option(const char* key, const QString& label, const QString& tooltip, int min, int max,
                         const QString& default_value, const QString& unset_label = QString()) {
  EncoderOption o;
  o.key = key;
  o.label = label;
  o.tooltip = tooltip;
  o.kind = EncoderOption::Int;
  o.min = min;
  o.max = max;
  o.default_value = default_value;
  o.unset_label = unset_label;
  return o;
}

QVector<EncoderInfo> build_table() {
  const QString x26x_crf_tooltip = QCoreApplication::translate(
      "EncoderOptions",
      "Quality Factor:\n\n0 = lossless\n17-18 = visually lossless (compressed, but unnoticeable)\n23 = high "
      "quality\n51 = lowest quality possible");
  QVector<EncoderInfo> t;

  // ffmpeg -h encoder=libx264 (FFmpeg 9.0): -preset <string> (default "medium"); -crf <float> (from -1 to FLT_MAX)
  // (default -1). CRF 36 is Amber's long-standing default; -1 is "not set".
  EncoderInfo x264;
  x264.name = "libx264";
  x264.codec_id = AV_CODEC_ID_H264;
  x264.label = "x264";
  x264.quality_key = "crf";
  x264.quality_mode_label = QCoreApplication::translate("EncoderOptions", "Quality-based (Constant Rate Factor)");
  x264.quality_label = QCoreApplication::translate("EncoderOptions", "Quality (CRF):");
  x264.quality_tooltip = x26x_crf_tooltip;
  x264.quality_min = 0;
  x264.quality_max = 51;
  x264.quality_default = 36;
  x264.supports_average_bitrate = true;
  // Two-pass, FFmpeg n9.0 libx264.c:1119-1121: PASS1 sets x264's b_stat_write, PASS2 b_stat_read, through the stats
  // file of the "stats" option (libx264.c:1545; x264's default name is in the working directory). The same code and
  // option back to n3.4 (libx264.c:502-504, 954).
  x264.supports_two_pass = true;
  x264.two_pass_stats_key = "stats";
  x264.options = {x26x_preset()};
  t.append(x264);

  // ffmpeg -h encoder=libx265: -preset <string> (no default: x265's own is "medium"); -crf <float> (from -1 to
  // FLT_MAX) (default -1)
  EncoderInfo x265 = x264;
  x265.name = "libx265";
  x265.codec_id = AV_CODEC_ID_HEVC;
  x265.label = "x265";
  // libx265.c:552-568: PASS1/PASS2 become x265's pass=1/2, the stats file comes from -x265-stats (libx265.c:1050).
  // Both came in one commit (d25d133df3, first released in FFmpeg 8.1; n8.0 and older ignore the flags), so the
  // stats option check of validate_encoder_info() is exact.
  x265.two_pass_stats_key = "x265-stats";
  t.append(x265);

  // AV1, in preference order (available_encoders keeps it): SVT-AV1 is the fast one, libaom the slow reference.
  // ffmpeg -h encoder=libsvtav1: -preset <int> (from -2 to 13) (default -2); -crf <int> (from 0 to 63) (default 0).
  // FFmpeg's wrapper only applies preset >= -1 and crf > 0; SVT-AV1 4.2 marks presets -2/-1 debug only, and
  // SvtAv1EncApp --help gives CRF 35 as SVT-AV1's default.
  EncoderInfo svt;
  svt.name = "libsvtav1";
  svt.codec_id = AV_CODEC_ID_AV1;
  svt.label = QCoreApplication::translate("EncoderOptions", "SVT-AV1 (fast)");
  svt.quality_key = "crf";
  svt.quality_mode_label = QCoreApplication::translate("EncoderOptions", "Quality-based (Constant Rate Factor)");
  svt.quality_label = QCoreApplication::translate("EncoderOptions", "Quality (CRF):");
  svt.quality_min = 1;
  svt.quality_max = 63;
  svt.quality_default = 35;
  svt.supports_average_bitrate = true;
  // libsvtav1.c:366-401: PASS2 decodes stats_in into rc_stats_buffer, PASS1 sets pass=1; 677-710: stats_out once, on
  // the end-of-stream packet. Since FFmpeg 8.1: commit 5ba2525c7a, on libavcodec 62.23.103 without a bump, so the
  // next version; n8.0 (62.11.100) and older ignore the flags and pass 1 is a plain encode without stats (FFmpeg 6.1
  // and SVT-AV1 1.7: 0 bytes). The stats_out code is under SVT_AV1_CHECK_VERSION(2, 0, 0) (n8.1:659, n9.0:677), and
  // configure takes SVT-AV1 >= 0.9.0: built against 0.9-1.x, FFmpeg takes PASS1 and writes nothing, hence the probe.
  svt.supports_two_pass = true;
  svt.two_pass_min_avcodec = AV_VERSION_INT(62, 23, 104);
  svt.two_pass_probe = true;
  svt.options = {
      int_option("preset", QCoreApplication::translate("EncoderOptions", "Speed preset:"),
                 QCoreApplication::translate("EncoderOptions",
                                             "0 = slowest, smallest file\n13 = fastest\nDefault lets SVT-AV1 choose"),
                 0, 13, QString(), QCoreApplication::translate("EncoderOptions", "Default"))};
  t.append(svt);

  // ffmpeg -h encoder=librav1e: -qp <int> (from -1 to 255) (default -1); -speed <int> (from -1 to 10) (default -1).
  // -1 is "not set" for both; rav1e --help gives quantizer 100 and speed 6 as its defaults.
  EncoderInfo rav1e;
  rav1e.name = "librav1e";
  rav1e.codec_id = AV_CODEC_ID_AV1;
  rav1e.label = "rav1e";
  rav1e.quality_key = "qp";
  rav1e.quality_mode_label = QCoreApplication::translate("EncoderOptions", "Quality-based (Constant Quantizer)");
  rav1e.quality_label = QCoreApplication::translate("EncoderOptions", "Quantizer (QP):");
  rav1e.quality_min = 0;
  rav1e.quality_max = 255;
  rav1e.quality_default = 100;
  rav1e.supports_average_bitrate = true;
  // No two-pass: librav1e.c handles PASS1/PASS2 (225-249, 517-535), but never asks rav1e to emit pass data
  // (rav1e_config_set_emit_data, rav1e.h), so stats_out stays empty: ffmpeg -pass 1 writes a 0-byte log with
  // FFmpeg 9.0.2 and rav1e 0.8.1
  rav1e.options = {
      int_option("speed", QCoreApplication::translate("EncoderOptions", "Speed:"),
                 QCoreApplication::translate("EncoderOptions",
                                             "0 = slowest, smallest file\n10 = fastest\nDefault lets rav1e choose (6)"),
                 0, 10, QString(), QCoreApplication::translate("EncoderOptions", "Default"))};
  t.append(rav1e);

  // ffmpeg -h encoder=libaom-av1: -cpu-used <int> (from 0 to 8) (default 1); -crf <int> (from -1 to 63) (default -1).
  // Constant quality needs bit_rate 0: libaomenc.c picks AOM_Q for crf >= 0 without a bitrate, and the encoder's
  // codec defaults already set b=0. With neither, it falls back to crf 32.
  EncoderInfo aom;
  aom.name = "libaom-av1";
  aom.codec_id = AV_CODEC_ID_AV1;
  aom.label = QCoreApplication::translate("EncoderOptions", "libaom (slow)");
  aom.quality_key = "crf";
  aom.quality_mode_label = QCoreApplication::translate("EncoderOptions", "Quality-based (Constant Rate Factor)");
  aom.quality_label = QCoreApplication::translate("EncoderOptions", "Quality (CRF):");
  aom.quality_min = 0;
  aom.quality_max = 63;
  aom.quality_default = 32;
  aom.supports_average_bitrate = true;
  // libaomenc.c:840-845 and 921-945: PASS1/PASS2 select the first/last pass, PASS2 decodes stats_in; 1387-1397:
  // stats_out at the drain. Unconditional since the wrapper came in n4.0 (libaomenc.c:325-328, 394-421, 639-648).
  aom.supports_two_pass = true;
  aom.options = {int_option("cpu-used", QCoreApplication::translate("EncoderOptions", "Speed:"),
                            QCoreApplication::translate("EncoderOptions", "0 = slowest, smallest file\n8 = fastest"), 0,
                            8, "1")};
  t.append(aom);

  // ffmpeg -h encoder=libvpx-vp9: -crf <int> (from -1 to 63) (default -1); -cpu-used <int> (from -8 to 8) (default 1).
  // Constant quality needs bit_rate 0 (the -b:v 0 of FFmpeg's VP9 guide, which recommends CRF 31 for 1080p):
  // libvpxenc.c:1104-1111 picks VPX_Q for crf >= 0 without a bitrate, VPX_CQ (crf as a floor under a bitrate)
  // with one. The encoder's codec defaults set b=0 (libvpxenc.c:2097). Two-pass: libvpxenc.c:1097-1100 and
  // 1189-1213 (PASS2 decodes stats_in), 1964-1974 (stats_out at the drain); unconditional back to n3.4
  // (libvpxenc.c:502-505, 604-631, 1055-1059).
  EncoderInfo vp9;
  vp9.name = "libvpx-vp9";
  vp9.codec_id = AV_CODEC_ID_VP9;
  vp9.label = "libvpx";
  vp9.quality_key = "crf";
  vp9.quality_mode_label = QCoreApplication::translate("EncoderOptions", "Quality-based (Constant Quality)");
  vp9.quality_label = QCoreApplication::translate("EncoderOptions", "Quality (CRF):");
  vp9.quality_min = 0;
  vp9.quality_max = 63;
  vp9.quality_default = 31;
  vp9.supports_average_bitrate = true;
  vp9.supports_two_pass = true;
  vp9.options = {int_option(
      "cpu-used", QCoreApplication::translate("EncoderOptions", "Speed:"),
      QCoreApplication::translate("EncoderOptions", "Higher is faster, lower makes smaller files at the same quality"),
      0, 8, "1")};
  t.append(vp9);

  // compression_level is a generic AVCodecContext option (-1 = encoder default); flacenc.c uses 5 for -1 and
  // rejects levels above 12
  EncoderInfo flac;
  flac.name = "flac";
  flac.codec_id = AV_CODEC_ID_FLAC;
  flac.label = "FLAC";
  flac.options = {
      int_option("compression_level", QCoreApplication::translate("EncoderOptions", "Compression level:"),
                 QCoreApplication::translate("EncoderOptions", "Higher = smaller file, slower export; always lossless"),
                 0, 12, "5")};
  t.append(flac);

  // Hardware encoders last: available_encoders() keeps the table order, so they come after the software ones of their
  // codec. VAAPI, ffmpeg -h encoder=h264_vaapi (FFmpeg 9.0): -rc_mode (default auto); -qp <int> (from 0 to 52)
  // (default 0, "not set": CQP then takes the codec's default_quality, 20 for H.264, vaapi_encode_h264.c:967, 25 for
  // HEVC, vaapi_encode_h265.c:1036). Quality-based passes rc_mode=CQP so a driver without it fails the export instead
  // of quietly encoding another way. Average bitrate passes bit_rate alone: vaapi_encode.c:1341-1347 picks AVBR, else
  // VBR, else CBR (radeonsi on FFmpeg 9.0.2: VBR, average = bit_rate). No two-pass, no speed preset that maps.
  EncoderInfo h264_vaapi;
  h264_vaapi.name = "h264_vaapi";
  h264_vaapi.codec_id = AV_CODEC_ID_H264;
  h264_vaapi.label = QCoreApplication::translate("EncoderOptions", "Hardware (VAAPI)");
  h264_vaapi.quality_key = "qp";
  h264_vaapi.quality_mode_label = QCoreApplication::translate("EncoderOptions", "Quality-based (Constant QP)");
  h264_vaapi.quality_label = QCoreApplication::translate("EncoderOptions", "Quality (QP):");
  h264_vaapi.quality_min = 1;
  h264_vaapi.quality_max = 51;
  h264_vaapi.quality_default = 20;
  h264_vaapi.supports_average_bitrate = true;
  h264_vaapi.quality_mode_options = {{"rc_mode", "CQP"}};
  h264_vaapi.hw_device_type = AV_HWDEVICE_TYPE_VAAPI;
  // P010 needs a High 10 encode profile (radeonsi has none: the probe drops it)
  h264_vaapi.hw_sw_formats = {AV_PIX_FMT_NV12, AV_PIX_FMT_P010};
  t.append(h264_vaapi);

  EncoderInfo hevc_vaapi = h264_vaapi;
  hevc_vaapi.name = "hevc_vaapi";
  hevc_vaapi.codec_id = AV_CODEC_ID_HEVC;
  hevc_vaapi.quality_default = 25;
  t.append(hevc_vaapi);

  // ffmpeg -h encoder=av1_vaapi: no qp option; CQP takes global_quality as the base_q_idx (0-255,
  // vaapi_encode.c:1418-1428, vaapi_encode_av1.c:140), else default_quality 25 (vaapi_encode_av1.c:851)
  EncoderInfo av1_vaapi = h264_vaapi;
  av1_vaapi.name = "av1_vaapi";
  av1_vaapi.codec_id = AV_CODEC_ID_AV1;
  av1_vaapi.quality_key = "global_quality";
  av1_vaapi.quality_label = QCoreApplication::translate("EncoderOptions", "Quantizer (QP):");
  av1_vaapi.quality_max = 255;
  av1_vaapi.quality_default = 25;
  t.append(av1_vaapi);

  return t;
}

bool is_numeric(const AVOption* o) {
  switch (o->type) {
    case AV_OPT_TYPE_INT:
    case AV_OPT_TYPE_INT64:
    case AV_OPT_TYPE_BOOL:
    case AV_OPT_TYPE_FLOAT:
    case AV_OPT_TYPE_DOUBLE:
      return true;
    default:
      return false;
  }
}

double numeric_default(const AVOption* o) {
  return (o->type == AV_OPT_TYPE_FLOAT || o->type == AV_OPT_TYPE_DOUBLE) ? o->default_val.dbl
                                                                         : double(o->default_val.i64);
}

// The encoder's private option first, then the generic AVCodecContext option of that name (compression_level...)
const AVOption* find_codec_option(const AVCodec* codec, const QString& key) {
  const QByteArray k = key.toUtf8();
  if (codec->priv_class != nullptr) {
    const AVClass* cls = codec->priv_class;
    const AVOption* o = av_opt_find(&cls, k.constData(), nullptr, 0, AV_OPT_SEARCH_FAKE_OBJ);
    if (o != nullptr) return o;
  }
  const AVClass* generic = avcodec_get_class();
  return av_opt_find(&generic, k.constData(), nullptr, 0, AV_OPT_SEARCH_FAKE_OBJ);
}

// Narrows [*min, *max] to the AVOption's range. False when nothing is left.
bool narrow_range(const AVOption* o, int* min, int* max) {
  *min = qMax(*min, int(qBound(double(INT_MIN), std::ceil(o->min), double(INT_MAX))));
  *max = qMin(*max, int(qBound(double(INT_MIN), std::floor(o->max), double(INT_MAX))));
  return *min <= *max;
}

}  // namespace

const QVector<EncoderInfo>& encoder_table() {
  static const QVector<EncoderInfo> kTable = build_table();
  return kTable;
}

EncoderInfo validate_encoder_info(const EncoderInfo& raw, const AVCodec* codec) {
  EncoderInfo v = raw;
  v.options.clear();

  if (!v.quality_key.isEmpty()) {
    const AVOption* q = find_codec_option(codec, v.quality_key);
    if (q == nullptr || !is_numeric(q) || !narrow_range(q, &v.quality_min, &v.quality_max)) {
      v.quality_key.clear();
    } else {
      const double d = numeric_default(q);
      // FFmpeg's quality defaults are "not set" sentinels (-1, 0) outside the usable range: the table's stays then
      if (d >= v.quality_min && d <= v.quality_max) v.quality_default = int(d);
      v.quality_default = qBound(v.quality_min, v.quality_default, v.quality_max);
    }
  }

  for (const EncoderOption& raw_opt : raw.options) {
    const AVOption* o = find_codec_option(codec, raw_opt.key);
    if (o == nullptr) continue;  // this FFmpeg doesn't know it: no row
    EncoderOption opt = raw_opt;
    if (opt.kind == EncoderOption::Int) {
      if (!is_numeric(o) || !narrow_range(o, &opt.min, &opt.max)) continue;
      const double d = numeric_default(o);
      if (d >= opt.min && d <= opt.max) {
        // The encoder's real default is a value of the range: no unset step needed
        opt.default_value = QString::number(int(d));
        opt.unset_label.clear();
      } else if (opt.unset_label.isEmpty()) {
        const int table_default = opt.default_value.isEmpty() ? opt.min : opt.default_value.toInt();
        opt.default_value = QString::number(qBound(opt.min, table_default, opt.max));
      } else {
        opt.default_value.clear();
      }
    } else {
      if (o->type != AV_OPT_TYPE_STRING || opt.choices.isEmpty()) continue;
      // libx264 reports "medium"; libx265 has no default string (x265 picks medium itself): the table's stays
      const QString d = o->default_val.str != nullptr ? QString::fromUtf8(o->default_val.str) : QString();
      if (opt.choices.contains(d)) opt.default_value = d;
      if (!opt.choices.contains(opt.default_value)) opt.default_value = opt.choices.first();
    }
    v.options.append(opt);
  }

  // Fixed options this FFmpeg doesn't know are left out like the rows
  v.quality_mode_options.clear();
  for (const auto& o : raw.quality_mode_options) {
    if (find_codec_option(codec, o.first) != nullptr) v.quality_mode_options.append(o);
  }

  // A stats file option this FFmpeg doesn't know: no two-pass
  if (v.supports_two_pass && !v.two_pass_stats_key.isEmpty()) {
    const AVOption* o = find_codec_option(codec, v.two_pass_stats_key);
    if (o == nullptr || o->type != AV_OPT_TYPE_STRING) v.supports_two_pass = false;
  }
  // The libavcodec running, not the headers built against: a newer or older one of the same major can be loaded
  if (avcodec_version() < v.two_pass_min_avcodec) v.supports_two_pass = false;
  return v;
}

bool pass1_writes_stats(const QString& name) {
  const AVCodec* codec = avcodec_find_encoder_by_name(name.toUtf8().constData());
  if (codec == nullptr) return false;
  AVCodecContext* ctx = avcodec_alloc_context3(codec);
  AVFrame* frame = av_frame_alloc();
  AVPacket* packet = av_packet_alloc();
  bool stats = false;
  if (ctx != nullptr && frame != nullptr && packet != nullptr) {
    // 64x64: SVT-AV1 before 3.0 refuses smaller frames (libsvtav1.c n9.0:219-234)
    ctx->width = 64;
    ctx->height = 64;
    ctx->pix_fmt = AV_PIX_FMT_YUV420P;
    ctx->time_base = {1, 25};
    ctx->framerate = {25, 1};
    ctx->bit_rate = 200000;  // average bitrate, as a two-pass export
    ctx->flags |= AV_CODEC_FLAG_PASS1;
    frame->format = ctx->pix_fmt;
    frame->width = ctx->width;
    frame->height = ctx->height;
    if (avcodec_open2(ctx, codec, nullptr) >= 0 && av_frame_get_buffer(frame, 0) >= 0) {
      for (int plane = 0; plane < 3; plane++)
        memset(frame->data[plane], 128, frame->linesize[plane] * (plane ? 32 : 64));
      frame->pts = 0;
      // One frame, then the drain; stats_out read after every packet and at EOF, as ExportThread::Encode() does
      for (AVFrame* in : {frame, static_cast<AVFrame*>(nullptr)}) {
        if (avcodec_send_frame(ctx, in) < 0) break;
        int ret = 0;
        while (ret >= 0) {
          ret = avcodec_receive_packet(ctx, packet);
          if ((ret >= 0 || ret == AVERROR_EOF) && ctx->stats_out != nullptr && ctx->stats_out[0] != '\0') stats = true;
          av_packet_unref(packet);
        }
        if (ret != AVERROR(EAGAIN) && ret != AVERROR_EOF) break;
      }
    }
  }
  av_packet_free(&packet);
  av_frame_free(&frame);
  avcodec_free_context(&ctx);
  return stats;
}

bool two_pass_works(const EncoderInfo& info) {
  if (!info.supports_two_pass) return false;
  if (!info.two_pass_probe) return true;
  // CBA: the export dialog runs the first probe on the GUI thread (~250 ms once, SVT-AV1 4.2); a QtConcurrent run
  // with the checkbox shown when it ends if that ever shows
  static QMutex mutex;
  static QHash<QString, bool> results;
  QMutexLocker lock(&mutex);  // held during the probe: a second caller waits for its result
  auto it = results.constFind(info.name);
  if (it == results.constEnd()) {
    it = results.insert(info.name, pass1_writes_stats(info.name));
    qInfo() << "Two-pass:" << info.name << (it.value() ? "writes" : "does not write") << "pass-1 statistics";
  }
  return it.value();
}

// CBA: VAAPI only, like the table
AVPixelFormat hardware_frame_format(AVHWDeviceType type) {
  return type == AV_HWDEVICE_TYPE_VAAPI ? AV_PIX_FMT_VAAPI : AV_PIX_FMT_NONE;
}

namespace {

// codec opens on device_ref with a 256x256 hw_frames_ctx of sw_format and encodes one uploaded frame into a packet
bool encode_test_on_device(AVBufferRef* device_ref, const AVCodec* codec, AVPixelFormat hw_format, int sw_format) {
  constexpr int kSize = 256;
  AVBufferRef* frames_ref = av_hwframe_ctx_alloc(device_ref);
  AVCodecContext* ctx = nullptr;
  AVFrame* sw_frame = av_frame_alloc();
  AVFrame* hw_frame = av_frame_alloc();
  AVPacket* packet = av_packet_alloc();
  bool ok = frames_ref != nullptr && sw_frame != nullptr && hw_frame != nullptr && packet != nullptr;
  if (ok) {
    auto* frames = reinterpret_cast<AVHWFramesContext*>(frames_ref->data);
    frames->format = hw_format;
    frames->sw_format = static_cast<AVPixelFormat>(sw_format);
    frames->width = kSize;
    frames->height = kSize;
    ok = av_hwframe_ctx_init(frames_ref) >= 0;
  }
  if (ok) {
    ctx = avcodec_alloc_context3(codec);
    ok = ctx != nullptr;
  }
  if (ok) {
    ctx->width = kSize;
    ctx->height = kSize;
    ctx->pix_fmt = hw_format;
    ctx->time_base = {1, 25};
    ctx->framerate = {25, 1};
    ctx->hw_frames_ctx = av_buffer_ref(frames_ref);
    sw_frame->format = sw_format;
    sw_frame->width = kSize;
    sw_frame->height = kSize;
    ok = ctx->hw_frames_ctx != nullptr && avcodec_open2(ctx, codec, nullptr) >= 0 &&
         av_frame_get_buffer(sw_frame, 0) >= 0;
  }
  if (ok) {
    // Mid grey in both 8-bit NV12 and P010 (0x8080, the top 10 bits used)
    const AVPixFmtDescriptor* desc = av_pix_fmt_desc_get(static_cast<AVPixelFormat>(sw_format));
    for (int plane = 0; plane < 4 && sw_frame->data[plane] != nullptr; plane++) {
      const int rows = plane == 0 || desc == nullptr ? kSize : AV_CEIL_RSHIFT(kSize, desc->log2_chroma_h);
      memset(sw_frame->data[plane], 128, size_t(sw_frame->linesize[plane]) * rows);
    }
    ok = av_hwframe_get_buffer(frames_ref, hw_frame, 0) >= 0 && av_hwframe_transfer_data(hw_frame, sw_frame, 0) >= 0;
    hw_frame->pts = 0;
  }
  bool got_packet = false;
  if (ok) {
    // One frame, then the drain, until a packet comes out
    for (AVFrame* in : {hw_frame, static_cast<AVFrame*>(nullptr)}) {
      if (got_packet || avcodec_send_frame(ctx, in) < 0) break;
      int ret = 0;
      while (ret >= 0) {
        ret = avcodec_receive_packet(ctx, packet);
        if (ret >= 0 && packet->size > 0) got_packet = true;
        av_packet_unref(packet);
      }
      if (ret != AVERROR(EAGAIN) && ret != AVERROR_EOF) break;
    }
  }
  av_packet_free(&packet);
  av_frame_free(&hw_frame);
  av_frame_free(&sw_frame);
  avcodec_free_context(&ctx);
  av_buffer_unref(&frames_ref);
  return got_packet;
}

// info's hw_sw_formats that encode on device_ref (null: none), NV12 first: when the first fails nothing else is tried
QVector<int> probe_formats(AVBufferRef* device_ref, const EncoderInfo& info) {
  const AVCodec* codec = avcodec_find_encoder_by_name(info.name.toUtf8().constData());
  const AVPixelFormat hw_format = hardware_frame_format(info.hw_device_type);
  QVector<int> formats;
  if (device_ref == nullptr || codec == nullptr || hw_format == AV_PIX_FMT_NONE) return formats;
  for (int f : info.hw_sw_formats) {
    if (encode_test_on_device(device_ref, codec, hw_format, f)) {
      formats.append(f);
    } else if (formats.isEmpty()) {
      break;
    }
  }
  return formats;
}

}  // namespace

bool hardware_encode_test(const QString& name, AVHWDeviceType type, int sw_format, const char* device) {
  const AVCodec* codec = avcodec_find_encoder_by_name(name.toUtf8().constData());
  const AVPixelFormat hw_format = hardware_frame_format(type);
  if (codec == nullptr || hw_format == AV_PIX_FMT_NONE) return false;
  AVBufferRef* device_ref = nullptr;
  if (av_hwdevice_ctx_create(&device_ref, type, device, nullptr, 0) < 0) return false;
  const bool ok = encode_test_on_device(device_ref, codec, hw_format, sw_format);
  av_buffer_unref(&device_ref);
  return ok;
}

QVector<int> hardware_encoder_formats(const EncoderInfo& info) {
  if (info.hw_device_type == AV_HWDEVICE_TYPE_NONE) return {};
  // CBA: the export dialog runs the first probe on the GUI thread (see the qInfo below for its cost); a QtConcurrent
  // run if that ever shows
  static QMutex mutex;
  static QHash<QString, QVector<int>> results;
  QMutexLocker lock(&mutex);  // held during the probe: a second caller waits for its result
  auto it = results.constFind(info.name);
  if (it != results.constEnd()) return it.value();

  // Creating the device is most of the cost (radeonsi: ~95 ms the first time in a process, ~20 ms after, against
  // ~1 ms to open an encoder): one device for every installed encoder of this type, all cached at once
  QElapsedTimer timer;
  timer.start();
  AVBufferRef* device_ref = nullptr;
  if (av_hwdevice_ctx_create(&device_ref, info.hw_device_type, nullptr, nullptr, 0) < 0) device_ref = nullptr;
  QVector<EncoderInfo> probed = {info};
  for (const EncoderInfo& e : encoder_table()) {
    if (e.hw_device_type == info.hw_device_type && e.name != info.name && !results.contains(e.name) &&
        avcodec_find_encoder_by_name(e.name.toUtf8().constData()) != nullptr) {
      probed.append(e);
    }
  }
  QStringList summary;
  for (const EncoderInfo& e : probed) {
    const QVector<int> formats = probe_formats(device_ref, e);
    results.insert(e.name, formats);
    QStringList names;
    for (int f : formats) names.append(QString::fromUtf8(av_get_pix_fmt_name(static_cast<AVPixelFormat>(f))));
    summary.append(e.name + ": " + (formats.isEmpty() ? QStringLiteral("unavailable") : names.join(", ")));
  }
  av_buffer_unref(&device_ref);
  qInfo().noquote() << "Hardware encoders:" << summary.join("; ") << QString("(probe %1 ms)").arg(timer.elapsed());
  return results.value(info.name);
}

bool hardware_encoder_works(const EncoderInfo& info) { return !hardware_encoder_formats(info).isEmpty(); }

QString hardware_format_label(int sw_format) {
  switch (sw_format) {
    case AV_PIX_FMT_NV12:
      return QCoreApplication::translate("EncoderOptions", "NV12 (8-bit)");
    case AV_PIX_FMT_P010:
      return QCoreApplication::translate("EncoderOptions", "P010 (10-bit)");
    default:
      return QString::fromUtf8(av_get_pix_fmt_name(static_cast<AVPixelFormat>(sw_format)));
  }
}

bool find_encoder_info(const QString& name, EncoderInfo* out) {
  for (const EncoderInfo& e : encoder_table()) {
    if (e.name != name) continue;
    const AVCodec* codec = avcodec_find_encoder_by_name(name.toUtf8().constData());
    if (codec == nullptr || codec->id != e.codec_id) return false;
    *out = validate_encoder_info(e, codec);
    return true;
  }
  return false;
}

QStringList available_encoders(AVCodecID id) {
  QStringList names;
  bool in_table = false;
  for (const EncoderInfo& e : encoder_table()) {
    if (e.codec_id != id) continue;
    in_table = true;
    const AVCodec* codec = avcodec_find_encoder_by_name(e.name.toUtf8().constData());
    if (codec == nullptr || codec->id != id) continue;
    if (e.hw_device_type != AV_HWDEVICE_TYPE_NONE && !hardware_encoder_works(e)) continue;
    names.append(e.name);
  }
  if (!in_table) {
    const AVCodec* codec = avcodec_find_encoder(id);
    if (codec != nullptr) names.append(QString::fromUtf8(codec->name));
  }
  return names;
}

const AVCodec* find_encoder(const QString& name, AVCodecID id) {
  if (name.isEmpty()) return avcodec_find_encoder(id);
  const AVCodec* codec = avcodec_find_encoder_by_name(name.toUtf8().constData());
  return (codec != nullptr && codec->id == id) ? codec : nullptr;
}

bool export_supports_pix_fmt(int pix_fmt) {
  return pix_fmt == AV_PIX_FMT_PAL8 || sws_isSupportedOutput(static_cast<AVPixelFormat>(pix_fmt)) > 0;
}

bool format_supports_faststart(const AVOutputFormat* fmt) {
  if (fmt == nullptr || fmt->priv_class == nullptr) return false;
  const AVClass* cls = fmt->priv_class;
  return av_opt_find(&cls, "faststart", "movflags", 0, AV_OPT_SEARCH_FAKE_OBJ) != nullptr;
}

bool filename_supports_faststart(const QString& filename) {
  return format_supports_faststart(av_guess_format(nullptr, filename.toUtf8().constData(), nullptr));
}

}  // namespace amber
