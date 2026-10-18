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

#ifndef EXPORTTHREAD_H
#define EXPORTTHREAD_H

#include <QByteArray>
#include <QMutex>
#include <QOffscreenSurface>
#include <QPair>
#include <QTemporaryDir>
#include <QThread>
#include <QVector>
#include <QWaitCondition>
#include <atomic>
#include <memory>

struct AVFormatContext;
struct AVCodecContext;
struct AVFrame;
struct AVPacket;
struct AVStream;
struct AVCodec;
struct SwsContext;
struct SwrContext;
struct AVFilterGraph;
struct AVFilterContext;
class Sequence;
class RenderThread;

extern "C" {
#include <libavcodec/avcodec.h>
}

#define COMPRESSION_TYPE_CBR 0
#define COMPRESSION_TYPE_CFR 1  // quality-based: the encoder's quality option (crf, qp...) gets video_bitrate
#define COMPRESSION_TYPE_TARGETSIZE 2
#define COMPRESSION_TYPE_TARGETBR 3  // average bitrate: bit_rate alone (ABR/VBR), one pass unless two_pass

// structs that store parameters passed from the export dialogs to this thread

struct ExportParams {
  // export parameters
  QString filename;
  bool video_enabled;
  int video_codec;
  int video_width;
  int video_height;
  int video_compression_type;
  double video_bitrate;
  bool audio_enabled;
  int audio_codec;
  int audio_sampling_rate;
  int audio_bitrate;
  long start_frame;
  long end_frame;
  // FFmpeg encoder names (rendering/encoderoptions.h); empty: avcodec_find_encoder() of the codec
  QString video_encoder;
  QString audio_encoder;
  // Extra encoder options for avcodec_open2(), only those differing from the encoder's defaults
  QVector<QPair<QString, QString>> video_options;
  QVector<QPair<QString, QString>> audio_options;
  // movflags=+faststart when the muxer has it (MOV family): the index goes before the media data
  bool faststart = false;
  // Analyse the whole range first, then encode (AV_CODEC_FLAG_PASS1/PASS2). Only for COMPRESSION_TYPE_TARGETBR video
  // with an encoder whose descriptor has supports_two_pass (rendering/encoderoptions.h); ignored otherwise.
  // CBA: two-pass only in Average bitrate mode; libvpx/libaom also gain from it in quality mode
  bool two_pass = false;
};

struct VideoCodecParams {
  int pix_fmt;
  int threads;
};

class ExportThread : public QThread {
  Q_OBJECT
 public:
  ExportThread(Sequence* seq, const ExportParams& params, const VideoCodecParams& vparams, QObject* parent = nullptr);
  void run() override;

  // Pre-created GL fallback surface (created on GUI thread, passed through to
  // the internal RenderThread).  ExportThread does NOT take ownership.
  void setGlFallbackSurface(QOffscreenSurface* surface);

  const QString& GetError();

  bool WasInterrupted();
 signals:
  void ProgressChanged(int value, qint64 remaining_ms);
  // Two-pass exports only, as each pass starts: pass 1 (analysis) covers 0-50 % of ProgressChanged, pass 2 the rest
  void PassChanged(int pass, int passes);
 public slots:
  void Interrupt();

 private:
  bool Encode(AVFormatContext* ofmt_ctx, AVCodecContext* codec_ctx, AVFrame* frame, AVPacket* packet, AVStream* stream);
  bool SetupVideo();
  bool SetupAudio();
  bool SetupContainer();
  bool Export();
  void Cleanup();
  bool SetupPaletteGraph();
  bool EncodeVideoFrame(RenderThread* renderer, double timecode_secs);
  bool EncodePaletteFrames();
  bool FlushPaletteGraph();
  bool EncodeAudioFrames(long& file_audio_samples, double timecode_secs, int audio_cachers);
  bool EncodeAllFrames(RenderThread* renderer, long& file_audio_samples);
  bool RenderPass(long& file_audio_samples);
  bool TwoPassApplies();
  bool AnalysisPass();
  bool FlushSwrAudio(long& file_audio_samples);
  bool FlushEncoders();

  std::atomic<bool> interrupt_;

  // 0: one-pass export; two-pass: 1 while analysing (nothing written, no audio), then 2 for the real export
  int pass_{0};
  QByteArray pass1_stats_;  // stats_out of pass 1, concatenated: pass 2's stats_in (FFmpeg doesn't copy it)
  // Encoders with a stats file (libx264, libx265): its AVOption, and the folder holding it until the pass-2 encoder
  // is freed
  QString stats_key_;
  std::unique_ptr<QTemporaryDir> stats_dir_;
  QString stats_file_;

  Sequence* seq_;

  // params imported from dialogs
  ExportParams params_;
  VideoCodecParams vcodec_params_;

  AVFormatContext* fmt_ctx{nullptr};
  AVStream* video_stream{nullptr};
  const AVCodec* vcodec{nullptr};
  AVCodecContext* vcodec_ctx{nullptr};
  AVFrame* video_frame{nullptr};
  AVFrame* sws_frame{nullptr};
  SwsContext* sws_ctx{nullptr};
  // PAL8 only (swscale can't output it): buffer -> palettegen/paletteuse -> buffersink
  AVFilterGraph* palette_graph{nullptr};
  AVFilterContext* palette_src{nullptr};
  AVFilterContext* palette_sink{nullptr};
  bool unpremultiply_{false};  // the encoder's pixel format has alpha: straighten the premultiplied composite
  AVStream* audio_stream{nullptr};
  const AVCodec* acodec{nullptr};
  AVFrame* audio_frame{nullptr};
  AVFrame* swr_frame{nullptr};
  AVCodecContext* acodec_ctx{nullptr};
  AVPacket* video_pkt{nullptr};
  AVPacket* audio_pkt{nullptr};
  SwrContext* swr_ctx{nullptr};

  int aframe_bytes;
  int ret;
  char* c_filename{nullptr};

  QMutex mutex;
  QWaitCondition waitCond;
  bool render_complete_{false};
  bool container_opened_{false};  // avio_open succeeded: a file exists at params_.filename

  QString export_error;

  QOffscreenSurface* gl_fallback_surface_{nullptr};
 private slots:
  void wake();
};

// Engine side of an export, around ExportThread::start(). GUI thread only: the GL fallback surface and the
// autorecovery timer belong to it. UI work (pausing playback, moving the viewer's playhead) stays with the caller.
//
// prepare_export() puts the audio path in rendering mode at the export sample rate (which also stops autorecovery and
// freezes the viewer), moves seq's playhead to the export start and aligns the audio buffer with it, closes seq's open
// clips so the export reopens them, saves an autorecovery file if there are unsaved changes, and on the OpenGL backend
// hands the thread a GL fallback surface created here. Returns that surface (null on other backends): pass it to
// finish_export() once the thread has finished.
QOffscreenSurface* prepare_export(ExportThread* thread, Sequence* seq, const ExportParams& params);

// Leaves rendering mode (restarting autorecovery), clears the audio buffer and deletes gl_surface.
void finish_export(QOffscreenSurface* gl_surface);

#endif  // EXPORTTHREAD_H
