// Headless export: loads a hand-written .ove with LoadThread (one video clip and one audio clip, of a Y4M and a WAV the
// test writes), exports it to .mp4 through prepare_export() + ExportThread exactly as the editor's export dialog does,
// and decodes the result with FFmpeg.
//
// Why not tests/fixtures/t1_yuv_input.mp4: it holds a single frame lasting 33333 us, which Amber's frame math
// (Footage::get_length_in_frames(), floor(0.99999) at 30 fps) counts as 0 frames, so a clip of it is never active.
//
// Proof that the engine renders on its own: this target links amber-engine, Qt Gui and Qt Test only -- no QtWidgets,
// no UI sources, no stub file, a QGuiApplication, HeadlessAppContext, and no sound device (init_audio() never runs).

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavfilter/avfilter.h>
#include <libavformat/avformat.h>
#include <libavutil/pixdesc.h>
#include <libswscale/swscale.h>
}

#include <QElapsedTimer>
#include <QFile>
#include <QGuiApplication>
#include <QPointer>
#include <QScopeGuard>
#include <QSurfaceFormat>
#include <QTemporaryDir>
#include <QTest>
#include <QtEndian>
#include <QtMath>

#include "core/headlessappcontext.h"
#include "effects/effect.h"
#include "engine/clip.h"
#include "engine/sequence.h"
#include "global/config.h"
#include "global/projectio.h"
#include "project/footage.h"
#include "project/loadthread.h"
#include "project/media.h"
#include "project/previewgenerator.h"
#include "project/projectmodel.h"
#include "rendering/exportthread.h"
#include "rendering/renderfunctions.h"
#include "rendering/renderthread.h"

// External entry point from effectloaders.cpp (no header export), as in test_render_harness.cpp
extern void load_internal_effects();

namespace {

constexpr int kSize = 64;      // source and sequence are 64x64
constexpr double kFps = 30.0;  // source and sequence frame rate
constexpr long kClipOut = 10;  // both clips span frames [0, 10)
// Source frames: two more than the clip uses, as a duration of exactly N frames can come out a hair short in
// AV_TIME_BASE and floor to N - 1 frames (see the header comment)
constexpr int kSourceFrames = 12;
constexpr int kAudioRate = 48000;

// kSourceFrames frames of an 8x8-pixel black/white checkerboard (limited range), YUV 4:2:0, as a Y4M stream
QByteArray checkerboard_y4m() {
  QByteArray luma(kSize * kSize, Qt::Uninitialized);
  for (int y = 0; y < kSize; y++) {
    for (int x = 0; x < kSize; x++) {
      luma[y * kSize + x] = char(((x / 8 + y / 8) % 2 == 0) ? 235 : 16);
    }
  }
  const QByteArray chroma(kSize * kSize / 2, char(128));  // U and V planes, 32x32 each
  QByteArray y4m = QByteArray("YUV4MPEG2 W") + QByteArray::number(kSize) + " H" + QByteArray::number(kSize) + " F" +
                   QByteArray::number(int(kFps)) + ":1 Ip A1:1 C420jpeg\n";
  for (int i = 0; i < kSourceFrames; i++) {
    y4m += "FRAME\n";
    y4m += luma;
    y4m += chroma;
  }
  return y4m;
}

// kSourceFrames frames of a uniform mid-grey picture, half the source size: a stand-in proxy of checkerboard_y4m()
QByteArray grey_proxy_y4m() {
  constexpr int s = kSize / 2;
  const QByteArray luma(s * s, char(128));
  const QByteArray chroma(s * s / 2, char(128));
  QByteArray y4m = QByteArray("YUV4MPEG2 W") + QByteArray::number(s) + " H" + QByteArray::number(s) + " F" +
                   QByteArray::number(int(kFps)) + ":1 Ip A1:1 C420jpeg\n";
  for (int i = 0; i < kSourceFrames; i++) {
    y4m += "FRAME\n";
    y4m += luma;
    y4m += chroma;
  }
  return y4m;
}

// 1 s of a stereo sine (by default 440 Hz at half scale), S16 PCM. With ramp, the amplitude rises linearly from 0 so
// the signal isn't symmetric in time.
QByteArray sine_wav(double freq = 440.0, double amplitude = 0.5, bool ramp = false) {
  const int frames = kAudioRate;
  QByteArray pcm(frames * 4, Qt::Uninitialized);
  for (int i = 0; i < frames; i++) {
    const double a = ramp ? amplitude * i / frames : amplitude;
    qint16 v = qint16(qRound(32768.0 * a * qSin(2.0 * M_PI * freq * i / kAudioRate)));
    qToLittleEndian(v, pcm.data() + i * 4);
    qToLittleEndian(v, pcm.data() + i * 4 + 2);
  }
  QByteArray wav;
  auto u32 = [&wav](quint32 x) {
    char b[4];
    qToLittleEndian(x, b);
    wav.append(b, 4);
  };
  auto u16 = [&wav](quint16 x) {
    char b[2];
    qToLittleEndian(x, b);
    wav.append(b, 2);
  };
  wav.append("RIFF");
  u32(36 + pcm.size());
  wav.append("WAVEfmt ");
  u32(16);
  u16(1);  // PCM
  u16(2);  // channels
  u32(kAudioRate);
  u32(kAudioRate * 4);  // byte rate
  u16(4);               // block align
  u16(16);              // bits per sample
  wav.append("data");
  u32(pcm.size());
  wav.append(pcm);
  return wav;
}

// Same layout Project::save_project() writes: footage 1 (video) and 2 (audio) in folder 1, the open sequence at the
// root. Loop mode 2 (clamp) holds the fixture's single frame for the clip's whole length. A non-empty proxy marks the
// video footage as having that finished proxy file.
QString fixture(const QString& url, const QString& video, const QString& audio, const QString& proxy = QString()) {
  const QString proxy_attrs =
      proxy.isEmpty() ? QString() : QString(R"( proxy="1" proxypath="%1")").arg(proxy.toHtmlEscaped());
  return QString(R"(<?xml version="1.0" encoding="UTF-8"?>
<project>
    <version>%1</version>
    <url>%2</url>
    <folders>
        <folder id="1" name="Bin" parent="0"/>
    </folders>
    <media>
        <footage id="1" folder="1" name="video" url="%3"%9/>
        <footage id="2" folder="1" name="audio" url="%4"/>
    </media>
    <sequences>
        <sequence id="1" folder="0" name="Export" width="%5" height="%5" framerate="%6" afreq="%7" alayout="3"
                  open="1">
            <clip id="0" enabled="1" name="video" clipin="0" in="0" out="%8" track="-1" r="128" g="128" b="192"
                  autoscale="1" type="0" media="1" stream="0" speed="1" maintainpitch="0" reverse="0" loop="2">
                <linked></linked>
            </clip>
            <clip id="1" enabled="1" name="audio" clipin="0" in="0" out="%8" track="0" r="128" g="192" b="128"
                  autoscale="1" type="0" media="2" stream="0" speed="1" maintainpitch="0" reverse="0">
                <linked></linked>
            </clip>
        </sequence>
    </sequences>
</project>
)")
      .arg(QString::number(amber::kSaveVersion), url.toHtmlEscaped(), video.toHtmlEscaped(), audio.toHtmlEscaped(),
           QString::number(kSize), QString::number(kFps, 'f', 10), QString::number(kAudioRate),
           QString::number(kClipOut), proxy_attrs);
}

// Three sequences over two WAVs (footage 1 and 2): Inner holds both on audio tracks 0 and 1, Nested holds one clip of
// Inner, Flat holds the same two WAV clips directly
QString nested_fixture(const QString& url, const QString& wav_a, const QString& wav_b) {
  return QString(R"(<?xml version="1.0" encoding="UTF-8"?>
<project>
    <version>%1</version>
    <url>%2</url>
    <folders>
        <folder id="1" name="Bin" parent="0"/>
    </folders>
    <media>
        <footage id="1" folder="1" name="a" url="%3"/>
        <footage id="2" folder="1" name="b" url="%4"/>
    </media>
    <sequences>
        <sequence id="1" folder="0" name="Inner" width="%5" height="%5" framerate="%6" afreq="%7" alayout="3">
            <clip id="0" enabled="1" name="a" clipin="0" in="0" out="%8" track="0" r="128" g="192" b="128"
                  autoscale="1" type="0" media="1" stream="0" speed="1" maintainpitch="0" reverse="0">
                <linked></linked>
            </clip>
            <clip id="1" enabled="1" name="b" clipin="0" in="0" out="%8" track="1" r="128" g="192" b="128"
                  autoscale="1" type="0" media="2" stream="0" speed="1" maintainpitch="0" reverse="0">
                <linked></linked>
            </clip>
        </sequence>
        <sequence id="2" folder="0" name="Nested" width="%5" height="%5" framerate="%6" afreq="%7" alayout="3">
            <clip id="0" enabled="1" name="Inner" clipin="0" in="0" out="%8" track="0" r="192" g="128" b="128"
                  autoscale="1" type="%9" sequence="1" speed="1" maintainpitch="0" reverse="0">
                <linked></linked>
            </clip>
        </sequence>
        <sequence id="3" folder="0" name="Flat" width="%5" height="%5" framerate="%6" afreq="%7" alayout="3"
                  open="1">
            <clip id="0" enabled="1" name="a" clipin="0" in="0" out="%8" track="0" r="128" g="192" b="128"
                  autoscale="1" type="0" media="1" stream="0" speed="1" maintainpitch="0" reverse="0">
                <linked></linked>
            </clip>
            <clip id="1" enabled="1" name="b" clipin="0" in="0" out="%8" track="1" r="128" g="192" b="128"
                  autoscale="1" type="0" media="2" stream="0" speed="1" maintainpitch="0" reverse="0">
                <linked></linked>
            </clip>
        </sequence>
    </sequences>
</project>
)")
      .arg(QString::number(amber::kSaveVersion), url.toHtmlEscaped(), wav_a.toHtmlEscaped(), wav_b.toHtmlEscaped(),
           QString::number(kSize), QString::number(kFps, 'f', 10), QString::number(kAudioRate),
           QString::number(kClipOut), QString::number(MEDIA_TYPE_SEQUENCE));
}

// Two sequences over one WAV: Forward plays it, Reversed plays it with the reverse flag (the test moves Forward's
// in point to the span Reversed reads)
QString reversed_fixture(const QString& url, const QString& wav) {
  return QString(R"(<?xml version="1.0" encoding="UTF-8"?>
<project>
    <version>%1</version>
    <url>%2</url>
    <folders>
        <folder id="1" name="Bin" parent="0"/>
    </folders>
    <media>
        <footage id="1" folder="1" name="ramp" url="%3"/>
    </media>
    <sequences>
        <sequence id="1" folder="0" name="Forward" width="%4" height="%4" framerate="%5" afreq="%6" alayout="3">
            <clip id="0" enabled="1" name="ramp" clipin="0" in="0" out="%7" track="0" r="128" g="192" b="128"
                  autoscale="1" type="0" media="1" stream="0" speed="1" maintainpitch="0" reverse="0">
                <linked></linked>
            </clip>
        </sequence>
        <sequence id="2" folder="0" name="Reversed" width="%4" height="%4" framerate="%5" afreq="%6" alayout="3"
                  open="1">
            <clip id="0" enabled="1" name="ramp" clipin="0" in="0" out="%7" track="0" r="128" g="192" b="128"
                  autoscale="1" type="0" media="1" stream="0" speed="1" maintainpitch="0" reverse="1">
                <linked></linked>
            </clip>
        </sequence>
    </sequences>
</project>
)")
      .arg(QString::number(amber::kSaveVersion), url.toHtmlEscaped(), wav.toHtmlEscaped(), QString::number(kSize),
           QString::number(kFps, 'f', 10), QString::number(kAudioRate), QString::number(kClipOut));
}

// RGBA8 copy of a decoded video frame (BT.601 through swscale's defaults, same for source and output)
QByteArray to_rgba(const AVFrame* f) {
  QByteArray out(f->width * f->height * 4, Qt::Uninitialized);
  SwsContext* sws = sws_getContext(f->width, f->height, AVPixelFormat(f->format), f->width, f->height, AV_PIX_FMT_RGBA,
                                   SWS_BILINEAR, nullptr, nullptr, nullptr);
  uint8_t* dst[4] = {reinterpret_cast<uint8_t*>(out.data()), nullptr, nullptr, nullptr};
  int dst_linesize[4] = {f->width * 4, 0, 0, 0};
  sws_scale(sws, f->data, f->linesize, 0, f->height, dst, dst_linesize);
  sws_freeContext(sws);
  return out;
}

struct Decoded {
  bool ok = false;
  bool has_video = false;
  bool has_audio = false;
  int video_frames = 0;
  double video_fps = 0;  // stream avg_frame_rate
  int width = 0;
  int height = 0;
  QByteArray first_rgba;
  QVector<QByteArray> rgba;  // every video frame, when decode_file() is asked to keep them
  int audio_rate = 0;
  int audio_channels = 0;
  long audio_samples = 0;  // per channel
  double audio_peak = 0;   // max |sample|, 0..1
  // Channel 0 samples, -1..1
  QVector<float> audio_left;
};

void read_audio_frame(const AVFrame* f, Decoded& d) {
  const int ch = f->ch_layout.nb_channels;
  for (int c = 0; c < ch; c++) {
    for (int i = 0; i < f->nb_samples; i++) {
      double v = 0;
      switch (f->format) {
        case AV_SAMPLE_FMT_FLTP:
          v = reinterpret_cast<const float*>(f->extended_data[c])[i];
          break;
        case AV_SAMPLE_FMT_FLT:
          v = reinterpret_cast<const float*>(f->extended_data[0])[i * ch + c];
          break;
        case AV_SAMPLE_FMT_S16P:
          v = reinterpret_cast<const qint16*>(f->extended_data[c])[i] / 32768.0;
          break;
        case AV_SAMPLE_FMT_S16:
          v = reinterpret_cast<const qint16*>(f->extended_data[0])[i * ch + c] / 32768.0;
          break;
        default:
          return;  // unexpected format: peak stays 0 and the test fails on it
      }
      d.audio_peak = qMax(d.audio_peak, qAbs(v));
      if (c == 0) d.audio_left.append(float(v));
    }
  }
  d.audio_samples += f->nb_samples;
}

// Decode every frame of the first video and first audio stream of a file
Decoded decode_file(const QString& path, bool keep_frames = false) {
  Decoded d;
  AVFormatContext* fmt = nullptr;
  QByteArray p = path.toUtf8();
  if (avformat_open_input(&fmt, p.constData(), nullptr, nullptr) < 0) return d;
  if (avformat_find_stream_info(fmt, nullptr) < 0) {
    avformat_close_input(&fmt);
    return d;
  }

  int vi = av_find_best_stream(fmt, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
  int ai = av_find_best_stream(fmt, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
  AVCodecContext* vctx = nullptr;
  AVCodecContext* actx = nullptr;
  auto open_dec = [fmt](int index) -> AVCodecContext* {
    const AVCodec* codec = avcodec_find_decoder(fmt->streams[index]->codecpar->codec_id);
    if (codec == nullptr) return nullptr;
    AVCodecContext* ctx = avcodec_alloc_context3(codec);
    avcodec_parameters_to_context(ctx, fmt->streams[index]->codecpar);
    if (avcodec_open2(ctx, codec, nullptr) < 0) avcodec_free_context(&ctx);
    return ctx;
  };
  if (vi >= 0) vctx = open_dec(vi);
  if (ai >= 0) actx = open_dec(ai);
  d.has_video = vctx != nullptr;
  if (vctx != nullptr) d.video_fps = av_q2d(fmt->streams[vi]->avg_frame_rate);
  d.has_audio = actx != nullptr;
  if (actx != nullptr) {
    d.audio_rate = actx->sample_rate;
    d.audio_channels = actx->ch_layout.nb_channels;
  }

  AVPacket* pkt = av_packet_alloc();
  AVFrame* frame = av_frame_alloc();
  auto drain = [&](AVCodecContext* ctx, bool video) {
    while (avcodec_receive_frame(ctx, frame) == 0) {
      if (video) {
        if (d.video_frames == 0) {
          d.width = frame->width;
          d.height = frame->height;
          d.first_rgba = to_rgba(frame);
        }
        if (keep_frames) d.rgba.append(to_rgba(frame));
        d.video_frames++;
      } else {
        read_audio_frame(frame, d);
      }
      av_frame_unref(frame);
    }
  };
  while (av_read_frame(fmt, pkt) >= 0) {
    if (vctx != nullptr && pkt->stream_index == vi) {
      avcodec_send_packet(vctx, pkt);
      drain(vctx, true);
    } else if (actx != nullptr && pkt->stream_index == ai) {
      avcodec_send_packet(actx, pkt);
      drain(actx, false);
    }
    av_packet_unref(pkt);
  }
  if (vctx != nullptr) {
    avcodec_send_packet(vctx, nullptr);
    drain(vctx, true);
  }
  if (actx != nullptr) {
    avcodec_send_packet(actx, nullptr);
    drain(actx, false);
  }

  av_frame_free(&frame);
  av_packet_free(&pkt);
  avcodec_free_context(&vctx);
  avcodec_free_context(&actx);
  avformat_close_input(&fmt);
  d.ok = true;
  return d;
}

// kSize x kSize sequence holding one white Solid (default Transform, centred, full frame) at 50 % Transform opacity
// over nothing, on frames [0, kClipOut): the composite is half-transparent white
SequencePtr half_white_sequence() {
  SequencePtr seq = std::make_shared<Sequence>();
  seq->name = "Alpha";
  seq->width = kSize;
  seq->height = kSize;
  seq->frame_rate = kFps;
  seq->audio_frequency = kAudioRate;
  ClipPtr c = std::make_shared<Clip>(seq.get());
  c->set_media(nullptr, 0);
  c->set_timeline_in(0);
  c->set_timeline_out(kClipOut);
  c->set_clip_in(0);
  c->set_track(-1);
  c->set_name("Solid");
  EffectPtr transform = Effect::Create(c.get(), Effect::GetInternalMeta(EFFECT_INTERNAL_TRANSFORM, EFFECT_TYPE_EFFECT));
  transform->row(5)->Field(0)->SetValueAt(0.0, QVariant(50.0));  // Opacity
  c->effects.append(transform);
  EffectPtr solid = Effect::Create(c.get(), Effect::GetInternalMeta(EFFECT_INTERNAL_SOLID, EFFECT_TYPE_EFFECT));
  solid->row(2)->Field(0)->SetValueAt(0.0, QVariant(QColor(Qt::white)));  // Color
  c->effects.append(solid);
  seq->clips.append(c);
  return seq;
}

// kSize x kSize sequence of three Solids in a row, each kPaletteClipFrames long and scaled to 50 % (the centre half of
// the frame) over nothing: an opaque colour off the rgb8 3-3-2 grid in the centre, fully transparent around it
constexpr int kPaletteClipFrames = 2;
const QColor kPaletteColours[] = {QColor(200, 120, 40), QColor(40, 200, 120), QColor(120, 40, 200)};

SequencePtr palette_sequence() {
  SequencePtr seq = std::make_shared<Sequence>();
  seq->name = "Palette";
  seq->width = kSize;
  seq->height = kSize;
  seq->frame_rate = kFps;
  seq->audio_frequency = kAudioRate;
  long in = 0;
  for (const QColor& colour : kPaletteColours) {
    ClipPtr c = std::make_shared<Clip>(seq.get());
    c->set_media(nullptr, 0);
    c->set_timeline_in(in);
    c->set_timeline_out(in + kPaletteClipFrames);
    c->set_clip_in(0);
    c->set_track(-1);
    c->set_name("Solid");
    EffectPtr transform =
        Effect::Create(c.get(), Effect::GetInternalMeta(EFFECT_INTERNAL_TRANSFORM, EFFECT_TYPE_EFFECT));
    transform->row(1)->Field(0)->SetValueAt(0.0, QVariant(50.0));  // Scale (uniform)
    c->effects.append(transform);
    EffectPtr solid = Effect::Create(c.get(), Effect::GetInternalMeta(EFFECT_INTERNAL_SOLID, EFFECT_TYPE_EFFECT));
    solid->row(2)->Field(0)->SetValueAt(0.0, QVariant(colour));  // Color
    c->effects.append(solid);
    seq->clips.append(c);
    in += kPaletteClipFrames;
  }
  return seq;
}

}  // namespace

class TestHeadlessRender : public QObject {
  Q_OBJECT

 private:
  HeadlessAppContext ctx_;
  ProjectIO* pio_ = nullptr;
  QTemporaryDir dir_;

  // Export the way ExportDialog::launch_export_thread() / export_thread_finished() do it. Returns the error, empty on
  // success.
  QString export_sequence(Sequence* seq, const ExportParams& params, AVPixelFormat pix_fmt = AV_PIX_FMT_YUV420P) {
    VideoCodecParams vparams;
    vparams.pix_fmt = pix_fmt;
    vparams.threads = 0;

    auto* et = new ExportThread(seq, params, vparams);
    bool finished = false;
    connect(et, &QThread::finished, this, [&finished]() { finished = true; });
    QOffscreenSurface* surface = prepare_export(et, seq, params);
    if (surface == nullptr) return "no GL fallback surface";
    et->start();
    // ExportThread's render and audio wake-ups are queued to this thread: qWaitFor keeps the event loop running
    if (!QTest::qWaitFor([&finished]() { return finished; }, 60000)) return "export timed out";
    et->wait();
    finish_export(surface);
    QString error = et->GetError();
    const bool interrupted = et->WasInterrupted();
    delete et;
    if (error.isEmpty() && interrupted) error = "export interrupted";
    return error;
  }

  // Load, as the active sequence, a checkerboard original whose footage has a finished uniform grey proxy, plus a tone
  // clip, and wait for the probes. Callers return on QTest::currentTestFailed().
  void load_proxied_project(const QString& name) {
    const QString video = dir_.filePath(name + "_checkerboard.y4m");
    {
      QFile f(video);
      QVERIFY(f.open(QFile::WriteOnly | QFile::Truncate));
      f.write(checkerboard_y4m());
    }
    const QString proxy = dir_.filePath(name + "_checkerboard.proxy.y4m");
    {
      QFile f(proxy);
      QVERIFY(f.open(QFile::WriteOnly | QFile::Truncate));
      f.write(grey_proxy_y4m());
    }
    const QString audio = dir_.filePath(name + "_tone.wav");
    {
      QFile f(audio);
      QVERIFY(f.open(QFile::WriteOnly | QFile::Truncate));
      f.write(sine_wav());
    }
    const QString project = dir_.filePath(name + ".ove");
    {
      QFile f(project);
      QVERIFY(f.open(QFile::WriteOnly | QFile::Truncate));
      f.write(fixture(project, video, audio, proxy).toUtf8());
    }

    bool succeeded = false;
    auto* lt = new LoadThread(project, false);
    QPointer<LoadThread> lt_guard(lt);
    connect(lt, &LoadThread::success, this, [&succeeded]() { succeeded = true; });
    lt->start();
    QTRY_VERIFY_WITH_TIMEOUT(lt_guard.isNull(), 10000);
    QVERIFY(succeeded);

    SequencePtr seq = amber::ActiveSequence;
    QVERIFY(seq != nullptr);
    Footage* vf = nullptr;
    for (const auto& c : seq->clips) {
      QVERIFY(c->media() != nullptr);
      Footage* f = c->media()->to_footage();
      QPointer<PreviewGenerator> gen(f->preview_gen.load());
      QTRY_VERIFY_WITH_TIMEOUT(gen.isNull(), 10000);
      QVERIFY(f->ready);
      if (c->track() < 0) vf = f;
    }
    QVERIFY(vf != nullptr);
    QVERIFY(vf->proxy);
    QCOMPARE(vf->proxy_path, proxy);
  }

 private slots:
  void initTestCase() {
    QVERIFY(dir_.isValid());
    // Probe thumbnails, shader cache and anything else under the standard paths stay in the temp dir (test mode
    // would write to ~/.qttest)
    qputenv("XDG_DATA_HOME", dir_.filePath("data").toUtf8());
    qputenv("XDG_CACHE_HOME", dir_.filePath("cache").toUtf8());
    qputenv("XDG_CONFIG_HOME", dir_.filePath("config").toUtf8());
    amber::app_ctx = &ctx_;
    pio_ = new ProjectIO();
    amber::project_io = pio_;
    // What main() resolves the backend to on Linux; the render thread then uses the GL fallback surface that
    // prepare_export() creates on this thread
    amber::CurrentRuntimeConfig.rhi_backend = RhiBackend::OpenGL;
    amber::CurrentConfig.hardware_decoding = false;
    if (effects.empty()) load_internal_effects();
  }

  void cleanupTestCase() {
    amber::ActiveSequence = nullptr;
    amber::project_model.clear();
    amber::project_io = nullptr;
    delete pio_;
    amber::app_ctx = nullptr;
  }

  void exportsLoadedProject() {
    QElapsedTimer timer;
    timer.start();

    // --- Fixture: Y4M + WAV + .ove
    const QString video = dir_.filePath("checkerboard.y4m");
    {
      QFile f(video);
      QVERIFY(f.open(QFile::WriteOnly | QFile::Truncate));
      f.write(checkerboard_y4m());
    }
    const QString audio = dir_.filePath("tone.wav");
    {
      QFile f(audio);
      QVERIFY(f.open(QFile::WriteOnly | QFile::Truncate));
      f.write(sine_wav());
    }
    const QString project = dir_.filePath("export.ove");
    {
      QFile f(project);
      QVERIFY(f.open(QFile::WriteOnly | QFile::Truncate));
      f.write(fixture(project, video, audio).toUtf8());
    }

    // --- Load (worker thread + this event loop, as the app does), then wait for both media probes
    bool succeeded = false;
    auto* lt = new LoadThread(project, false);
    QPointer<LoadThread> lt_guard(lt);
    connect(lt, &LoadThread::success, this, [&succeeded]() { succeeded = true; });
    lt->start();
    QTRY_VERIFY_WITH_TIMEOUT(lt_guard.isNull(), 10000);
    QVERIFY(succeeded);

    SequencePtr seq = amber::ActiveSequence;
    QVERIFY(seq != nullptr);
    QCOMPARE(seq->clips.size(), 2);
    for (const auto& c : seq->clips) {
      QVERIFY(c->media() != nullptr);
      Footage* f = c->media()->to_footage();
      QPointer<PreviewGenerator> gen(f->preview_gen.load());
      QTRY_VERIFY_WITH_TIMEOUT(gen.isNull(), 10000);
      QVERIFY(f->ready);
      QVERIFY(!f->invalid);
      // The clip must be active on every frame it spans
      QVERIFY2(c->media_length() >= kClipOut, qPrintable(QString("media length %1").arg(c->media_length())));
    }
    QCOMPARE(seq->getEndFrame(), kClipOut);

    // --- Export
    ExportParams params;
    params.filename = dir_.filePath("out.mp4");
    params.video_enabled = true;
    params.video_codec = AV_CODEC_ID_MPEG4;  // native libavcodec encoder, in every FFmpeg build
    params.video_width = kSize;
    params.video_height = kSize;
    params.video_compression_type = COMPRESSION_TYPE_CBR;
    params.video_bitrate = 2.0;  // Mbps
    params.audio_enabled = true;
    params.audio_codec = AV_CODEC_ID_AAC;  // native too
    params.audio_sampling_rate = kAudioRate;
    params.audio_bitrate = 128;  // kbps
    params.start_frame = 0;
    params.end_frame = seq->getEndFrame();
    const QString error = export_sequence(seq.get(), params);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QVERIFY(QFileInfo::exists(params.filename));

    // --- Decode the export and the source
    Decoded out = decode_file(params.filename);
    Decoded src = decode_file(video);
    QVERIFY(out.ok);
    QVERIFY(src.ok);
    QVERIFY(out.has_video);
    QVERIFY(src.video_frames >= 1);

    // end_frame is exclusive (one frame past the last clip): a 10-frame sequence gives 10 frames, no extra empty one
    const long expected_frames = params.end_frame - params.start_frame;
    QCOMPARE(long(out.video_frames), expected_frames);
    // The export takes the sequence's frame rate (ExportParams has no frame rate of its own)
    QVERIFY2(qAbs(out.video_fps - seq->frame_rate) < 0.001, qPrintable(QString("output fps %1").arg(out.video_fps)));
    QCOMPARE(out.width, kSize);
    QCOMPARE(out.height, kSize);

    // First frame vs source first frame (8x8 checkerboard), through YUV->RGB on the GPU, RGB->YUV420P
    // and MPEG-4 compression: compare the mean per-channel difference, and every block centre must keep its colour
    QCOMPARE(out.first_rgba.size(), src.first_rgba.size());
    QVERIFY(uchar(out.first_rgba[(4 * kSize + 4) * 4]) > 200);  // block (0,0) white
    QVERIFY(uchar(out.first_rgba[(4 * kSize + 12) * 4]) < 55);  // block (1,0) black
    double sum = 0;
    int max_centre_delta = 0;
    for (int i = 0; i < out.first_rgba.size(); i += 4) {
      for (int ch = 0; ch < 3; ch++) {
        sum += qAbs(int(uchar(out.first_rgba[i + ch])) - int(uchar(src.first_rgba[i + ch])));
      }
    }
    for (int by = 0; by < 8; by++) {
      for (int bx = 0; bx < 8; bx++) {
        const int off = ((by * 8 + 4) * kSize + bx * 8 + 4) * 4;
        for (int ch = 0; ch < 3; ch++) {
          max_centre_delta =
              qMax(max_centre_delta, qAbs(int(uchar(out.first_rgba[off + ch])) - int(uchar(src.first_rgba[off + ch]))));
        }
      }
    }
    const double mean_delta = sum / (kSize * kSize * 3);
    qInfo("first frame: mean |delta| = %.2f, max |delta| at block centres = %d", mean_delta, max_centre_delta);
    QVERIFY2(mean_delta < 16.0, qPrintable(QString("mean delta %1").arg(mean_delta)));
    QVERIFY2(max_centre_delta < 48, qPrintable(QString("max centre delta %1").arg(max_centre_delta)));

    // Audio: the clip's sine made it into the file although no sound device was ever opened
    QVERIFY(out.has_audio);
    QCOMPARE(out.audio_rate, kAudioRate);
    QCOMPARE(out.audio_channels, 2);
    // EncodeAudioFrames() encodes up to the end of the last exported frame, in whole encoder frames (1024 samples
    // for AAC): as long as the video, end_frame / fps seconds, and less than two encoder frames longer
    const long expected_samples = qRound64((params.end_frame - params.start_frame) / kFps * kAudioRate);
    qInfo("audio: %ld samples per channel (expected >= %ld), peak %.3f", out.audio_samples, expected_samples,
          out.audio_peak);
    QVERIFY(out.audio_samples >= expected_samples);
    QVERIFY(out.audio_samples < expected_samples + 2 * 1024);
    QVERIFY2(out.audio_peak > 0.25, qPrintable(QString("audio peak %1").arg(out.audio_peak)));

    qInfo("load + export + decode: %lld ms", timer.elapsed());
  }

  // Export decodes the original media even when the footage has a finished proxy (the proxy is a preview stand-in):
  // the proxy here is uniform grey, so a checkerboard in the output proves the original was read
  void exportIgnoresProxy() {
    load_proxied_project("proxied");
    if (QTest::currentTestFailed()) return;
    SequencePtr seq = amber::ActiveSequence;

    ExportParams params;
    params.filename = dir_.filePath("proxied_out.mp4");
    params.video_enabled = true;
    params.video_codec = AV_CODEC_ID_MPEG4;
    params.video_width = kSize;
    params.video_height = kSize;
    params.video_compression_type = COMPRESSION_TYPE_CBR;
    params.video_bitrate = 2.0;
    params.audio_enabled = false;
    params.start_frame = 0;
    params.end_frame = seq->getEndFrame();
    const QString error = export_sequence(seq.get(), params);
    QVERIFY2(error.isEmpty(), qPrintable(error));

    Decoded out = decode_file(params.filename);
    QVERIFY(out.ok);
    QVERIFY(out.has_video);
    QCOMPARE(out.width, kSize);
    const int white = uchar(out.first_rgba[(4 * kSize + 4) * 4]);
    const int black = uchar(out.first_rgba[(4 * kSize + 12) * 4]);
    qInfo("exported block (0,0) = %d, block (1,0) = %d (grey proxy would give ~128 for both)", white, black);
    QVERIFY(white > 200);
    QVERIFY(black < 55);

    // Export leaves every clip closed, so the viewer reopens them outside export mode, on the proxy
    for (const auto& c : seq->clips) QVERIFY(!c->IsOpen());
  }

  // The viewer path (Save Frame renders the viewer's clips) follows Use Proxies live: the grey proxy while on, the
  // checkerboard original once toggled off, the open clip reopening on the other file without being closed in between
  void viewerFollowsUseProxies() {
    const bool saved = amber::CurrentConfig.use_proxies;
    const auto restore = qScopeGuard([saved] { amber::CurrentConfig.use_proxies = saved; });
    amber::CurrentConfig.use_proxies = true;
    load_proxied_project("toggled");
    if (QTest::currentTestFailed()) return;
    SequencePtr seq = amber::ActiveSequence;
    Clip* video = nullptr;
    for (const auto& c : seq->clips) {
      if (c->track() < 0) video = c.get();
    }
    QVERIFY(video != nullptr);

    QOffscreenSurface* surface = QRhiGles2InitParams::newFallbackSurface();
    auto* rt = new RenderThread();
    rt->setGlFallbackSurface(surface);
    rt->start();
    // Two pixels in horizontally adjacent checkerboard blocks: equal on the grey proxy, black and white on the
    // original (in either order, whatever the frame's vertical orientation)
    auto save_frame = [&](const QString& file, int& a, int& b) {
      const QString fn = dir_.filePath(file);
      QFile::remove(fn);
      rt->start_render(seq.get(), 1, fn);  // re-queued until the clip's texture is ready
      QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(fn), 10000);
      QImage img(fn);
      QCOMPARE(img.size(), QSize(kSize, kSize));
      a = qGray(img.pixel(4, 4));
      b = qGray(img.pixel(12, 4));
    };
    int a = 0, b = 0;
    save_frame("proxies_on.png", a, b);
    qInfo("proxies on: %d %d (grey proxy: ~128 for both)", a, b);
    const bool on_ok = qAbs(a - 128) <= 16 && qAbs(b - 128) <= 16;
    const bool opened_on_proxy = video->IsOpen() && video->OpenedWithProxies();

    amber::CurrentConfig.use_proxies = false;
    save_frame("proxies_off.png", a, b);
    qInfo("proxies off: %d %d (checkerboard: one white, one black)", a, b);
    const bool off_ok = qAbs(a - b) > 180;
    const bool reopened = video->IsOpen() && !video->OpenedWithProxies();

    close_active_clips(seq.get());  // before the render thread drains the deferred RHI deletions on shutdown
    rt->cancel();
    delete rt;
    delete surface;
    QVERIFY2(on_ok, "proxies on did not render the grey proxy");
    QVERIFY(opened_on_proxy);
    QVERIFY2(off_ok, "proxies off did not render the checkerboard original");
    QVERIFY(reopened);
  }

  // A nested sequence's audio must export identically to the same clips placed directly in a sequence: the outer
  // mix pulls the nested clip's inner streams at the same timecode
  void nestedAudioMatchesFlat() {
    // --- Fixture: two sines a quarter scale each (the mix stays below full scale) + .ove
    const QString wav_a = dir_.filePath("tone_a.wav");
    {
      QFile f(wav_a);
      QVERIFY(f.open(QFile::WriteOnly | QFile::Truncate));
      f.write(sine_wav(330.0, 0.25));
    }
    const QString wav_b = dir_.filePath("tone_b.wav");
    {
      QFile f(wav_b);
      QVERIFY(f.open(QFile::WriteOnly | QFile::Truncate));
      f.write(sine_wav(1000.0, 0.25));
    }
    const QString project = dir_.filePath("nested.ove");
    {
      QFile f(project);
      QVERIFY(f.open(QFile::WriteOnly | QFile::Truncate));
      f.write(nested_fixture(project, wav_a, wav_b).toUtf8());
    }

    // --- Load, find the sequences, wait for both media probes
    bool succeeded = false;
    auto* lt = new LoadThread(project, false);
    QPointer<LoadThread> lt_guard(lt);
    connect(lt, &LoadThread::success, this, [&succeeded]() { succeeded = true; });
    lt->start();
    QTRY_VERIFY_WITH_TIMEOUT(lt_guard.isNull(), 10000);
    QVERIFY(succeeded);

    auto find_sequence = [](const QString& name) -> SequencePtr {
      for (int i = 0; i < amber::project_model.childCount(); i++) {
        Media* m = amber::project_model.child(i);
        if (m->get_type() == MEDIA_TYPE_SEQUENCE && m->to_sequence()->name == name) return m->to_sequence();
      }
      return nullptr;
    };
    SequencePtr nested_seq = find_sequence("Nested");
    SequencePtr flat_seq = find_sequence("Flat");
    QVERIFY(nested_seq != nullptr);
    QVERIFY(flat_seq != nullptr);
    QCOMPARE(nested_seq->clips.size(), 1);
    QVERIFY(nested_seq->clips.at(0)->media() != nullptr);
    QCOMPARE(nested_seq->clips.at(0)->media()->get_type(), int(MEDIA_TYPE_SEQUENCE));
    QCOMPARE(flat_seq->clips.size(), 2);
    for (const auto& c : flat_seq->clips) {
      QVERIFY(c->media() != nullptr);
      Footage* f = c->media()->to_footage();
      QPointer<PreviewGenerator> gen(f->preview_gen.load());
      QTRY_VERIFY_WITH_TIMEOUT(gen.isNull(), 10000);
      QVERIFY(f->ready);
      QVERIFY(!f->invalid);
      QVERIFY2(c->media_length() >= kClipOut, qPrintable(QString("media length %1").arg(c->media_length())));
    }
    QCOMPARE(nested_seq->getEndFrame(), kClipOut);
    QCOMPARE(flat_seq->getEndFrame(), kClipOut);

    // --- Export both, audio only, lossless
    auto export_audio = [this](Sequence* seq, const QString& name, QString& path) -> QString {
      ExportParams params{};
      params.filename = dir_.filePath(name + ".wav");
      params.video_enabled = false;
      params.audio_enabled = true;
      params.audio_codec = AV_CODEC_ID_PCM_S16LE;
      params.audio_sampling_rate = kAudioRate;
      params.audio_bitrate = 1536;  // ignored by PCM
      params.start_frame = 0;
      params.end_frame = seq->getEndFrame();
      path = params.filename;
      return export_sequence(seq, params);
    };
    QString nested_path;
    QString flat_path;
    QString error = export_audio(nested_seq.get(), "nested", nested_path);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    error = export_audio(flat_seq.get(), "flat", flat_path);
    QVERIFY2(error.isEmpty(), qPrintable(error));

    // --- Decode and compare the left channels sample by sample
    Decoded nested = decode_file(nested_path);
    Decoded flat = decode_file(flat_path);
    QVERIFY(nested.ok);
    QVERIFY(flat.ok);
    QVERIFY(nested.has_audio);
    QVERIFY(flat.has_audio);
    qInfo("samples per channel: nested %ld, flat %ld", nested.audio_samples, flat.audio_samples);
    QVERIFY(nested.audio_left.size() > kAudioRate / 10);
    const int n = qMin(nested.audio_left.size(), flat.audio_left.size());
    float max_delta = 0;
    for (int i = 0; i < n; i++) max_delta = qMax(max_delta, qAbs(nested.audio_left[i] - flat.audio_left[i]));
    qInfo("nested vs flat: %d samples, max |delta| = %f, peaks %f / %f", n, max_delta, nested.audio_peak,
          flat.audio_peak);
    QVERIFY2(flat.audio_peak > 0.3, "flat mix is silent or too quiet");
    QVERIFY2(max_delta <= 2.0f / 32768, qPrintable(QString("max delta %1").arg(max_delta)));
    QCOMPARE(nested.audio_samples, flat.audio_samples);
  }

  // A reversed audio clip must export as the time-reversed forward export: the reverse path decodes a span, flips it
  // and must hand the whole flipped buffer to the mixer, not the last raw frame it decoded
  void reversedAudioMatchesForward() {
    const QString wav = dir_.filePath("ramp.wav");
    {
      QFile f(wav);
      QVERIFY(f.open(QFile::WriteOnly | QFile::Truncate));
      f.write(sine_wav(440.0, 0.5, true));
    }
    const QString project = dir_.filePath("reversed.ove");
    {
      QFile f(project);
      QVERIFY(f.open(QFile::WriteOnly | QFile::Truncate));
      f.write(reversed_fixture(project, wav).toUtf8());
    }

    bool succeeded = false;
    auto* lt = new LoadThread(project, false);
    QPointer<LoadThread> lt_guard(lt);
    connect(lt, &LoadThread::success, this, [&succeeded]() { succeeded = true; });
    lt->start();
    QTRY_VERIFY_WITH_TIMEOUT(lt_guard.isNull(), 10000);
    QVERIFY(succeeded);

    auto find_sequence = [](const QString& name) -> SequencePtr {
      for (int i = 0; i < amber::project_model.childCount(); i++) {
        Media* m = amber::project_model.child(i);
        if (m->get_type() == MEDIA_TYPE_SEQUENCE && m->to_sequence()->name == name) return m->to_sequence();
      }
      return nullptr;
    };
    SequencePtr fwd_seq = find_sequence("Forward");
    SequencePtr rev_seq = find_sequence("Reversed");
    QVERIFY(fwd_seq != nullptr);
    QVERIFY(rev_seq != nullptr);
    QCOMPARE(fwd_seq->clips.size(), 1);
    QCOMPARE(rev_seq->clips.size(), 1);
    ClipPtr fwd_clip = fwd_seq->clips.at(0);
    ClipPtr rev_clip = rev_seq->clips.at(0);
    QVERIFY(rev_clip->reversed());
    QVERIFY(fwd_clip->media() != nullptr);
    Footage* footage = fwd_clip->media()->to_footage();
    QPointer<PreviewGenerator> gen(footage->preview_gen.load());
    QTRY_VERIFY_WITH_TIMEOUT(gen.isNull(), 10000);
    QVERIFY(footage->ready);
    QVERIFY(!footage->invalid);
    QVERIFY(rev_clip->media_length() > kClipOut);
    // A reversed clip plays from the media's last frame down (playhead_to_clip_seconds: media_length - frame - 1), so
    // timeline [0, kClipOut) reads source frames [media_length - 1 - kClipOut, media_length - 1) backwards: start the
    // forward clip there so both exports cover the same source span
    fwd_clip->set_clip_in(rev_clip->media_length() - 1 - kClipOut);

    auto export_audio = [this](Sequence* seq, const QString& name, QString& path) -> QString {
      ExportParams params{};
      params.filename = dir_.filePath(name + ".wav");
      params.video_enabled = false;
      params.audio_enabled = true;
      params.audio_codec = AV_CODEC_ID_PCM_S16LE;
      params.audio_sampling_rate = kAudioRate;
      params.audio_bitrate = 1536;  // ignored by PCM
      params.start_frame = 0;
      params.end_frame = seq->getEndFrame();
      path = params.filename;
      return export_sequence(seq, params);
    };
    QString fwd_path;
    QString rev_path;
    QString error = export_audio(fwd_seq.get(), "forward", fwd_path);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    error = export_audio(rev_seq.get(), "reversed", rev_path);
    QVERIFY2(error.isEmpty(), qPrintable(error));

    Decoded fwd = decode_file(fwd_path);
    Decoded rev = decode_file(rev_path);
    QVERIFY(fwd.ok);
    QVERIFY(rev.ok);
    QVERIFY(fwd.has_audio);
    QVERIFY(rev.has_audio);
    QCOMPARE(rev.audio_samples, fwd.audio_samples);

    // Over the clip's span, the reversed export read forwards must match the forward export read backwards, up to a
    // few samples of alignment (where each path rounds the span's ends): take the best lag, compare sample by sample
    const int span = int(qRound64(kClipOut / kFps * kAudioRate));
    QVERIFY(fwd.audio_left.size() >= span);
    QVERIFY(rev.audio_left.size() >= span);
    constexpr int kMaxLag = 8;
    float best_delta = 1e9f;
    int best_lag = 0;
    for (int lag = -kMaxLag; lag <= kMaxLag; lag++) {
      float d = 0;
      for (int i = kMaxLag; i < span - kMaxLag; i++)
        d = qMax(d, qAbs(rev.audio_left[i] - fwd.audio_left[span - 1 - i + lag]));
      if (d < best_delta) {
        best_delta = d;
        best_lag = lag;
      }
    }
    qInfo("reversed vs forward: %d samples, best lag %d, max |delta| = %f, peaks %f / %f", span, best_lag, best_delta,
          rev.audio_peak, fwd.audio_peak);
    QVERIFY2(fwd.audio_peak > 0.25, "forward export is silent or too quiet");
    QVERIFY2(best_delta <= 2.0f / 32768, qPrintable(QString("max delta %1 at lag %2").arg(best_delta).arg(best_lag)));
  }

  // Track volume and pan scale the exported mix: -6 dB on A1 brings the RMS to about half (x0.501), a hard right pan
  // silences the left channel and a hard left pan leaves it as it was
  void trackVolumeAndPanScaleTheMix() {
    const QString video = dir_.filePath("gain_checkerboard.y4m");
    {
      QFile f(video);
      QVERIFY(f.open(QFile::WriteOnly | QFile::Truncate));
      f.write(checkerboard_y4m());
    }
    const QString audio = dir_.filePath("gain_tone.wav");
    {
      QFile f(audio);
      QVERIFY(f.open(QFile::WriteOnly | QFile::Truncate));
      f.write(sine_wav());
    }
    const QString project = dir_.filePath("gain.ove");
    {
      QFile f(project);
      QVERIFY(f.open(QFile::WriteOnly | QFile::Truncate));
      f.write(fixture(project, video, audio).toUtf8());
    }

    bool succeeded = false;
    auto* lt = new LoadThread(project, false);
    QPointer<LoadThread> lt_guard(lt);
    connect(lt, &LoadThread::success, this, [&succeeded]() { succeeded = true; });
    lt->start();
    QTRY_VERIFY_WITH_TIMEOUT(lt_guard.isNull(), 10000);
    QVERIFY(succeeded);
    SequencePtr seq = amber::ActiveSequence;
    QVERIFY(seq != nullptr);
    for (const auto& c : seq->clips) {
      QVERIFY(c->media() != nullptr);
      QPointer<PreviewGenerator> gen(c->media()->to_footage()->preview_gen.load());
      QTRY_VERIFY_WITH_TIMEOUT(gen.isNull(), 10000);
    }

    // Audio only, lossless; RMS of the left channel
    int run = 0;
    auto left_rms = [&](double volume_db, int pan, double* rms) -> QString {
      TrackState t;
      t.volume_db = volume_db;
      t.pan = pan;
      seq->set_track_state(0, t);
      ExportParams params{};
      params.filename = dir_.filePath(QString("gain_%1.wav").arg(run++));
      params.video_enabled = false;
      params.audio_enabled = true;
      params.audio_codec = AV_CODEC_ID_PCM_S16LE;
      params.audio_sampling_rate = kAudioRate;
      params.audio_bitrate = 1536;  // ignored by PCM
      params.start_frame = 0;
      params.end_frame = seq->getEndFrame();
      const QString error = export_sequence(seq.get(), params);
      if (!error.isEmpty()) return error;
      const Decoded d = decode_file(params.filename);
      if (!d.ok || !d.has_audio || d.audio_left.size() < kAudioRate / 10) return "no audio decoded";
      double sum = 0;
      for (float v : d.audio_left) sum += double(v) * v;
      *rms = qSqrt(sum / d.audio_left.size());
      return QString();
    };

    double unity = 0, minus6 = 0, right = 0, left = 0;
    QString error = left_rms(0.0, 0, &unity);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    error = left_rms(-6.0, 0, &minus6);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    error = left_rms(0.0, 100, &right);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    error = left_rms(0.0, -100, &left);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    seq->set_track_state(0, TrackState());

    const double ratio = minus6 / unity;
    qInfo("left RMS: unity %.4f, -6 dB %.4f (ratio %.4f), R100 %.6f, L100 %.4f", unity, minus6, ratio, right, left);
    QVERIFY2(unity > 0.3, "unity export is silent or too quiet");  // a half-scale sine: RMS 0.354
    QVERIFY2(qAbs(ratio - 0.501) < 0.005, qPrintable(QString("ratio %1").arg(ratio)));
    QVERIFY2(right < 1e-4, qPrintable(QString("R100 left RMS %1").arg(right)));
    QVERIFY2(qAbs(left - unity) < 1e-4, qPrintable(QString("L100 left RMS %1").arg(left)));
  }

  // A 0 dBFS tone through the Compressor at its defaults (-20 dB threshold, 4:1, 10 ms attack) exports at about
  // -15 dBFS once the attack has settled: the effect runs in the export's cacher with a continuous per-stream state
  void compressorSettlesInExport() {
    const QString video = dir_.filePath("comp_checkerboard.y4m");
    {
      QFile f(video);
      QVERIFY(f.open(QFile::WriteOnly | QFile::Truncate));
      f.write(checkerboard_y4m());
    }
    const QString audio = dir_.filePath("comp_tone.wav");
    {
      QFile f(audio);
      QVERIFY(f.open(QFile::WriteOnly | QFile::Truncate));
      f.write(sine_wav(440.0, 32767.0 / 32768.0));
    }
    const QString project = dir_.filePath("comp.ove");
    {
      QFile f(project);
      QVERIFY(f.open(QFile::WriteOnly | QFile::Truncate));
      f.write(fixture(project, video, audio).toUtf8());
    }

    bool succeeded = false;
    auto* lt = new LoadThread(project, false);
    QPointer<LoadThread> lt_guard(lt);
    connect(lt, &LoadThread::success, this, [&succeeded]() { succeeded = true; });
    lt->start();
    QTRY_VERIFY_WITH_TIMEOUT(lt_guard.isNull(), 10000);
    QVERIFY(succeeded);
    SequencePtr seq = amber::ActiveSequence;
    QVERIFY(seq != nullptr);
    Clip* audio_clip = nullptr;
    for (const auto& c : seq->clips) {
      QVERIFY(c->media() != nullptr);
      QPointer<PreviewGenerator> gen(c->media()->to_footage()->preview_gen.load());
      QTRY_VERIFY_WITH_TIMEOUT(gen.isNull(), 10000);
      if (c->track() >= 0) audio_clip = c.get();
    }
    QVERIFY(audio_clip != nullptr);
    const EffectMeta* meta = get_meta_from_name("Compressor");
    QVERIFY(meta != nullptr);
    EffectPtr comp = Effect::Create(audio_clip, meta);
    QVERIFY(comp != nullptr);
    audio_clip->effects.append(comp);

    ExportParams params{};
    params.filename = dir_.filePath("comp.wav");
    params.video_enabled = false;
    params.audio_enabled = true;
    params.audio_codec = AV_CODEC_ID_PCM_S16LE;
    params.audio_sampling_rate = kAudioRate;
    params.audio_bitrate = 1536;  // ignored by PCM
    params.start_frame = 0;
    params.end_frame = seq->getEndFrame();
    const QString error = export_sequence(seq.get(), params);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    const Decoded d = decode_file(params.filename);
    QVERIFY(d.ok);
    QVERIFY(d.has_audio);

    // Peak over the second half of the clip's span (167-333 ms), well past the attack
    const int span = int(qRound64(kClipOut / kFps * kAudioRate));
    QVERIFY(d.audio_left.size() >= span);
    float tail = 0;
    for (int i = span / 2; i < span; i++) tail = qMax(tail, qAbs(d.audio_left[i]));
    const double tail_db = 20.0 * std::log10(qMax(double(tail), 1e-9));
    qInfo("compressor export: overall peak %.4f, peak over the second half %.4f (%.2f dBFS)", d.audio_peak, tail,
          tail_db);
    QVERIFY2(qAbs(tail_db - -15.0) < 0.5, qPrintable(QString("%1 dBFS").arg(tail_db)));
  }

  // A viewer request (start_render() without pixels or file name, published without the thread's lock) must be rendered
  // even when it lands between the thread's "anything queued?" check and its wait. Each request goes out the moment
  // the previous frame is announced, right when the thread runs that check: without the bounded wait in run() the
  // second or third request already sleeps forever.
  void viewerRequestIsNeverLost() {
    SequencePtr seq = std::make_shared<Sequence>();
    seq->width = 16;
    seq->height = 16;
    seq->frame_rate = kFps;
    QOffscreenSurface* surface = QRhiGles2InitParams::newFallbackSurface();
    auto* rt = new RenderThread();
    rt->setGlFallbackSurface(surface);
    std::atomic<int> frames{0};
    connect(rt, &RenderThread::ready, rt, [&frames]() { frames++; }, Qt::DirectConnection);
    rt->start();
    int requests = 0;
    bool lost = false;
    QElapsedTimer total;
    total.start();
    while (!lost && total.elapsed() < 2000) {
      const int before = frames.load();
      rt->start_render(seq.get(), 1, nullptr, nullptr, 0, 1);
      // Spin rather than sleep: answering at once is what hits the window. The first frame creates the QRhi.
      const int deadline_ms = requests == 0 ? 10000 : 1000;
      QElapsedTimer t;
      t.start();
      while (frames.load() == before && t.elapsed() < deadline_ms) {
      }
      lost = frames.load() == before;
      requests++;
    }
    rt->cancel();
    delete rt;
    delete surface;
    QVERIFY2(!lost, qPrintable(QString("request %1 never rendered").arg(requests)));
  }

  // The composite is premultiplied; a pixel format with alpha must receive straight alpha, an opaque one the
  // composite over black. Half-transparent white: straight (255, 255, 255, 128), over black (128, 128, 128).
  void exportWritesStraightAlpha_data() {
    QTest::addColumn<int>("codec");
    QTest::addColumn<int>("pix_fmt");
    QTest::addColumn<QString>("file");
    QTest::newRow("png rgba") << int(AV_CODEC_ID_PNG) << int(AV_PIX_FMT_RGBA) << "png.mov";
    QTest::newRow("png rgba64be") << int(AV_CODEC_ID_PNG) << int(AV_PIX_FMT_RGBA64BE) << "png16.mov";
    QTest::newRow("qtrle argb") << int(AV_CODEC_ID_QTRLE) << int(AV_PIX_FMT_ARGB) << "qtrle.mov";
    QTest::newRow("prores yuva444p10le") << int(AV_CODEC_ID_PRORES) << int(AV_PIX_FMT_YUVA444P10LE) << "prores.mov";
    QTest::newRow("png rgb24 (opaque)") << int(AV_CODEC_ID_PNG) << int(AV_PIX_FMT_RGB24) << "png24.mov";
    QTest::newRow("mpeg4 yuv420p (opaque)") << int(AV_CODEC_ID_MPEG4) << int(AV_PIX_FMT_YUV420P) << "mpeg4.mp4";
  }

  void exportWritesStraightAlpha() {
    QFETCH(int, codec);
    QFETCH(int, pix_fmt);
    QFETCH(QString, file);
    if (avcodec_find_encoder(AVCodecID(codec)) == nullptr) QSKIP("encoder not in this FFmpeg build");

    SequencePtr seq = half_white_sequence();
    ExportParams params;
    params.filename = dir_.filePath(file);
    params.video_enabled = true;
    params.video_codec = codec;
    params.video_width = kSize;
    params.video_height = kSize;
    params.video_compression_type = COMPRESSION_TYPE_CBR;
    params.video_bitrate = 20.0;  // Mbps
    params.audio_enabled = false;
    params.start_frame = 0;
    params.end_frame = 2;
    const QString error = export_sequence(seq.get(), params, AVPixelFormat(pix_fmt));
    QVERIFY2(error.isEmpty(), qPrintable(error));

    Decoded out = decode_file(params.filename);
    QVERIFY(out.ok);
    QVERIFY(out.has_video);
    QCOMPARE(out.first_rgba.size(), kSize * kSize * 4);
    const int off = ((kSize / 2) * kSize + kSize / 2) * 4;
    const int r = uchar(out.first_rgba[off]), g = uchar(out.first_rgba[off + 1]), b = uchar(out.first_rgba[off + 2]),
              a = uchar(out.first_rgba[off + 3]);
    qInfo("%s: centre pixel (%d, %d, %d, %d)", QTest::currentDataTag(), r, g, b, a);
    const bool has_alpha = av_pix_fmt_desc_get(AVPixelFormat(pix_fmt))->flags & AV_PIX_FMT_FLAG_ALPHA;
    // ProRes and MPEG-4 are lossy, and the YUV round trip (BT.601 limited range) costs a few levels
    const int tol = 4;
    if (has_alpha) {
      QVERIFY2(qAbs(a - 128) <= tol, qPrintable(QString("alpha %1").arg(a)));
      for (int ch : {r, g, b}) QVERIFY2(ch >= 255 - tol, qPrintable(QString("straight colour %1").arg(ch)));
    } else {
      for (int ch : {r, g, b}) QVERIFY2(qAbs(ch - 128) <= tol, qPrintable(QString("colour over black %1").arg(ch)));
    }
  }

  // swscale can't write PAL8 (choosing it used to crash the export): it goes through palettegen/paletteuse, one
  // palette per frame. Its colours must come out closer than the fixed 3-3-2 palette of rgb8 (the GIF default), and
  // fully transparent pixels must stay transparent.
  void gifPal8Export() {
    if (avcodec_find_encoder(AV_CODEC_ID_GIF) == nullptr) QSKIP("GIF encoder not in this FFmpeg build");
    if (avfilter_get_by_name("palettegen") == nullptr || avfilter_get_by_name("paletteuse") == nullptr) {
      QSKIP("palettegen/paletteuse not in this FFmpeg build");
    }
    const int frames = kPaletteClipFrames * int(std::size(kPaletteColours));
    auto export_gif = [this, frames](AVPixelFormat pix_fmt, const QString& file, Decoded& out, int size = kSize) {
      SequencePtr seq = palette_sequence();
      ExportParams params;
      params.filename = dir_.filePath(file);
      params.video_enabled = true;
      params.video_codec = AV_CODEC_ID_GIF;
      params.video_width = size;
      params.video_height = size;
      params.video_compression_type = COMPRESSION_TYPE_CBR;
      params.video_bitrate = 20.0;  // Mbps
      params.audio_enabled = false;
      params.start_frame = 0;
      params.end_frame = frames;
      const QString error = export_sequence(seq.get(), params, pix_fmt);
      QVERIFY2(error.isEmpty(), qPrintable(error));
      out = decode_file(params.filename, true);
      QVERIFY(out.ok);
      QVERIFY(out.has_video);
      QCOMPARE(out.rgba.size(), frames);
      for (const QByteArray& f : out.rgba) QCOMPARE(f.size(), size * size * 4);
    };
    // Inside the solid (centre half) or around it, two pixels clear of its edge
    auto inside = [](int x, int y) {
      return x >= kSize / 4 + 2 && x < 3 * kSize / 4 - 2 && y >= kSize / 4 + 2 && y < 3 * kSize / 4 - 2;
    };
    auto outside = [](int x, int y) {
      return x < kSize / 4 - 2 || x >= 3 * kSize / 4 + 2 || y < kSize / 4 - 2 || y >= 3 * kSize / 4 + 2;
    };
    // Mean absolute colour error inside the solid, per channel, over every frame
    auto colour_error = [&inside](const Decoded& d) {
      double sum = 0;
      long n = 0;
      for (int f = 0; f < d.rgba.size(); f++) {
        const QColor& c = kPaletteColours[f / kPaletteClipFrames];
        const int want[3] = {c.red(), c.green(), c.blue()};
        for (int y = 0; y < kSize; y++) {
          for (int x = 0; x < kSize; x++) {
            if (!inside(x, y)) continue;
            const int off = (y * kSize + x) * 4;
            for (int ch = 0; ch < 3; ch++) sum += qAbs(int(uchar(d.rgba[f][off + ch])) - want[ch]);
            n += 3;
          }
        }
      }
      return sum / n;
    };

    Decoded pal8;
    export_gif(AV_PIX_FMT_PAL8, "pal8.gif", pal8);
    if (QTest::currentTestFailed()) return;
    Decoded rgb8;
    export_gif(AV_PIX_FMT_RGB8, "rgb8.gif", rgb8);
    if (QTest::currentTestFailed()) return;

    const double pal8_error = colour_error(pal8);
    const double rgb8_error = colour_error(rgb8);
    qInfo("mean colour error: pal8 %.2f, rgb8 %.2f (%d frames)", pal8_error, rgb8_error, int(pal8.rgba.size()));
    QVERIFY2(pal8_error < 2.0, qPrintable(QString("pal8 error %1").arg(pal8_error)));
    QVERIFY2(pal8_error < rgb8_error, qPrintable(QString("pal8 %1, rgb8 %2").arg(pal8_error).arg(rgb8_error)));

    int max_alpha = 0;
    for (const QByteArray& f : pal8.rgba) {
      for (int y = 0; y < kSize; y++) {
        for (int x = 0; x < kSize; x++) {
          if (outside(x, y)) max_alpha = qMax(max_alpha, int(uchar(f[(y * kSize + x) * 4 + 3])));
        }
      }
    }
    const int centre_alpha = uchar(pal8.rgba[0][((kSize / 2) * kSize + kSize / 2) * 4 + 3]);
    qInfo("pal8 alpha: around the solid max %d, centre %d", max_alpha, centre_alpha);
    QCOMPARE(max_alpha, 0);
    QCOMPARE(centre_alpha, 255);

    // Another export size puts a scale in front of the palette filters
    Decoded scaled;
    export_gif(AV_PIX_FMT_PAL8, "pal8_scaled.gif", scaled, kSize / 2);
  }

  // A pixel format swscale can't write fails the export with an error instead of crashing it
  void exportUnsupportedPixelFormatFails() {
    if (sws_isSupportedOutput(AV_PIX_FMT_BAYER_RGGB8)) QSKIP("swscale writes Bayer now: pick another format");
    if (avcodec_find_encoder(AV_CODEC_ID_RAWVIDEO) == nullptr) QSKIP("rawvideo encoder not in this FFmpeg build");
    SequencePtr seq = half_white_sequence();
    ExportParams params;
    params.filename = dir_.filePath("bayer.nut");
    params.video_enabled = true;
    params.video_codec = AV_CODEC_ID_RAWVIDEO;
    params.video_width = kSize;
    params.video_height = kSize;
    params.video_compression_type = COMPRESSION_TYPE_CBR;
    params.video_bitrate = 20.0;  // Mbps
    params.audio_enabled = false;
    params.start_frame = 0;
    params.end_frame = 2;
    const QString error = export_sequence(seq.get(), params, AV_PIX_FMT_BAYER_RGGB8);
    qInfo("export error: %s", qPrintable(error));
    QVERIFY(error.contains("could not convert to pixel format"));
    QVERIFY(!QFileInfo::exists(params.filename));
  }

  // Save Frame writes a PNG with straight alpha too, and a format without alpha the composite over black
  void saveFrameWritesStraightAlpha_data() {
    QTest::addColumn<QString>("file");
    QTest::newRow("png") << "alpha.png";
    QTest::newRow("jpg (opaque)") << "alpha.jpg";
  }

  void saveFrameWritesStraightAlpha() {
    QFETCH(QString, file);
    SequencePtr seq = half_white_sequence();
    QOffscreenSurface* surface = QRhiGles2InitParams::newFallbackSurface();
    auto* rt = new RenderThread();
    rt->setGlFallbackSurface(surface);
    std::atomic<int> frames{0};
    connect(rt, &RenderThread::ready, rt, [&frames]() { frames++; }, Qt::DirectConnection);
    rt->start();
    const QString fn = dir_.filePath(file);
    QFile::remove(fn);
    // A frame whose clip texture isn't ready keeps the save queued; paint() saves before announcing the frame
    rt->start_render(seq.get(), 1, fn);
    QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(fn), 10000);
    rt->cancel();
    delete rt;
    delete surface;
    QImage saved(fn);
    QCOMPARE(saved.size(), QSize(kSize, kSize));
    const QColor px = saved.pixelColor(kSize / 2, kSize / 2);
    qInfo("save frame %s: centre pixel (%d, %d, %d, %d)", qPrintable(file), px.red(), px.green(), px.blue(),
          px.alpha());
    const QString rgb = QString("%1 %2 %3").arg(px.red()).arg(px.green()).arg(px.blue());
    if (saved.hasAlphaChannel()) {
      QVERIFY2(qAbs(px.alpha() - 128) <= 1, qPrintable(QString("alpha %1").arg(px.alpha())));
      QVERIFY2(px.red() >= 254 && px.green() >= 254 && px.blue() >= 254, qPrintable("straight colour " + rgb));
    } else {
      // JPEG is lossy, a flat colour barely so
      const bool over_black = qAbs(px.red() - 128) <= 3 && qAbs(px.green() - 128) <= 3 && qAbs(px.blue() - 128) <= 3;
      QVERIFY2(over_black, qPrintable("colour over black " + rgb));
    }
  }

  // Save Frame renders at full resolution even when a viewer request at a preview divider (the viewer's retry after a
  // failed render, a scrub) arrives before the save has been rendered
  void saveFrameKeepsFullResolution() {
    SequencePtr seq = std::make_shared<Sequence>();
    seq->width = 16;
    seq->height = 16;
    seq->frame_rate = kFps;
    QOffscreenSurface* surface = QRhiGles2InitParams::newFallbackSurface();
    auto* rt = new RenderThread();
    rt->setGlFallbackSurface(surface);
    std::atomic<int> frames{0};
    connect(rt, &RenderThread::ready, rt, [&frames]() { frames++; }, Qt::DirectConnection);
    rt->start();
    rt->start_render(seq.get(), 1, nullptr, nullptr, 0, 1);  // creates the QRhi
    QTRY_VERIFY_WITH_TIMEOUT(frames.load() == 1, 10000);

    const QString fn = dir_.filePath("frame.png");
    QFile::remove(fn);
    rt->start_render(seq.get(), 1, fn);
    rt->start_render(seq.get(), 1, nullptr, nullptr, 0, 4);
    // paint() saves before announcing the frame
    QTRY_VERIFY_WITH_TIMEOUT(frames.load() > 1, 10000);
    rt->cancel();
    delete rt;
    delete surface;
    QImage saved(fn);
    QCOMPARE(saved.size(), QSize(16, 16));
  }
};

int main(int argc, char* argv[]) {
  qputenv("QT_QPA_PLATFORM", "offscreen");

  // OpenGL 3.2 core so the OpenGL QRhi gets GLSL 150 (the .qsb shaders' GL target); see test_effects_gpu.cpp
  QSurfaceFormat fmt;
  fmt.setVersion(3, 2);
  fmt.setProfile(QSurfaceFormat::CoreProfile);
  fmt.setRenderableType(QSurfaceFormat::OpenGL);
  QSurfaceFormat::setDefaultFormat(fmt);

  QGuiApplication app(argc, argv);
  TestHeadlessRender test;
  return QTest::qExec(&test, argc, argv);
}

#include "test_headless_render.moc"
