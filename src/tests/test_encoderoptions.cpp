// Encoder descriptors (rendering/encoderoptions): validation against the installed FFmpeg's AVOptions, encoder
// availability and order, muxer fast start detection. Encoders missing from this FFmpeg build are skipped. Also the
// file names of a batch export (rendering/batchexport).

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/opt.h>
}

#include <QTest>

#include "rendering/batchexport.h"
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

  // Two-pass where FFmpeg n9.0's wrapper implements AV_CODEC_FLAG_PASS1/PASS2 (sources cited in encoderoptions.cpp),
  // libx264 and libx265 through a stats file option
  void twoPassFlags() {
    // Not librav1e: its wrapper never gets pass data out of rav1e (encoderoptions.cpp)
    const QStringList two_pass = {"libx264", "libx265", "libvpx-vp9", "libaom-av1", "libsvtav1"};
    for (const EncoderInfo& raw : amber::encoder_table()) {
      QCOMPARE(raw.supports_two_pass, two_pass.contains(raw.name));
      EncoderInfo info;
      if (!amber::find_encoder_info(raw.name, &info)) continue;  // not installed
      qInfo("%s: two-pass %d, stats file option \"%s\"", qPrintable(info.name), int(info.supports_two_pass),
            qPrintable(info.two_pass_stats_key));
      QCOMPARE(info.supports_two_pass, raw.supports_two_pass);
      // Only for average bitrate, which every two-pass encoder has
      if (info.supports_two_pass) QVERIFY(info.supports_average_bitrate);
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

  void batchFilenameKeepsName() {
    QCOMPARE(amber::batch_export_filename("Interview", "mp4", {}), QString("Interview.mp4"));
    QCOMPARE(amber::batch_export_filename("Sequence 01", "mkv", {}), QString("Sequence 01.mkv"));
    QCOMPARE(amber::batch_export_filename(QString::fromUtf8("Été – plan 2"), "mov", {}),
             QString::fromUtf8("Été – plan 2.mov"));
  }

  // What Windows refuses (and '/' everywhere) becomes '_'; so do control characters
  void batchFilenameReplacesInvalidCharacters() {
    QCOMPARE(amber::batch_export_filename("a/b\\c:d*e?f\"g<h>i|j", "mp4", {}), QString("a_b_c_d_e_f_g_h_i_j.mp4"));
    QCOMPARE(amber::batch_export_filename("tab\there\nnewline", "mp4", {}), QString("tab_here_newline.mp4"));
    QCOMPARE(amber::batch_export_filename("50% cut", "mp4", {}), QString("50% cut.mp4"));
  }

  // Surrounding spaces and trailing dots go (Windows drops trailing dots); nothing left gives "Sequence"
  void batchFilenameEmptyName() {
    QCOMPARE(amber::batch_export_filename("", "mp4", {}), QString("Sequence.mp4"));
    QCOMPARE(amber::batch_export_filename("   ", "mp4", {}), QString("Sequence.mp4"));
    QCOMPARE(amber::batch_export_filename("...", "mp4", {}), QString("Sequence.mp4"));
    QCOMPARE(amber::batch_export_filename("  Teaser  ", "mp4", {}), QString("Teaser.mp4"));
    QCOMPARE(amber::batch_export_filename("Final. ", "mp4", {}), QString("Final.mp4"));
    QCOMPARE(amber::batch_export_filename("v1.2", "mp4", {}), QString("v1.2.mp4"));
  }

  // A taken name gets " (2)", " (3)"... before the extension, taken names compared case-insensitively
  void batchFilenameCollisions() {
    QCOMPARE(amber::batch_export_filename("Interview", "mp4", {"Interview.mp4"}), QString("Interview (2).mp4"));
    QCOMPARE(amber::batch_export_filename("Interview", "mp4", {"interview.MP4", "Interview (2).mp4"}),
             QString("Interview (3).mp4"));
    // Another extension, or a longer name, isn't a collision
    QCOMPARE(amber::batch_export_filename("Interview", "mp4", {"Interview.mov", "Interview 2.mp4"}),
             QString("Interview.mp4"));
    QCOMPARE(amber::batch_export_filename("", "mp4", {"Sequence.mp4"}), QString("Sequence (2).mp4"));
    QCOMPARE(amber::batch_export_filename("a/b", "mp4", {"a_b.mp4"}), QString("a_b (2).mp4"));
  }

  // An image sequence's frame files take the name they were exported under, read with exactly the pattern's digits:
  // the frames of "Sequence 01" are "Sequence 0100001.png"..., not frames of "Sequence "
  void batchImageSequenceNames() {
    const QStringList entries{"Sequence 0100001.png", "Sequence 0100002.png", "Take 7.png", "notes.txt"};
    const QStringList taken = amber::batch_export_taken_names(entries, true);
    QVERIFY(taken.contains("Sequence 01.png"));
    QVERIFY(!taken.contains("Sequence .png"));
    QVERIFY(taken.contains("Take 7.png"));
    QCOMPARE(amber::batch_export_filename("Sequence 01", "png", taken), QString("Sequence 01 (2).png"));
    QCOMPARE(amber::batch_export_filename("Sequence", "png", taken), QString("Sequence.png"));
    QCOMPARE(amber::batch_export_filename("Sequence 0", "png", taken), QString("Sequence 0.png"));
    // Not an image sequence: the entries as they are
    QCOMPARE(amber::batch_export_taken_names(entries, false), entries);

    QCOMPARE(amber::batch_export_path("/out", "Sequence 01.png", "png", true), QString("/out/Sequence 01%05d.png"));
    QCOMPARE(amber::batch_export_path("/out", "50% cut.png", "png", true), QString("/out/50%% cut%05d.png"));
    QCOMPARE(amber::batch_export_path("/out", "50% cut.mp4", "mp4", false), QString("/out/50% cut.mp4"));
  }
};

QTEST_GUILESS_MAIN(TestEncoderOptions)
#include "test_encoderoptions.moc"
