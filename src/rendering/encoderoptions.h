#ifndef ENCODEROPTIONS_H
#define ENCODEROPTIONS_H

#include <QPair>
#include <QString>
#include <QStringList>
#include <QVector>

extern "C" {
#include <libavcodec/avcodec.h>
}

struct AVOutputFormat;

// Per-encoder export settings the export dialog shows as extra rows (ROADMAP "Per-codec options panel", #58). Pure
// data plus FFmpeg lookups: no widgets, so the tests and ExportThread use it too.
// CBA: no hardware encoders yet (ROADMAP #58)
namespace amber {

// One encoder setting, shown as a combo (Choice) or a spin box (Int)
struct EncoderOption {
  enum Kind { Choice, Int };

  QString key;  // AVOption name: the encoder's private options first, then the generic AVCodecContext ones
  QString label;
  QString tooltip;
  Kind kind = Int;
  QStringList choices;        // Choice: the values passed to FFmpeg
  QStringList choice_labels;  // Choice: what the combo shows, same order
  int min = 0;                // Int: usable range, narrowed to the AVOption's own range by validation
  int max = 0;
  // What the encoder uses when the option isn't passed. Empty (Int only): the encoder picks, shown as unset_label.
  QString default_value;
  // Int: non-empty when the encoder's default is a "not set" sentinel outside [min, max]. The spin box then gets one
  // extra step below min showing this text, which passes nothing.
  QString unset_label;
};

struct EncoderInfo {
  QString name;  // FFmpeg encoder name (avcodec_find_encoder_by_name)
  AVCodecID codec_id = AV_CODEC_ID_NONE;
  QString label;               // shown in the Encoder combo
  QString quality_key;         // AVOption of the quality-based mode; empty: no quality mode
  QString quality_mode_label;  // "Quality-based (Constant Rate Factor)"
  QString quality_label;       // "Quality (CRF):"
  QString quality_tooltip;     // empty: a generic "min = best ... max = smallest"
  int quality_min = 0;
  int quality_max = 0;
  int quality_default = 0;
  bool supports_average_bitrate = false;  // bit_rate alone gives an average-bitrate (ABR/VBR) encode
  // FFmpeg's wrapper implements AV_CODEC_FLAG_PASS1/PASS2: an average-bitrate export can analyse first
  // (ExportThread). The stats go through stats_out/stats_in, or with a file option through a file. With
  // two_pass_probe, two_pass_works() has the last word.
  bool supports_two_pass = false;
  QString two_pass_stats_key;  // AVOption naming the stats file (libx264, libx265); empty: stats_out/stats_in
  // The first libavcodec (AV_VERSION_INT, compared with avcodec_version()) whose wrapper has two-pass; 0: all
  unsigned two_pass_min_avcodec = 0;
  // Whether pass 1 writes statistics also depends on the encoder library FFmpeg was built against, which libavcodec
  // doesn't tell: two_pass_works() checks it with a test encode
  bool two_pass_probe = false;
  QVector<EncoderOption> options;
};

// key -> value pairs for avcodec_open2()'s options dictionary
using EncoderOptionValues = QVector<QPair<QString, QString>>;

// The raw table, before validation against the installed FFmpeg
const QVector<EncoderInfo>& encoder_table();

// raw checked against codec's AVOptions: options (and the quality key) the encoder doesn't know are dropped, Int
// ranges narrowed to the AVOption's range, and a default inside the range taken from the AVOption
EncoderInfo validate_encoder_info(const EncoderInfo& raw, const AVCodec* codec);

// The validated descriptor of name. False when name isn't in the table or isn't installed.
bool find_encoder_info(const QString& name, EncoderInfo* out);

// info.supports_two_pass, checked with pass1_writes_stats() for an encoder with two_pass_probe. The test encode runs
// at most once per process and encoder (SVT-AV1 4.2: about 250 ms, mostly opening the encoder). Thread-safe.
bool two_pass_works(const EncoderInfo& info);

// Uncached: name, opened with AV_CODEC_FLAG_PASS1 for a 64x64 average-bitrate encode, puts statistics in stats_out
// for one frame and the drain
bool pass1_writes_stats(const QString& name);

// Installed encoders of id from the table, in preference order. A codec without table entries gets its default
// encoder (avcodec_find_encoder), if any. A codec whose table encoders are all missing gets none.
QStringList available_encoders(AVCodecID id);

// The named encoder, or the codec's default one (avcodec_find_encoder) when name is empty. Null when missing or when
// it encodes another codec.
const AVCodec* find_encoder(const QString& name, AVCodecID id);

// The export can write this pixel format: swscale converts the RGBA composite to it, except PAL8 which goes through
// the palettegen/paletteuse filters (ExportThread::SetupPaletteGraph)
bool export_supports_pix_fmt(int pix_fmt);

// The muxer has movflags=+faststart (the MOV family: mp4, mov, 3gp...)
bool format_supports_faststart(const AVOutputFormat* fmt);
// Same, for the muxer FFmpeg guesses from a filename (or "x.ext")
bool filename_supports_faststart(const QString& filename);

}  // namespace amber

#endif  // ENCODEROPTIONS_H
