// Encoder descriptors (rendering/encoderoptions): validation against the installed FFmpeg's AVOptions, encoder
// availability and order, muxer fast start detection. Encoders missing from this FFmpeg build are skipped.

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/opt.h>
}

#include <QTest>

#include "rendering/encoderoptions.h"

using amber::EncoderInfo;
using amber::EncoderOption;

namespace {

const EncoderOption* find_option(const EncoderInfo& info, const QString& key) {
  for (const EncoderOption& o : info.options) {
    if (o.key == key) return &o;
  }
  return nullptr;
}

bool has_string_option(const AVCodec* codec, const char* key) {
  if (codec->priv_class == nullptr) return false;
  const AVClass* cls = codec->priv_class;
  const AVOption* o = av_opt_find(&cls, key, nullptr, 0, AV_OPT_SEARCH_FAKE_OBJ);
  return o != nullptr && o->type == AV_OPT_TYPE_STRING;
}

QString avcodec_version_string() {
  const unsigned v = avcodec_version();
  return QString("%1.%2.%3").arg(v >> 16).arg((v >> 8) & 0xff).arg(v & 0xff);
}

}  // namespace

class TestEncoderOptions : public QObject {
  Q_OBJECT

 private slots:
  // An option the encoder doesn't know is dropped, a known one kept; a generic AVCodecContext option is found too
  void validationDropsUnknownKey() {
    const AVCodec* flac = avcodec_find_encoder_by_name("flac");
    if (flac == nullptr) QSKIP("flac encoder not in this FFmpeg build");
    EncoderInfo raw;
    raw.name = "flac";
    raw.codec_id = AV_CODEC_ID_FLAC;
    raw.quality_key = "no_such_quality_option";
    EncoderOption level;
    level.key = "compression_level";
    level.kind = EncoderOption::Int;
    level.min = 0;
    level.max = 12;
    level.default_value = "5";
    EncoderOption bogus;
    bogus.key = "amber_no_such_option";
    bogus.kind = EncoderOption::Int;
    bogus.min = 0;
    bogus.max = 1;
    bogus.default_value = "0";
    raw.options = {bogus, level};

    const EncoderInfo v = amber::validate_encoder_info(raw, flac);
    QCOMPARE(v.options.size(), 1);
    QCOMPARE(v.options.at(0).key, QString("compression_level"));
    QVERIFY(v.quality_key.isEmpty());
    // compression_level is a generic option, -1 ("encoder default") .. INT_MAX: the table's range stays, and the
    // sentinel default is replaced by the table's
    QCOMPARE(v.options.at(0).min, 0);
    QCOMPARE(v.options.at(0).max, 12);
    QCOMPARE(v.options.at(0).default_value, QString("5"));
  }

  // The installed FFmpeg narrows a range: libsvtav1's preset runs -2..13 there, the table asks 0..20
  void validationNarrowsRangeToFFmpeg() {
    const AVCodec* svt = avcodec_find_encoder_by_name("libsvtav1");
    if (svt == nullptr) QSKIP("libsvtav1 not in this FFmpeg build");
    EncoderInfo raw;
    raw.name = "libsvtav1";
    raw.codec_id = AV_CODEC_ID_AV1;
    EncoderOption preset;
    preset.key = "preset";
    preset.kind = EncoderOption::Int;
    preset.min = 0;
    preset.max = 20;
    preset.unset_label = "Default";
    raw.options = {preset};
    const EncoderInfo v = amber::validate_encoder_info(raw, svt);
    QCOMPARE(v.options.size(), 1);
    QCOMPARE(v.options.at(0).min, 0);
    QVERIFY2(v.options.at(0).max < 20, qPrintable(QString::number(v.options.at(0).max)));
    // FFmpeg's default (-2, "not set") is below the range: the row starts on the unset step
    QVERIFY(v.options.at(0).default_value.isEmpty());
    QCOMPARE(v.options.at(0).unset_label, QString("Default"));
  }

  // Ranges and defaults of the real table, as the installed FFmpeg reports them
  void libx264QualityAndPreset() {
    if (avcodec_find_encoder_by_name("libx264") == nullptr) QSKIP("libx264 not in this FFmpeg build");
    EncoderInfo x264;
    QVERIFY(amber::find_encoder_info("libx264", &x264));
    QCOMPARE(x264.codec_id, AV_CODEC_ID_H264);
    QCOMPARE(x264.quality_key, QString("crf"));
    // FFmpeg's crf runs -1..FLT_MAX: the table's 0..51 stays, and Amber's CRF 36 default with it
    QCOMPARE(x264.quality_min, 0);
    QCOMPARE(x264.quality_max, 51);
    QCOMPARE(x264.quality_default, 36);
    QVERIFY(x264.supports_average_bitrate);
    const EncoderOption* preset = find_option(x264, "preset");
    QVERIFY(preset != nullptr);
    QCOMPARE(preset->kind, EncoderOption::Choice);
    QCOMPARE(preset->default_value, QString("medium"));  // FFmpeg's own default for libx264
    QVERIFY(preset->choices.contains("ultrafast"));
    QVERIFY(preset->choices.contains("veryslow"));
    QCOMPARE(preset->choices.size(), preset->choice_labels.size());
  }

  void libsvtav1Ranges() {
    const AVCodec* svt = avcodec_find_encoder_by_name("libsvtav1");
    if (svt == nullptr) QSKIP("libsvtav1 not in this FFmpeg build");
    EncoderInfo info;
    QVERIFY(amber::find_encoder_info("libsvtav1", &info));
    const AVClass* cls = svt->priv_class;
    const AVOption* crf = av_opt_find(&cls, "crf", nullptr, 0, AV_OPT_SEARCH_FAKE_OBJ);
    const AVOption* preset_opt = av_opt_find(&cls, "preset", nullptr, 0, AV_OPT_SEARCH_FAKE_OBJ);
    QVERIFY(crf != nullptr);
    QVERIFY(preset_opt != nullptr);
    qInfo("libsvtav1 crf %g..%g, preset %g..%g (default %lld)", crf->min, crf->max, preset_opt->min, preset_opt->max,
          static_cast<long long>(preset_opt->default_val.i64));
    QCOMPARE(info.quality_key, QString("crf"));
    // crf 0 means "not set" in FFmpeg's wrapper (it only applies crf > 0): the usable range starts at 1
    QCOMPARE(info.quality_min, 1);
    QCOMPARE(info.quality_max, int(crf->max));
    const EncoderOption* preset = find_option(info, "preset");
    QVERIFY(preset != nullptr);
    QCOMPARE(preset->max, int(preset_opt->max));
    QVERIFY(preset->min >= int(preset_opt->min));
  }

  void librav1eQuantizer() {
    if (avcodec_find_encoder_by_name("librav1e") == nullptr) QSKIP("librav1e not in this FFmpeg build");
    EncoderInfo info;
    QVERIFY(amber::find_encoder_info("librav1e", &info));
    QCOMPARE(info.quality_key, QString("qp"));
    QCOMPARE(info.quality_min, 0);
    QCOMPARE(info.quality_max, 255);
    QVERIFY(find_option(info, "speed") != nullptr);
  }

  void flacCompressionLevel() {
    if (avcodec_find_encoder_by_name("flac") == nullptr) QSKIP("flac encoder not in this FFmpeg build");
    EncoderInfo info;
    QVERIFY(amber::find_encoder_info("flac", &info));
    QVERIFY(info.quality_key.isEmpty());
    const EncoderOption* level = find_option(info, "compression_level");
    QVERIFY(level != nullptr);
    QCOMPARE(level->min, 0);
    QCOMPARE(level->max, 12);
    QCOMPARE(level->default_value, QString("5"));
  }

  // libvpx-vp9: CRF 0..63 (FFmpeg's -1 is "not set"), the VP9 guide's 31 by default, cpu-used narrowed to 0..8
  void libvpxVp9Descriptor() {
    if (avcodec_find_encoder_by_name("libvpx-vp9") == nullptr) QSKIP("libvpx-vp9 not in this FFmpeg build");
    EncoderInfo info;
    QVERIFY(amber::find_encoder_info("libvpx-vp9", &info));
    QCOMPARE(info.codec_id, AV_CODEC_ID_VP9);
    QCOMPARE(info.quality_key, QString("crf"));
    QCOMPARE(info.quality_min, 0);
    QCOMPARE(info.quality_max, 63);
    QCOMPARE(info.quality_default, 31);
    QVERIFY(info.supports_average_bitrate);
    QVERIFY(info.supports_two_pass);
    QVERIFY(info.two_pass_stats_key.isEmpty());  // stats_out/stats_in
    const EncoderOption* speed = find_option(info, "cpu-used");
    QVERIFY(speed != nullptr);
    QCOMPARE(speed->kind, EncoderOption::Int);
    QCOMPARE(speed->min, 0);
    QCOMPARE(speed->max, 8);
    QCOMPARE(speed->default_value, QString("1"));  // FFmpeg's default
    QCOMPARE(info.options.size(), 1);
    // VP8 stays on the old path
    QVERIFY(!amber::find_encoder_info("libvpx", &info));
  }

  // Two-pass where the installed FFmpeg's wrapper implements AV_CODEC_FLAG_PASS1/PASS2 (sources cited in
  // encoderoptions.cpp): libx264, libvpx-vp9 and libaom-av1 in every FFmpeg that has them, libx265 where it has its
  // x265-stats option, libsvtav1 from libavcodec 62.23.104 (both FFmpeg 8.1). libx264 and libx265 use a stats file.
  void twoPassFlags() {
    // Not librav1e: its wrapper never gets pass data out of rav1e (encoderoptions.cpp)
    const QStringList two_pass = {"libx264", "libx265", "libvpx-vp9", "libaom-av1", "libsvtav1"};
    for (const EncoderInfo& raw : amber::encoder_table()) {
      QCOMPARE(raw.supports_two_pass, two_pass.contains(raw.name));
      // Only libsvtav1's statistics depend on the SVT-AV1 FFmpeg was built against
      QCOMPARE(raw.two_pass_probe, raw.name == "libsvtav1");
      EncoderInfo info;
      if (!amber::find_encoder_info(raw.name, &info)) continue;  // not installed
      const AVCodec* codec = avcodec_find_encoder_by_name(raw.name.toUtf8().constData());
      bool expected = raw.supports_two_pass;
      if (raw.name == "libx265") expected = has_string_option(codec, "x265-stats");
      if (raw.name == "libsvtav1") expected = avcodec_version() >= AV_VERSION_INT(62, 23, 104);
      qInfo("%s: two-pass %d (libavcodec %s), stats file option \"%s\"", qPrintable(info.name),
            int(info.supports_two_pass), qPrintable(avcodec_version_string()), qPrintable(info.two_pass_stats_key));
      QCOMPARE(info.supports_two_pass, expected);
      // Only for average bitrate, which every two-pass encoder has
      if (info.supports_two_pass) QVERIFY(info.supports_average_bitrate);
      // No test encode without two_pass_probe
      if (!info.two_pass_probe) QCOMPARE(amber::two_pass_works(info), info.supports_two_pass);
    }
    EncoderInfo x264;
    if (amber::find_encoder_info("libx264", &x264)) QCOMPARE(x264.two_pass_stats_key, QString("stats"));
    EncoderInfo x265;
    if (amber::find_encoder_info("libx265", &x265)) QCOMPARE(x265.two_pass_stats_key, QString("x265-stats"));
  }

  // A stats file option the encoder doesn't know turns two-pass off
  void twoPassNeedsItsStatsOption() {
    const AVCodec* x264 = avcodec_find_encoder_by_name("libx264");
    if (x264 == nullptr) QSKIP("libx264 not in this FFmpeg build");
    EncoderInfo raw;
    raw.name = "libx264";
    raw.codec_id = AV_CODEC_ID_H264;
    raw.supports_two_pass = true;
    raw.two_pass_stats_key = "stats";
    QVERIFY(amber::validate_encoder_info(raw, x264).supports_two_pass);
    raw.two_pass_stats_key = "amber_no_such_stats_option";
    QVERIFY(!amber::validate_encoder_info(raw, x264).supports_two_pass);
  }

  // A libavcodec older than two_pass_min_avcodec turns two-pass off
  void twoPassNeedsItsAvcodec() {
    const AVCodec* flac = avcodec_find_encoder_by_name("flac");
    if (flac == nullptr) QSKIP("flac encoder not in this FFmpeg build");
    EncoderInfo raw;
    raw.name = "flac";
    raw.codec_id = AV_CODEC_ID_FLAC;
    raw.supports_two_pass = true;
    raw.two_pass_min_avcodec = avcodec_version();
    QVERIFY(amber::validate_encoder_info(raw, flac).supports_two_pass);
    raw.two_pass_min_avcodec = avcodec_version() + 1;
    QVERIFY(!amber::validate_encoder_info(raw, flac).supports_two_pass);
  }

  // The pass-1 test encode tells an encoder that writes statistics from one that doesn't, and two_pass_works() follows
  // it for libsvtav1
  void pass1Probe() {
    QVERIFY(!amber::pass1_writes_stats("amber_no_such_encoder"));
    if (avcodec_find_encoder_by_name("libvpx-vp9") != nullptr) QVERIFY(amber::pass1_writes_stats("libvpx-vp9"));
    // librav1e takes PASS1 but never gets pass data out of rav1e (encoderoptions.cpp)
    if (avcodec_find_encoder_by_name("librav1e") != nullptr) QVERIFY(!amber::pass1_writes_stats("librav1e"));

    EncoderInfo svt;
    if (!amber::find_encoder_info("libsvtav1", &svt)) QSKIP("libsvtav1 not in this FFmpeg build");
    const bool writes = amber::pass1_writes_stats("libsvtav1");
    qInfo("libsvtav1, libavcodec %s: two-pass %d, pass 1 writes statistics %d", qPrintable(avcodec_version_string()),
          int(svt.supports_two_pass), int(writes));
    // Before FFmpeg 8.1 the wrapper ignores PASS1 (FFmpeg 6.1 with SVT-AV1 1.7: a plain encode, no statistics)
    if (!svt.supports_two_pass) QVERIFY(!writes);
    QCOMPARE(amber::two_pass_works(svt), svt.supports_two_pass && writes);
    QCOMPARE(amber::two_pass_works(svt), svt.supports_two_pass && writes);  // the cached answer
  }

  void unknownEncoderNotFound() {
    EncoderInfo info;
    QVERIFY(!amber::find_encoder_info("amber_no_such_encoder", &info));
    // An installed encoder without a descriptor
    QVERIFY(!amber::find_encoder_info("mpeg4", &info));
  }

  // AV1: SVT-AV1, then rav1e, then libaom, whichever are installed, never a hardware encoder
  void av1EncoderOrder() {
    const QStringList av1 = amber::available_encoders(AV_CODEC_ID_AV1);
    qInfo("AV1 encoders: %s", qPrintable(av1.join(", ")));
    QStringList expected;
    for (const char* name : {"libsvtav1", "librav1e", "libaom-av1"}) {
      if (avcodec_find_encoder_by_name(name) != nullptr) expected.append(name);
    }
    QCOMPARE(av1, expected);
  }

  // A codec without table entries gets FFmpeg's default encoder, as before the table
  void codecWithoutTableUsesDefaultEncoder() {
    const AVCodec* mpeg4 = avcodec_find_encoder(AV_CODEC_ID_MPEG4);
    if (mpeg4 == nullptr) QSKIP("no MPEG-4 encoder");
    QCOMPARE(amber::available_encoders(AV_CODEC_ID_MPEG4), QStringList{QString(mpeg4->name)});
    if (avcodec_find_encoder_by_name("libx264") != nullptr) {
      QCOMPARE(amber::available_encoders(AV_CODEC_ID_H264), QStringList{"libx264"});
    }
  }

  void faststartMuxers() {
    QVERIFY(amber::filename_supports_faststart("x.mp4"));
    QVERIFY(amber::filename_supports_faststart("x.mov"));
    QVERIFY(amber::filename_supports_faststart("x.3gp"));
    QVERIFY(!amber::filename_supports_faststart("x.mkv"));
    QVERIFY(!amber::filename_supports_faststart("x.webm"));
    QVERIFY(!amber::filename_supports_faststart("x.avi"));
    QVERIFY(!amber::filename_supports_faststart("x.nosuchextension"));
  }
};

QTEST_GUILESS_MAIN(TestEncoderOptions)
#include "test_encoderoptions.moc"
