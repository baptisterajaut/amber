#include "rendering/encoderoptions.h"

extern "C" {
#include <libavformat/avformat.h>
#include <libavutil/opt.h>
#include <libswscale/swscale.h>
}

#include <QCoreApplication>
#include <climits>
#include <cmath>

namespace amber {

namespace {

QString tr(const char* s) { return QCoreApplication::translate("EncoderOptions", s); }

// x264 and x265 share their preset names (x264.h / x265.h x26x_preset_names; placebo left out)
EncoderOption x26x_preset() {
  EncoderOption o;
  o.key = "preset";
  o.label = tr("Speed preset:");  // "Preset:" is the export preset row at the top of the dialog
  o.tooltip = tr("Slower presets make smaller files at the same quality");
  o.kind = EncoderOption::Choice;
  o.choices = {"ultrafast", "superfast", "veryfast", "faster", "fast", "medium", "slow", "slower", "veryslow"};
  o.choice_labels = {tr("Ultrafast"), tr("Superfast"), tr("Very fast"), tr("Faster"),   tr("Fast"),
                     tr("Medium"),    tr("Slow"),      tr("Slower"),    tr("Very slow")};
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
  const QString x26x_crf_tooltip =
      tr("Quality Factor:\n\n0 = lossless\n17-18 = visually lossless (compressed, but unnoticeable)\n23 = high "
         "quality\n51 = lowest quality possible");
  QVector<EncoderInfo> t;

  // ffmpeg -h encoder=libx264 (FFmpeg 9.0): -preset <string> (default "medium"); -crf <float> (from -1 to FLT_MAX)
  // (default -1). CRF 36 is Amber's long-standing default; -1 is "not set".
  EncoderInfo x264;
  x264.name = "libx264";
  x264.codec_id = AV_CODEC_ID_H264;
  x264.label = "x264";
  x264.quality_key = "crf";
  x264.quality_mode_label = tr("Quality-based (Constant Rate Factor)");
  x264.quality_label = tr("Quality (CRF):");
  x264.quality_tooltip = x26x_crf_tooltip;
  x264.quality_min = 0;
  x264.quality_max = 51;
  x264.quality_default = 36;
  x264.supports_average_bitrate = true;
  // Two-pass, FFmpeg n9.0 libx264.c:1119-1121: PASS1 sets x264's b_stat_write, PASS2 b_stat_read, through the stats
  // file of the "stats" option (libx264.c:1545; x264's default name is in the working directory)
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
  // libx265.c:552-568: PASS1/PASS2 become x265's pass=1/2, the stats file comes from -x265-stats (libx265.c:1050)
  x265.two_pass_stats_key = "x265-stats";
  t.append(x265);

  // AV1, in preference order (available_encoders keeps it): SVT-AV1 is the fast one, libaom the slow reference.
  // ffmpeg -h encoder=libsvtav1: -preset <int> (from -2 to 13) (default -2); -crf <int> (from 0 to 63) (default 0).
  // FFmpeg's wrapper only applies preset >= -1 and crf > 0; SVT-AV1 4.2 marks presets -2/-1 debug only, and
  // SvtAv1EncApp --help gives CRF 35 as SVT-AV1's default.
  EncoderInfo svt;
  svt.name = "libsvtav1";
  svt.codec_id = AV_CODEC_ID_AV1;
  svt.label = tr("SVT-AV1 (fast)");
  svt.quality_key = "crf";
  svt.quality_mode_label = tr("Quality-based (Constant Rate Factor)");
  svt.quality_label = tr("Quality (CRF):");
  svt.quality_min = 1;
  svt.quality_max = 63;
  svt.quality_default = 35;
  svt.supports_average_bitrate = true;
  // libsvtav1.c:366-401: PASS2 decodes stats_in into rc_stats_buffer, PASS1 sets pass=1; 677-710: stats_out once, on
  // the end-of-stream packet (SVT-AV1 2.0+)
  svt.supports_two_pass = true;
  svt.options = {int_option("preset", tr("Speed preset:"),
                            tr("0 = slowest, smallest file\n13 = fastest\nDefault lets SVT-AV1 choose"), 0, 13,
                            QString(), tr("Default"))};
  t.append(svt);

  // ffmpeg -h encoder=librav1e: -qp <int> (from -1 to 255) (default -1); -speed <int> (from -1 to 10) (default -1).
  // -1 is "not set" for both; rav1e --help gives quantizer 100 and speed 6 as its defaults.
  EncoderInfo rav1e;
  rav1e.name = "librav1e";
  rav1e.codec_id = AV_CODEC_ID_AV1;
  rav1e.label = "rav1e";
  rav1e.quality_key = "qp";
  rav1e.quality_mode_label = tr("Quality-based (Constant Quantizer)");
  rav1e.quality_label = tr("Quantizer (QP):");
  rav1e.quality_min = 0;
  rav1e.quality_max = 255;
  rav1e.quality_default = 100;
  rav1e.supports_average_bitrate = true;
  // No two-pass: librav1e.c handles PASS1/PASS2 (225-249, 517-535), but never asks rav1e to emit pass data
  // (rav1e_config_set_emit_data, rav1e.h), so stats_out stays empty: ffmpeg -pass 1 writes a 0-byte log with
  // FFmpeg 9.0.2 and rav1e 0.8.1
  rav1e.options = {int_option("speed", tr("Speed:"),
                              tr("0 = slowest, smallest file\n10 = fastest\nDefault lets rav1e choose (6)"), 0, 10,
                              QString(), tr("Default"))};
  t.append(rav1e);

  // ffmpeg -h encoder=libaom-av1: -cpu-used <int> (from 0 to 8) (default 1); -crf <int> (from -1 to 63) (default -1).
  // Constant quality needs bit_rate 0: libaomenc.c picks AOM_Q for crf >= 0 without a bitrate, and the encoder's
  // codec defaults already set b=0. With neither, it falls back to crf 32.
  EncoderInfo aom;
  aom.name = "libaom-av1";
  aom.codec_id = AV_CODEC_ID_AV1;
  aom.label = tr("libaom (slow)");
  aom.quality_key = "crf";
  aom.quality_mode_label = tr("Quality-based (Constant Rate Factor)");
  aom.quality_label = tr("Quality (CRF):");
  aom.quality_min = 0;
  aom.quality_max = 63;
  aom.quality_default = 32;
  aom.supports_average_bitrate = true;
  // libaomenc.c:840-845 and 921-945: PASS1/PASS2 select the first/last pass, PASS2 decodes stats_in; 1387-1397:
  // stats_out at the drain
  aom.supports_two_pass = true;
  aom.options = {int_option("cpu-used", tr("Speed:"), tr("0 = slowest, smallest file\n8 = fastest"), 0, 8, "1")};
  t.append(aom);

  // ffmpeg -h encoder=libvpx-vp9: -crf <int> (from -1 to 63) (default -1); -cpu-used <int> (from -8 to 8) (default 1).
  // Constant quality needs bit_rate 0 (the -b:v 0 of FFmpeg's VP9 guide, which recommends CRF 31 for 1080p):
  // libvpxenc.c:1104-1111 picks VPX_Q for crf >= 0 without a bitrate, VPX_CQ (crf as a floor under a bitrate)
  // with one. The encoder's codec defaults set b=0 (libvpxenc.c:2097). Two-pass: libvpxenc.c:1097-1100 and
  // 1189-1213 (PASS2 decodes stats_in), 1964-1974 (stats_out at the drain).
  EncoderInfo vp9;
  vp9.name = "libvpx-vp9";
  vp9.codec_id = AV_CODEC_ID_VP9;
  vp9.label = "libvpx";
  vp9.quality_key = "crf";
  vp9.quality_mode_label = tr("Quality-based (Constant Quality)");
  vp9.quality_label = tr("Quality (CRF):");
  vp9.quality_min = 0;
  vp9.quality_max = 63;
  vp9.quality_default = 31;
  vp9.supports_average_bitrate = true;
  vp9.supports_two_pass = true;
  vp9.options = {int_option("cpu-used", tr("Speed:"),
                            tr("Higher is faster, lower makes smaller files at the same quality"), 0, 8, "1")};
  t.append(vp9);

  // compression_level is a generic AVCodecContext option (-1 = encoder default); flacenc.c uses 5 for -1 and
  // rejects levels above 12
  EncoderInfo flac;
  flac.name = "flac";
  flac.codec_id = AV_CODEC_ID_FLAC;
  flac.label = "FLAC";
  flac.options = {int_option("compression_level", tr("Compression level:"),
                             tr("Higher = smaller file, slower export; always lossless"), 0, 12, "5")};
  t.append(flac);

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

  // A stats file option this FFmpeg doesn't know: no two-pass
  if (v.supports_two_pass && !v.two_pass_stats_key.isEmpty()) {
    const AVOption* o = find_codec_option(codec, v.two_pass_stats_key);
    if (o == nullptr || o->type != AV_OPT_TYPE_STRING) v.supports_two_pass = false;
  }
  return v;
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
    if (codec != nullptr && codec->id == id) names.append(e.name);
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
