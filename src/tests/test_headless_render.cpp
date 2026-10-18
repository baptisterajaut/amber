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
#include <libavutil/log.h>
#include <libavutil/pixdesc.h>
#include <libswscale/swscale.h>
}

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QGuiApplication>
#include <QMap>
#include <QMutex>
#include <QMutexLocker>
#include <QPointer>
#include <QRegularExpression>
#include <QScopeGuard>
#include <QSurfaceFormat>
#include <QTemporaryDir>
#include <QTest>
#include <QtEndian>
#include <QtMath>
#include <algorithm>
#include <functional>

#include "core/audio.h"
#include "core/headlessappcontext.h"
#include "effects/effect.h"
#include "effects/internal/ladspaeffect.h"
#include "engine/clip.h"
#include "engine/sequence.h"
#include "global/config.h"
#include "global/projectio.h"
#include "project/footage.h"
#include "project/loadthread.h"
#include "project/media.h"
#include "project/previewgenerator.h"
#include "project/projectmodel.h"
#include "rendering/batchexport.h"
#include "rendering/encoderoptions.h"
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

// frames of a diagonal gradient scrolling right with noise on top, weak over the first half and strong over the second,
// YUV 4:2:0, as a Y4M stream: rate control has detail to spend bits on, more of it towards the end
QByteArray noise_y4m(int frames) {
  quint32 seed = 12345;
  auto noise = [&seed](int amplitude) {
    seed = seed * 1664525u + 1013904223u;  // fixed LCG: the same stream on every run
    return int((seed >> 16) % quint32(2 * amplitude + 1)) - amplitude;
  };
  QByteArray y4m = QByteArray("YUV4MPEG2 W") + QByteArray::number(kSize) + " H" + QByteArray::number(kSize) + " F" +
                   QByteArray::number(int(kFps)) + ":1 Ip A1:1 C420jpeg\n";
  for (int i = 0; i < frames; i++) {
    const int amplitude = i < frames / 2 ? 6 : 48;
    QByteArray luma(kSize * kSize, Qt::Uninitialized);
    for (int y = 0; y < kSize; y++) {
      for (int x = 0; x < kSize; x++) {
        luma[y * kSize + x] = char(qBound(16, 40 + ((x + y + 3 * i) * 3) % 160 + noise(amplitude), 235));
      }
    }
    QByteArray chroma(kSize * kSize / 2, Qt::Uninitialized);
    for (int c = 0; c < chroma.size(); c++) chroma[c] = char(qBound(16, 128 + noise(amplitude / 2), 240));
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

// A WAV file around stereo S16 PCM at kAudioRate
QByteArray wav_from_pcm(const QByteArray& pcm) {
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

// Seconds (by default 1) of a stereo sine (by default 440 Hz at half scale), S16 PCM. With ramp, the amplitude rises
// linearly from 0 so the signal isn't symmetric in time.
QByteArray sine_wav(double freq = 440.0, double amplitude = 0.5, bool ramp = false, double seconds = 1.0) {
  const int frames = int(qRound64(seconds * kAudioRate));
  QByteArray pcm(frames * 4, Qt::Uninitialized);
  for (int i = 0; i < frames; i++) {
    const double a = ramp ? amplitude * i / frames : amplitude;
    qint16 v = qint16(qRound(32768.0 * a * qSin(2.0 * M_PI * freq * i / kAudioRate)));
    qToLittleEndian(v, pcm.data() + i * 4);
    qToLittleEndian(v, pcm.data() + i * 4 + 2);
  }
  return wav_from_pcm(pcm);
}

// Seconds of a constant stereo level, S16 PCM: an envelope applied to it reads back sample by sample
QByteArray dc_wav(double seconds, double level) {
  const int frames = int(qRound64(seconds * kAudioRate));
  QByteArray pcm(frames * 4, Qt::Uninitialized);
  const qint16 v = qint16(qRound(32768.0 * level));
  for (int i = 0; i < frames * 2; i++) qToLittleEndian(v, pcm.data() + i * 2);
  return wav_from_pcm(pcm);
}

// Same layout Project::save_project() writes: footage 1 (video) and 2 (audio) in folder 1, the open sequence at the
// root. Loop mode 2 (clamp) holds the fixture's single frame for the clip's whole length. A non-empty proxy marks the
// video footage as having that finished proxy file. Both clips span [0, out).
QString fixture(const QString& url, const QString& video, const QString& audio, const QString& proxy = QString(),
                long out = kClipOut) {
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
           QString::number(kSize), QString::number(kFps, 'f', 10), QString::number(kAudioRate), QString::number(out),
           proxy_attrs);
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

// One clip on A1 over timeline [0, out), trimmed by clip_in, at a speed (no pitch correction), optionally reversed: a
// WAV clip, or with no wav a null-media clip (what a Tone or Noise generator sits on)
QString ramp_fixture(const QString& url, const QString& wav, long clip_in, long out, double speed, bool reversed) {
  const QString footage = wav.isEmpty()
                              ? QString()
                              : QString(R"(<footage id="1" folder="1" name="dc" url="%1"/>)").arg(wav.toHtmlEscaped());
  const QString media = wav.isEmpty() ? QString() : QString(R"( media="1" stream="0")");
  return QString(R"(<?xml version="1.0" encoding="UTF-8"?>
<project>
    <version>%1</version>
    <url>%2</url>
    <folders>
        <folder id="1" name="Bin" parent="0"/>
    </folders>
    <media>
        %3
    </media>
    <sequences>
        <sequence id="1" folder="0" name="Ramp" width="%4" height="%4" framerate="%5" afreq="%6" alayout="3"
                  open="1">
            <clip id="0" enabled="1" name="dc" clipin="%7" in="0" out="%8" track="0" r="128" g="192" b="128"
                  autoscale="1" type="0"%9 speed="%10" maintainpitch="0" reverse="%11">
                <linked></linked>
            </clip>
        </sequence>
    </sequences>
</project>
)")
      .arg(QString::number(amber::kSaveVersion), url.toHtmlEscaped(), footage, QString::number(kSize),
           QString::number(kFps, 'f', 10), QString::number(kAudioRate), QString::number(clip_in), QString::number(out),
           media)
      .arg(QString::number(speed, 'f', 3), reversed ? "1" : "0");
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
  qint64 video_bytes = 0;  // all video packets: the stream's bitrate without the container's overhead
  double video_fps = 0;    // stream avg_frame_rate
  int width = 0;
  int height = 0;
  int pix_fmt = -1;  // of the first decoded frame
  int video_codec = AV_CODEC_ID_NONE;
  QByteArray first_rgba;
  QVector<QByteArray> rgba;  // every video frame, when decode_file() is asked to keep them
  int audio_rate = 0;
  int audio_channels = 0;
  long audio_samples = 0;  // per channel
  double audio_peak = 0;   // max |sample|, 0..1
  // Channel 0 samples, -1..1
  QVector<float> audio_left;
  // Every channel, interleaved S16, for S16 and S16P streams (FLAC's 16-bit output): compared byte for byte
  QByteArray audio_s16;
};

void read_audio_frame(const AVFrame* f, Decoded& d) {
  const int ch = f->ch_layout.nb_channels;
  if (f->format == AV_SAMPLE_FMT_S16 || f->format == AV_SAMPLE_FMT_S16P) {
    for (int i = 0; i < f->nb_samples; i++) {
      for (int c = 0; c < ch; c++) {
        const qint16 v = f->format == AV_SAMPLE_FMT_S16P
                             ? reinterpret_cast<const qint16*>(f->extended_data[c])[i]
                             : reinterpret_cast<const qint16*>(f->extended_data[0])[i * ch + c];
        d.audio_s16.append(reinterpret_cast<const char*>(&v), 2);
      }
    }
  }
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
  if (vctx != nullptr) {
    d.video_fps = av_q2d(fmt->streams[vi]->avg_frame_rate);
    d.video_codec = fmt->streams[vi]->codecpar->codec_id;
  }
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
          d.pix_fmt = frame->format;
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
      d.video_bytes += pkt->size;
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

// Offset of the first top-level box of this type in an MP4/MOV file, -1 if there is none
qint64 mp4_box_offset(const QByteArray& file, const char* type) {
  qint64 pos = 0;
  while (pos + 8 <= file.size()) {
    qint64 size = qFromBigEndian<quint32>(file.constData() + pos);
    if (size == 1 && pos + 16 <= file.size()) size = qint64(qFromBigEndian<quint64>(file.constData() + pos + 8));
    if (size == 0) size = file.size() - pos;  // extends to the end of the file
    if (file.mid(pos + 4, 4) == type) return pos;
    if (size < 8) return -1;
    pos += size;
  }
  return -1;
}

// x264's settings string (its SEI: "x264 - core ... options: ..."), empty if the file has none
QByteArray x264_settings(const QByteArray& file) {
  const qsizetype start = file.indexOf("x264 - core");
  if (start < 0) return QByteArray();
  const qsizetype end = file.indexOf('\0', start);
  return file.mid(start, end < 0 ? -1 : end - start);
}

// FFmpeg's log while an AvLogCapture lives, every level (the encoders' debug dumps included), not printed
QMutex g_av_log_mutex;
QString g_av_log;

void capture_av_log(void* avcl, int level, const char* fmt, va_list vl) {
  char line[2048];
  int print_prefix = 1;
  av_log_format_line2(avcl, level, fmt, vl, line, sizeof(line), &print_prefix);
  QMutexLocker locker(&g_av_log_mutex);
  g_av_log += QString::fromUtf8(line);
}

struct AvLogCapture {
  AvLogCapture() {
    QMutexLocker locker(&g_av_log_mutex);
    g_av_log.clear();
    av_log_set_callback(capture_av_log);
  }
  ~AvLogCapture() { av_log_set_callback(av_log_default_callback); }
  QString text() const {
    QMutexLocker locker(&g_av_log_mutex);
    return g_av_log;
  }
};

// The entries of a directory, for "nothing was left behind" checks
QStringList dir_entries(const QString& path) {
  return QDir(path).entryList(QDir::AllEntries | QDir::Hidden | QDir::NoDotAndDotDot, QDir::Name);
}

// kSize x kSize sequence of one red Solid over frames [0, frames), its Transform opacity ramping from 100 % down to
// 20 % over black, so every frame differs
SequencePtr solid_sequence(long frames) {
  SequencePtr seq = std::make_shared<Sequence>();
  seq->name = "Solid";
  seq->width = kSize;
  seq->height = kSize;
  seq->frame_rate = kFps;
  seq->audio_frequency = kAudioRate;
  ClipPtr c = std::make_shared<Clip>(seq.get());
  c->set_media(nullptr, 0);
  c->set_timeline_in(0);
  c->set_timeline_out(frames);
  c->set_clip_in(0);
  c->set_track(-1);
  c->set_name("Solid");
  EffectPtr transform = Effect::Create(c.get(), Effect::GetInternalMeta(EFFECT_INTERNAL_TRANSFORM, EFFECT_TYPE_EFFECT));
  EffectRow* opacity = transform->row(5);
  opacity->SetKeyframingInternal(true);
  for (const auto& [time, value] : {std::pair<long, double>{0, 100.0}, {frames, 20.0}}) {
    EffectKeyframe k;
    k.time = time;
    k.data = value;
    k.type = EFFECT_KEYFRAME_LINEAR;
    opacity->Field(0)->keyframes.append(k);
  }
  c->effects.append(transform);
  EffectPtr solid = Effect::Create(c.get(), Effect::GetInternalMeta(EFFECT_INTERNAL_SOLID, EFFECT_TYPE_EFFECT));
  solid->row(2)->Field(0)->SetValueAt(0.0, QVariant(QColor(220, 40, 40)));  // Color
  c->effects.append(solid);
  seq->clips.append(c);
  return seq;
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
  // success. hook gets the thread before it starts (to connect its signals).
  QString export_sequence(Sequence* seq, const ExportParams& params, AVPixelFormat pix_fmt = AV_PIX_FMT_YUV420P,
                          const std::function<void(ExportThread*)>& hook = nullptr) {
    VideoCodecParams vparams;
    vparams.pix_fmt = pix_fmt;
    vparams.threads = 0;

    auto* et = new ExportThread(seq, params, vparams);
    if (hook) hook(et);
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

  // Load, as the active sequence, ramp_fixture() over media (empty: a null-media clip) and wait for its probe. Callers
  // return on QTest::currentTestFailed().
  void load_ramp_project(const QString& project, const QString& media, long clip_in, long out, double speed,
                         bool reversed, SequencePtr* seq) {
    {
      QFile f(project);
      QVERIFY(f.open(QFile::WriteOnly | QFile::Truncate));
      f.write(ramp_fixture(project, media, clip_in, out, speed, reversed).toUtf8());
    }
    bool succeeded = false;
    auto* lt = new LoadThread(project, false);
    QPointer<LoadThread> lt_guard(lt);
    connect(lt, &LoadThread::success, this, [&succeeded]() { succeeded = true; });
    lt->start();
    QTRY_VERIFY_WITH_TIMEOUT(lt_guard.isNull(), 10000);
    QVERIFY(succeeded);
    *seq = amber::ActiveSequence;
    QVERIFY(*seq != nullptr);
    QCOMPARE((*seq)->clips.size(), 1);
    Clip* clip = (*seq)->clips.at(0).get();
    QCOMPARE(clip->media() == nullptr, media.isEmpty());
    if (clip->media() != nullptr) {
      QPointer<PreviewGenerator> gen(clip->media()->to_footage()->preview_gen.load());
      QTRY_VERIFY_WITH_TIMEOUT(gen.isNull(), 10000);
      QVERIFY(clip->media()->to_footage()->ready);
    }
  }

  // Load, as the active sequence, fixture() over a noise_y4m() video and a tone, both clips spanning [0, frames), and
  // wait for the probes. Callers return on QTest::currentTestFailed().
  void load_noise_project(const QString& name, long frames, SequencePtr* seq) {
    const QString video = dir_.filePath(name + "_noise.y4m");
    {
      QFile f(video);
      QVERIFY(f.open(QFile::WriteOnly | QFile::Truncate));
      f.write(noise_y4m(int(frames) + 2));  // two spare frames, as kSourceFrames
    }
    const QString audio = dir_.filePath(name + "_tone.wav");
    {
      QFile f(audio);
      QVERIFY(f.open(QFile::WriteOnly | QFile::Truncate));
      f.write(sine_wav(440.0, 0.5, false, frames / kFps + 1));
    }
    const QString project = dir_.filePath(name + ".ove");
    {
      QFile f(project);
      QVERIFY(f.open(QFile::WriteOnly | QFile::Truncate));
      f.write(fixture(project, video, audio, QString(), frames).toUtf8());
    }
    bool succeeded = false;
    auto* lt = new LoadThread(project, false);
    QPointer<LoadThread> lt_guard(lt);
    connect(lt, &LoadThread::success, this, [&succeeded]() { succeeded = true; });
    lt->start();
    QTRY_VERIFY_WITH_TIMEOUT(lt_guard.isNull(), 10000);
    QVERIFY(succeeded);
    *seq = amber::ActiveSequence;
    QVERIFY(*seq != nullptr);
    for (const auto& c : (*seq)->clips) {
      QVERIFY(c->media() != nullptr);
      Footage* f = c->media()->to_footage();
      QPointer<PreviewGenerator> gen(f->preview_gen.load());
      QTRY_VERIFY_WITH_TIMEOUT(gen.isNull(), 10000);
      QVERIFY(f->ready);
      QVERIFY2(c->media_length() >= frames, qPrintable(QString("media length %1").arg(c->media_length())));
    }
    QCOMPARE((*seq)->getEndFrame(), frames);
  }

  // Points TMPDIR (QDir::tempPath(), where QTemporaryDir goes) at an empty folder of the test's own until the guard
  // goes, so a two-pass test can check that nothing was left there
  auto private_tmpdir(const QString& name, QString* path) {
    *path = dir_.filePath("tmp_" + name);
    QDir().mkpath(*path);
    const QByteArray saved = qgetenv("TMPDIR");
    qputenv("TMPDIR", path->toUtf8());
    return qScopeGuard([saved] {
      if (saved.isEmpty()) {
        qunsetenv("TMPDIR");
      } else {
        qputenv("TMPDIR", saved);
      }
    });
  }

  // The batch sequences of the BatchExport tests: Wide (64x48, 30 fps, 10 frames) and Tall (32x64, 25 fps, 6 frames,
  // in/out range 1-4), both solid_sequence(), and the user's active sequence; each has its playhead away from 0
  struct BatchSequences {
    SequencePtr wide;
    SequencePtr tall;
    SequencePtr user;
  };
  static BatchSequences batch_sequences() {
    BatchSequences b;
    b.wide = solid_sequence(10);
    b.wide->name = "Wide";
    b.wide->width = 64;
    b.wide->height = 48;
    b.wide->playhead = 7;
    b.tall = solid_sequence(6);
    b.tall->name = "Tall";
    b.tall->width = 32;
    b.tall->height = 64;
    b.tall->frame_rate = 25.0;
    b.tall->using_workarea = true;
    b.tall->workarea_in = 1;
    b.tall->workarea_out = 4;
    b.tall->playhead = 2;
    b.user = solid_sequence(4);
    b.user->name = "User";
    b.user->playhead = 3;
    return b;
  }

  // The export dialog's settings a batch starts from: MPEG-4 video, no audio
  static ExportParams batch_base() {
    ExportParams params{};
    params.video_enabled = true;
    params.video_codec = AV_CODEC_ID_MPEG4;
    params.video_compression_type = COMPRESSION_TYPE_CBR;
    params.video_bitrate = 2.0;
    params.audio_enabled = false;
    return params;
  }

  // Jobs for seqs into folder, named and derived as ExportDialog::start_batch_export() does; a PNG image sequence
  // with image_sequence. Every job gets vparams, as the dialog's Advanced settings.
  static QVector<BatchExport::Job> batch_jobs(const QVector<SequencePtr>& seqs, const QString& folder, bool use_inout,
                                              bool image_sequence = false,
                                              const VideoCodecParams& vparams = yuv420p_params()) {
    const QString ext = image_sequence ? "png" : "mp4";
    ExportParams base = batch_base();
    if (image_sequence) base.video_codec = AV_CODEC_ID_PNG;
    QStringList taken = amber::batch_export_taken_names(dir_entries(folder), image_sequence);
    QVector<BatchExport::Job> jobs;
    for (const SequencePtr& s : seqs) {
      const QString name = amber::batch_export_filename(s->name, ext, taken);
      taken.append(name);
      const QString filename = amber::batch_export_path(folder, name, ext, image_sequence);
      jobs.append({s, amber::batch_export_params(base, s.get(), use_inout, filename), vparams});
    }
    return jobs;
  }

  // Runs batch to its end; on_start gets each job index as its export starts. False on time-out.
  bool run_batch(BatchExport& batch, const std::function<void(int)>& on_start = nullptr) {
    bool finished = false;
    connect(&batch, &BatchExport::Finished, this, [&finished]() { finished = true; });
    if (on_start) {
      connect(&batch, &BatchExport::SequenceStarted, this,
              [&on_start](int index, int, const QString&) { on_start(index); });
    }
    batch.Start();
    return QTest::qWaitFor([&finished]() { return finished; }, 120000);
  }

  static VideoCodecParams yuv420p_params() {
    VideoCodecParams vparams;
    vparams.pix_fmt = AV_PIX_FMT_YUV420P;
    vparams.threads = 0;
    return vparams;
  }

  // Audio-only export of a whole sequence from start_frame to its end
  QString export_audio(Sequence* seq, const QString& file, AVCodecID codec, long start_frame) {
    ExportParams params{};
    params.filename = file;
    params.video_enabled = false;
    params.audio_enabled = true;
    params.audio_codec = codec;
    params.audio_sampling_rate = kAudioRate;
    params.audio_bitrate = 1536;  // ignored by PCM and FLAC
    params.start_frame = start_frame;
    params.end_frame = seq->getEndFrame();
    return export_sequence(seq, params);
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

#ifndef NOLADSPA
  // A Tone clip through LADSPA amp_mono at -6 dB exports at half the RMS of the same clip without it
  void ladspaAmpHalvesExport() {
    const EffectMeta* amp = nullptr;
    for (const EffectMeta& em : effects) {
      if (em.internal == EFFECT_INTERNAL_LADSPA && em.plugin_id == 1048) amp = &em;
    }
    if (amp == nullptr) {
      for (const EffectMeta& em : amber::ladspa::scan(amber::ladspa::search_paths())) {
        if (em.plugin_id == 1048) {
          effects.push_back(em);
          amp = &effects.back();
        }
      }
    }
    if (amp == nullptr) QSKIP("amp_mono (1048, LADSPA SDK amp.so) not installed");
    const EffectMeta* tone_meta = get_meta_from_name("Tone");
    QVERIFY(tone_meta != nullptr);

    double rms[2] = {0, 0};
    for (int pass = 0; pass < 2; pass++) {
      SequencePtr seq;
      load_ramp_project(dir_.filePath(QString("ladspa_%1.ove").arg(pass)), QString(), 0, 30, 1.0, false, &seq);
      if (QTest::currentTestFailed()) return;
      Clip* clip = seq->clips.at(0).get();
      EffectPtr gen = Effect::Create(clip, tone_meta);  // 1000 Hz at 25 % amount
      QVERIFY(gen != nullptr);
      clip->effects.append(gen);
      if (pass == 1) {
        EffectPtr e = Effect::Create(clip, amp);
        QVERIFY(e != nullptr);
        QCOMPARE(e->row_count(), 1);
        e->row(0)->Field(0)->SetValueAt(0, qPow(10.0, -6.0 / 20.0));
        clip->effects.append(e);
      }
      const QString out = dir_.filePath(QString("ladspa_%1.wav").arg(pass));
      const QString error = export_audio(seq.get(), out, AV_CODEC_ID_PCM_S16LE, 0);
      QVERIFY2(error.isEmpty(), qPrintable(error));
      const Decoded d = decode_file(out);
      QVERIFY(d.ok);
      QVERIFY(d.has_audio);
      const int span = int(qRound64(30 / kFps * kAudioRate));
      QVERIFY(d.audio_left.size() >= span);
      double sum = 0;
      for (int i = 0; i < span; i++) sum += double(d.audio_left[i]) * d.audio_left[i];
      rms[pass] = qSqrt(sum / span);
    }
    const double ratio = rms[1] / rms[0];
    qInfo("LADSPA amp_mono -6 dB export: RMS %.5f -> %.5f, ratio %.5f", rms[0], rms[1], ratio);
    QVERIFY(rms[0] > 0.05);
    QVERIFY2(qAbs(ratio - 0.5) < 0.005, qPrintable(QString::number(ratio)));
  }
#endif

  void volumeRampFollowsTimeline_data() {
    QTest::addColumn<double>("speed");
    QTest::addColumn<bool>("reversed");
    QTest::addColumn<int>("clip_in");      // frames trimmed off the clip's head
    QTest::addColumn<int>("start_frame");  // export range start
    // wav, flac (a FLAC seek stops at a block boundary short of its target, a WAV seek is sample-exact) or tone (a
    // null-media clip with a Tone)
    QTest::addColumn<QString>("source");
    QTest::newRow("speed 1") << 1.0 << false << 0 << 0 << "wav";
    QTest::newRow("speed 2") << 2.0 << false << 0 << 0 << "wav";
    QTest::newRow("reversed") << 1.0 << true << 0 << 0 << "wav";
    QTest::newRow("trimmed") << 1.0 << false << 10 << 0 << "wav";
    QTest::newRow("flac, export from frame 10") << 1.0 << false << 0 << 10 << "flac";
    QTest::newRow("flac, trimmed, export from frame 10") << 1.0 << false << 10 << 10 << "flac";
    QTest::newRow("flac, reversed, export from frame 10") << 1.0 << true << 0 << 10 << "flac";
    QTest::newRow("tone") << 1.0 << false << 0 << 0 << "tone";
    QTest::newRow("tone, trimmed") << 1.0 << false << 10 << 0 << "tone";
    QTest::newRow("tone, trimmed, export from frame 10") << 1.0 << false << 10 << 10 << "tone";
  }

  // A Volume keyframed linearly from 0 at the clip's start to 1 at its end exports as one straight ramp over the
  // clip's timeline span whatever the clip's speed, direction, trim or the export's start: keyframes live in
  // clip-local timeline time (as for video effects, get_timecode()), so each chunk handed to the effect must start at
  // its sample 0's clip-local time and span its real playback duration
  void volumeRampFollowsTimeline() {
    QFETCH(double, speed);
    QFETCH(bool, reversed);
    QFETCH(int, clip_in);
    QFETCH(int, start_frame);
    QFETCH(QString, source);
    const bool tone = source == "tone";
    constexpr long kOut = 30;  // 1 s
    constexpr double kLevel = 0.5;
    const QString tag = QString("ramp_%1").arg(QTest::currentDataTag()).replace(QRegularExpression("[^a-z0-9]+"), "_");
    QString media;
    if (!tone) {
      media = dir_.filePath(tag + ".wav");
      QFile f(media);
      QVERIFY(f.open(QFile::WriteOnly | QFile::Truncate));
      f.write(dc_wav(3.0, kLevel));  // enough source for 2x
    }
    if (source == "flac") {
      if (avcodec_find_encoder(AV_CODEC_ID_FLAC) == nullptr) QSKIP("FLAC encoder not in this FFmpeg build");
      SequencePtr src;
      load_ramp_project(dir_.filePath(tag + "_src.ove"), media, 0, 60, 1.0, false, &src);
      if (QTest::currentTestFailed()) return;
      const QString flac = dir_.filePath(tag + ".flac");
      const QString error = export_audio(src.get(), flac, AV_CODEC_ID_FLAC, 0);
      QVERIFY2(error.isEmpty(), qPrintable(error));
      media = flac;
    }
    SequencePtr seq;
    load_ramp_project(dir_.filePath(tag + ".ove"), media, clip_in, kOut, speed, reversed, &seq);
    if (QTest::currentTestFailed()) return;
    Clip* clip = seq->clips.at(0).get();
    QCOMPARE(clip->reversed(), reversed);
    QCOMPARE(clip->speed().value, speed);
    QCOMPARE(clip->clip_in(), long(clip_in));

    if (tone) {
      const EffectMeta* tone_meta = get_meta_from_name("Tone");
      QVERIFY(tone_meta != nullptr);
      EffectPtr gen = Effect::Create(clip, tone_meta);  // 1000 Hz at 25 % amount
      QVERIFY(gen != nullptr);
      clip->effects.append(gen);
    }
    const EffectMeta* meta = get_meta_from_name("Volume");
    QVERIFY(meta != nullptr);
    EffectPtr vol = Effect::Create(clip, meta);
    QVERIFY(vol != nullptr);
    QVERIFY(vol->row_count() > 0 && vol->row(0)->FieldCount() > 0);
    EffectRow* row = vol->row(0);
    row->SetKeyframingInternal(true);
    EffectField* field = row->Field(0);
    for (const auto& [time, value] : {std::pair<long, double>{clip_in, 0.0}, {clip_in + kOut, 1.0}}) {
      EffectKeyframe k;
      k.time = time;
      k.data = value;
      k.type = EFFECT_KEYFRAME_LINEAR;
      field->keyframes.append(k);
    }
    clip->effects.append(vol);

    const QString out = dir_.filePath(tag + "_out.wav");
    const QString error = export_audio(seq.get(), out, AV_CODEC_ID_PCM_S16LE, start_frame);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    const Decoded d = decode_file(out);
    QVERIFY(d.ok);
    QVERIFY(d.has_audio);

    // Timeline sample t plays at t / kAudioRate s into the clip: expected gain t / span. A chunk spanning twice its
    // duration runs the ramp ahead and drops back at the next chunk (a sawtooth). Within half a frame of a keyframe the
    // field returns the keyframe's own value (GetKeyframeData() rounds the time to a frame), so those plateaus are
    // skipped. A tone's gain is its amplitude over a whole 48-sample period, against the Tone's own level.
    const int span = int(qRound64(kOut / kFps * kAudioRate));
    const int half_frame = int(qRound64(0.5 / kFps * kAudioRate));
    const int offset = int(qRound64(start_frame / kFps * kAudioRate));  // timeline sample of the export's sample 0
    QVERIFY(d.audio_left.size() >= span - offset);
    constexpr int kPeriod = 48;  // 1000 Hz at 48 kHz
    const double tone_level = log_volume(0.25) * INT16_MAX / 32768.0;
    const int step = tone ? kPeriod : 1;
    double max_dev = 0;
    int worst = 0;
    double worst_got = 0, worst_want = 0;
    for (int t = qMax(half_frame, offset); t + step <= span - half_frame; t += step) {
      double got = 0, want = 0;
      if (tone) {
        double s = 0, c = 0;
        for (int k = 0; k < kPeriod; k++) {
          const double v = d.audio_left[t - offset + k];
          s += v * qSin(2.0 * M_PI * k / kPeriod);
          c += v * qCos(2.0 * M_PI * k / kPeriod);
        }
        got = 2.0 / kPeriod * qSqrt(s * s + c * c) / tone_level;
        want = (t + (kPeriod - 1) / 2.0) / span;
      } else {
        got = d.audio_left[t - offset] / kLevel;
        want = double(t) / span;
      }
      const double dev = qAbs(got - want);
      if (!(dev <= max_dev)) {  // NaN-safe
        max_dev = dev;
        worst = t;
        worst_got = got;
        worst_want = want;
      }
    }
    qInfo("volume ramp: max |gain deviation| %.5f at timeline sample %d (gain %.5f, want %.5f)", max_dev, worst,
          worst_got, worst_want);
    QVERIFY2(max_dev < 0.002, qPrintable(QString("max deviation %1 at sample %2").arg(max_dev).arg(worst)));
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

  // Video-only export parameters over the whole of seq
  static ExportParams video_params(const QString& file, Sequence* seq, int codec, const QString& encoder,
                                   int compression_type, double value) {
    ExportParams params;
    params.filename = file;
    params.video_enabled = true;
    params.video_codec = codec;
    params.video_encoder = encoder;
    params.video_width = kSize;
    params.video_height = kSize;
    params.video_compression_type = compression_type;
    params.video_bitrate = value;
    params.audio_enabled = false;
    params.start_frame = 0;
    params.end_frame = seq->getEndFrame();
    return params;
  }

  // x264's preset reaches the encoder: ultrafast and veryslow write different settings into x264's SEI and both
  // decode to every frame; the quality-based CRF and the average bitrate show there too; the default preset passed
  // explicitly changes nothing; an option the encoder doesn't know is reported, not fatal
  void x264OptionsReachEncoder() {
    if (avcodec_find_encoder_by_name("libx264") == nullptr) QSKIP("libx264 not in this FFmpeg build");
    if (avcodec_find_decoder(AV_CODEC_ID_H264) == nullptr) QSKIP("no H.264 decoder");
    const long frames = long(kFps);  // 1 s
    SequencePtr seq = solid_sequence(frames);
    auto run = [&](const QString& name, const QString& encoder, int compression_type, double value,
                   const QVector<QPair<QString, QString>>& options, QByteArray* bytes) {
      ExportParams params =
          video_params(dir_.filePath(name), seq.get(), AV_CODEC_ID_H264, encoder, compression_type, value);
      params.video_options = options;
      const QString error = export_sequence(seq.get(), params);
      QVERIFY2(error.isEmpty(), qPrintable(error));
      const Decoded d = decode_file(params.filename);
      QVERIFY(d.ok);
      QCOMPARE(long(d.video_frames), frames);
      QFile f(params.filename);
      QVERIFY(f.open(QFile::ReadOnly));
      *bytes = f.readAll();
    };
    QByteArray fast, slow, plain, medium, abr, bogus;
    run("x264_ultrafast.mp4", "libx264", COMPRESSION_TYPE_CFR, 30, {{"preset", "ultrafast"}}, &fast);
    if (QTest::currentTestFailed()) return;
    run("x264_veryslow.mp4", "libx264", COMPRESSION_TYPE_CFR, 30, {{"preset", "veryslow"}}, &slow);
    if (QTest::currentTestFailed()) return;
    run("x264_plain.mp4", QString(), COMPRESSION_TYPE_CFR, 30, {}, &plain);
    if (QTest::currentTestFailed()) return;
    run("x264_medium.mp4", "libx264", COMPRESSION_TYPE_CFR, 30, {{"preset", "medium"}}, &medium);
    if (QTest::currentTestFailed()) return;
    run("x264_abr.mp4", "libx264", COMPRESSION_TYPE_TARGETBR, 0.5, {}, &abr);
    if (QTest::currentTestFailed()) return;
    QTest::ignoreMessage(QtWarningMsg, QRegularExpression("video encoder ignored option amber_no_such_option"));
    run("x264_bogus.mp4", "libx264", COMPRESSION_TYPE_CFR, 30, {{"amber_no_such_option", "1"}}, &bogus);
    if (QTest::currentTestFailed()) return;

    const QByteArray fast_settings = x264_settings(fast);
    const QByteArray slow_settings = x264_settings(slow);
    qInfo("ultrafast: %lld bytes, veryslow: %lld bytes, default: %lld bytes", qint64(fast.size()), qint64(slow.size()),
          qint64(plain.size()));
    qInfo("ultrafast settings: %s", fast_settings.constData());
    qInfo("veryslow settings: %s", slow_settings.constData());
    QVERIFY(!fast_settings.isEmpty());
    QVERIFY(!slow_settings.isEmpty());
    QVERIFY(fast_settings != slow_settings);
    QVERIFY(fast_settings.contains("subme=0"));  // ultrafast turns subpixel refinement off
    QVERIFY(slow_settings.contains("subme=10"));
    // x264 prints its settings with the process locale's decimal separator
    const QRegularExpression crf30("crf=30[.,]0 ");
    QVERIFY(QString::fromLatin1(fast_settings).contains(crf30));
    QVERIFY(QString::fromLatin1(slow_settings).contains(crf30));
    // No encoder name and libx264 with its default preset spelled out: the same file
    QVERIFY(plain == medium);
    const QByteArray abr_settings = x264_settings(abr);
    qInfo("average bitrate settings: %s", abr_settings.constData());
    QVERIFY(abr_settings.contains("rc=abr"));
    QVERIFY(abr_settings.contains("bitrate=500"));
    QVERIFY(bogus == plain);
  }

  void av1Export_data() {
    QTest::addColumn<QString>("encoder");
    QTest::addColumn<int>("quality");
    QTest::addColumn<QString>("speed_key");
    QTest::addColumn<QString>("speed");
    QTest::newRow("libsvtav1") << "libsvtav1" << 35 << "preset" << "12";
    QTest::newRow("librav1e") << "librav1e" << 100 << "speed" << "10";
    QTest::newRow("libaom-av1") << "libaom-av1" << 32 << "cpu-used" << "8";
  }

  // AV1 through each installed software encoder, quality-based at its own quality key, at a fast speed: every frame
  // comes back at the export size, red where the solid is opaque
  void av1Export() {
    QFETCH(QString, encoder);
    QFETCH(int, quality);
    QFETCH(QString, speed_key);
    QFETCH(QString, speed);
    if (avcodec_find_encoder_by_name(encoder.toUtf8().constData()) == nullptr) QSKIP("encoder not in this build");
    if (avcodec_find_decoder(AV_CODEC_ID_AV1) == nullptr) QSKIP("no AV1 decoder");
    const long frames = long(kFps);
    SequencePtr seq = solid_sequence(frames);
    ExportParams params = video_params(dir_.filePath(encoder + ".mkv"), seq.get(), AV_CODEC_ID_AV1, encoder,
                                       COMPRESSION_TYPE_CFR, quality);
    params.video_options = {{speed_key, speed}};
    QElapsedTimer timer;
    timer.start();
    const QString error = export_sequence(seq.get(), params);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    const Decoded d = decode_file(params.filename);
    qInfo("%s: %lld bytes, %d frames, %dx%d, %lld ms", qPrintable(encoder), QFileInfo(params.filename).size(),
          d.video_frames, d.width, d.height, timer.elapsed());
    QVERIFY(d.ok);
    QVERIFY(d.has_video);
    QCOMPARE(long(d.video_frames), frames);
    QCOMPARE(d.width, kSize);
    QCOMPARE(d.height, kSize);
    const int off = ((kSize / 2) * kSize + kSize / 2) * 4;
    const int r = uchar(d.first_rgba[off]), g = uchar(d.first_rgba[off + 1]), b = uchar(d.first_rgba[off + 2]);
    qInfo("first frame centre (%d, %d, %d)", r, g, b);
    QVERIFY(r > 180 && g < 80 && b < 80);
  }

  // VP9 quality-based: libvpx encodes at constant quality (VPX_Q, which needs bit_rate 0; with a bitrate it would be
  // constrained quality), and CRF 50 makes a much smaller file than CRF 10
  void vp9QualityMode() {
    if (avcodec_find_encoder_by_name("libvpx-vp9") == nullptr) QSKIP("libvpx-vp9 not in this FFmpeg build");
    if (avcodec_find_decoder(AV_CODEC_ID_VP9) == nullptr) QSKIP("no VP9 decoder");
    constexpr long kFrames = 30;
    SequencePtr seq;
    load_noise_project("vp9_crf", kFrames, &seq);
    if (QTest::currentTestFailed()) return;
    auto run = [&](int crf, qint64* size, QString* rc_end_usage) {
      ExportParams params = video_params(dir_.filePath(QString("vp9_crf%1.webm").arg(crf)), seq.get(), AV_CODEC_ID_VP9,
                                         "libvpx-vp9", COMPRESSION_TYPE_CFR, crf);
      params.video_options = {{"cpu-used", "8"}};
      QString log;
      {
        AvLogCapture capture;
        const QString error = export_sequence(seq.get(), params);
        log = capture.text();
        QVERIFY2(error.isEmpty(), qPrintable(error));
      }
      // libvpxenc.c dumps its config at debug level before and after setting it up: the last dump is what it encodes
      // with. vpx_encoder.h: VPX_VBR 0, VPX_CBR 1, VPX_CQ 2, VPX_Q 3.
      QRegularExpressionMatchIterator it = QRegularExpression("rc_end_usage:\\s+(\\d+)").globalMatch(log);
      while (it.hasNext()) *rc_end_usage = it.next().captured(1);
      const Decoded d = decode_file(params.filename);
      QVERIFY(d.ok);
      QVERIFY(d.has_video);
      QCOMPARE(long(d.video_frames), kFrames);
      *size = QFileInfo(params.filename).size();
    };
    qint64 size10 = 0, size50 = 0;
    QString usage10, usage50;
    run(10, &size10, &usage10);
    if (QTest::currentTestFailed()) return;
    run(50, &size50, &usage50);
    if (QTest::currentTestFailed()) return;
    qInfo("VP9 CRF 10: %lld bytes, rc_end_usage %s; CRF 50: %lld bytes, rc_end_usage %s", size10, qPrintable(usage10),
          size50, qPrintable(usage50));
    QCOMPARE(usage10, QString("3"));
    QCOMPARE(usage50, QString("3"));
    QVERIFY2(size50 * 2 < size10, qPrintable(QString("CRF 50 %1 bytes, CRF 10 %2 bytes").arg(size50).arg(size10)));
  }

  void twoPassExport_data() {
    QTest::addColumn<QString>("encoder");
    QTest::addColumn<int>("codec");
    QTest::addColumn<QString>("ext");
    QTest::addColumn<QString>("speed_key");
    QTest::addColumn<QString>("speed");
    QTest::addColumn<bool>("audio");
    QTest::addColumn<double>("mbps");
    // Targets the encoder can reach on 64x64 noise. ffmpeg -pass 1/2 on the same Y4M: libaom (cpu-used 8) can't go
    // much below 0.5 Mbps there, SVT-AV1 (preset 8) undershoots 0.6 Mbps by 28 % and lands within 10 % of 0.3.
    // Not librav1e: no two-pass (encoderoptions.cpp).
    QTest::newRow("libx264") << "libx264" << int(AV_CODEC_ID_H264) << "mp4" << QString() << QString() << true << 0.6;
    QTest::newRow("libx265") << "libx265" << int(AV_CODEC_ID_HEVC) << "mp4" << "preset" << "ultrafast" << false << 0.6;
    QTest::newRow("libvpx-vp9") << "libvpx-vp9" << int(AV_CODEC_ID_VP9) << "webm" << "cpu-used" << "8" << false << 0.6;
    QTest::newRow("libaom-av1") << "libaom-av1" << int(AV_CODEC_ID_AV1) << "mkv" << "cpu-used" << "8" << false << 0.6;
    QTest::newRow("libsvtav1") << "libsvtav1" << int(AV_CODEC_ID_AV1) << "mkv" << "preset" << "8" << false << 0.3;
  }

  // Two-pass through every encoder flagged supports_two_pass: an analysis pass, then the export, reported as passes 1
  // and 2 with the bar's halves; every frame comes back, the average bitrate lands near the target, the audio (x264
  // row) is whole, and no stats file or temporary folder is left behind
  void twoPassExport() {
    QFETCH(QString, encoder);
    QFETCH(int, codec);
    QFETCH(QString, ext);
    QFETCH(QString, speed_key);
    QFETCH(QString, speed);
    QFETCH(bool, audio);
    QFETCH(double, mbps);
    if (avcodec_find_encoder_by_name(encoder.toUtf8().constData()) == nullptr) QSKIP("encoder not in this build");
    if (avcodec_find_decoder(AVCodecID(codec)) == nullptr) QSKIP("no decoder for this codec");
    amber::EncoderInfo info;
    QVERIFY(amber::find_encoder_info(encoder, &info));
    // libx265 before FFmpeg 8.1 (no x265-stats), libsvtav1 before FFmpeg 8.1 or built against SVT-AV1 < 2.0
    if (!amber::two_pass_works(info)) QSKIP("no two-pass with this encoder in this FFmpeg build (encoderoptions.cpp)");
    constexpr long kFrames = 60;
    SequencePtr seq;
    load_noise_project("twopass_" + encoder, kFrames, &seq);
    if (QTest::currentTestFailed()) return;

    ExportParams params = video_params(dir_.filePath("twopass_" + encoder + "." + ext), seq.get(), codec, encoder,
                                       COMPRESSION_TYPE_TARGETBR, mbps);
    params.two_pass = true;
    if (!speed_key.isEmpty()) params.video_options = {{speed_key, speed}};
    if (audio) {
      params.audio_enabled = true;
      params.audio_codec = AV_CODEC_ID_AAC;
      params.audio_sampling_rate = kAudioRate;
      params.audio_bitrate = 128;
    }
    QString tmp;
    const auto restore_tmp = private_tmpdir(encoder, &tmp);
    // libx264's and libx265's default stats file names are relative to the working directory
    const QStringList cwd_before = dir_entries(QDir::currentPath());

    QVector<QPair<int, int>> passes;
    QVector<int> progress;
    QElapsedTimer timer;
    timer.start();
    const QString error = export_sequence(seq.get(), params, AV_PIX_FMT_YUV420P, [&](ExportThread* et) {
      connect(et, &ExportThread::PassChanged, this, [&passes](int pass, int count) { passes.append({pass, count}); });
      connect(et, &ExportThread::ProgressChanged, this, [&progress](int value, qint64) { progress.append(value); });
    });
    QVERIFY2(error.isEmpty(), qPrintable(error));
    const qint64 elapsed = timer.elapsed();

    QCOMPARE(passes, (QVector<QPair<int, int>>{{1, 2}, {2, 2}}));
    // One value per frame and pass, then the final 100: pass 1 ends on the bar's half
    QCOMPARE(progress.size(), int(2 * kFrames + 1));
    QCOMPARE(progress.at(kFrames - 1), 50);
    QVERIFY(std::is_sorted(progress.begin(), progress.end()));
    QCOMPARE(progress.last(), 100);
    QVERIFY2(dir_entries(tmp).isEmpty(), qPrintable(dir_entries(tmp).join(", ")));
    QCOMPARE(dir_entries(QDir::currentPath()), cwd_before);

    const Decoded d = decode_file(params.filename);
    QVERIFY(d.ok);
    QVERIFY(d.has_video);
    QCOMPARE(long(d.video_frames), kFrames);
    const double target_kbps = mbps * 1000;
    const double kbps = d.video_bytes * 8.0 / (kFrames / kFps) / 1000.0;
    qInfo("%s two-pass: %.1f kbps for a %.0f kbps target (%+.1f %%), %lld ms", qPrintable(encoder), kbps, target_kbps,
          (kbps / target_kbps - 1) * 100, elapsed);
    QVERIFY2(qAbs(kbps - target_kbps) <= 0.3 * target_kbps, qPrintable(QString("%1 kbps").arg(kbps)));

    if (audio) {
      // Pass 2 starts with the audio buffer where a one-pass export starts it
      QVERIFY(d.has_audio);
      const long expected_samples = qRound64(kFrames / kFps * kAudioRate);
      QVERIFY(d.audio_samples >= expected_samples);
      QVERIFY(d.audio_samples < expected_samples + 2 * 1024);
      QVERIFY2(d.audio_peak > 0.25, qPrintable(QString("audio peak %1").arg(d.audio_peak)));
    }
    if (encoder == "libx264") {
      QFile f(params.filename);
      QVERIFY(f.open(QFile::ReadOnly));
      const QByteArray settings = x264_settings(f.readAll());
      QVERIFY2(settings.contains("rc=2pass"), settings.constData());
    }
    for (const auto& c : seq->clips) QVERIFY(!c->IsOpen());
  }

  // Cancelling during the analysis pass: no file, no stats left, no pass 2, the clips closed
  void twoPassInterruptedDuringAnalysis() {
    if (avcodec_find_encoder_by_name("libx264") == nullptr) QSKIP("libx264 not in this FFmpeg build");
    constexpr long kFrames = 60;
    SequencePtr seq;
    load_noise_project("twopass_cancel", kFrames, &seq);
    if (QTest::currentTestFailed()) return;
    ExportParams params = video_params(dir_.filePath("twopass_cancel.mp4"), seq.get(), AV_CODEC_ID_H264, "libx264",
                                       COMPRESSION_TYPE_TARGETBR, 0.3);
    params.two_pass = true;
    params.audio_enabled = true;
    params.audio_codec = AV_CODEC_ID_AAC;
    params.audio_sampling_rate = kAudioRate;
    params.audio_bitrate = 128;
    QString tmp;
    const auto restore_tmp = private_tmpdir("cancel", &tmp);

    int pass = 0;
    int interrupted_at = -1;
    const QString error = export_sequence(seq.get(), params, AV_PIX_FMT_YUV420P, [&](ExportThread* et) {
      connect(et, &ExportThread::PassChanged, this, [&pass](int p, int) { pass = p; });
      // Each frame waits for this thread's event loop (the render wake-up is queued here): the interrupt lands
      // within a frame or two of this one
      connect(et, &ExportThread::ProgressChanged, this, [&, et](int value, qint64) {
        if (pass == 1 && value >= 10 && interrupted_at < 0) {
          interrupted_at = value;
          et->Interrupt();
        }
      });
    });
    qInfo("interrupted at %d %%", interrupted_at);
    QCOMPARE(error, QString("export interrupted"));
    QVERIFY(interrupted_at >= 10 && interrupted_at < 50);
    QCOMPARE(pass, 1);
    QVERIFY(!QFileInfo::exists(params.filename));
    QVERIFY2(dir_entries(tmp).isEmpty(), qPrintable(dir_entries(tmp).join(", ")));
    for (const auto& c : seq->clips) QVERIFY(!c->IsOpen());
  }

  // two_pass is ignored outside Average bitrate and for an encoder without two-pass in its descriptor: the same file
  // as without it, and no pass reported
  void twoPassIgnoredWhereItDoesNotApply() {
    if (avcodec_find_encoder_by_name("libx264") == nullptr) QSKIP("libx264 not in this FFmpeg build");
    SequencePtr seq = solid_sequence(long(kFps));
    auto run = [&](const QString& name, int codec, const QString& encoder, int compression_type, double value,
                   bool two_pass, QByteArray* bytes) {
      ExportParams params = video_params(dir_.filePath(name), seq.get(), codec, encoder, compression_type, value);
      params.two_pass = two_pass;
      int passes = 0;
      const QString error = export_sequence(seq.get(), params, AV_PIX_FMT_YUV420P, [&](ExportThread* et) {
        connect(et, &ExportThread::PassChanged, this, [&passes](int, int) { passes++; });
      });
      QVERIFY2(error.isEmpty(), qPrintable(error));
      QCOMPARE(passes, 0);
      QFile f(params.filename);
      QVERIFY(f.open(QFile::ReadOnly));
      *bytes = f.readAll();
    };
    QByteArray crf, crf_two_pass, mpeg4, mpeg4_two_pass;
    run("ignored_crf.mp4", AV_CODEC_ID_H264, "libx264", COMPRESSION_TYPE_CFR, 30, false, &crf);
    if (QTest::currentTestFailed()) return;
    run("ignored_crf_2pass.mp4", AV_CODEC_ID_H264, "libx264", COMPRESSION_TYPE_CFR, 30, true, &crf_two_pass);
    if (QTest::currentTestFailed()) return;
    run("ignored_mpeg4.mp4", AV_CODEC_ID_MPEG4, QString(), COMPRESSION_TYPE_TARGETBR, 1.0, false, &mpeg4);
    if (QTest::currentTestFailed()) return;
    run("ignored_mpeg4_2pass.mp4", AV_CODEC_ID_MPEG4, QString(), COMPRESSION_TYPE_TARGETBR, 1.0, true, &mpeg4_two_pass);
    if (QTest::currentTestFailed()) return;
    QVERIFY(!crf.isEmpty());
    QVERIFY(crf == crf_two_pass);
    QVERIFY(!mpeg4.isEmpty());
    QVERIFY(mpeg4 == mpeg4_two_pass);
  }

  // Not slots: helpers of the VAAPI tests
 private:
  // A VAAPI encoder that works here (the probe passed): the tests below skip without one (CI has no GPU)
  static bool vaapi_encoder(const QString& name, amber::EncoderInfo* info) {
    return amber::find_encoder_info(name, info) && amber::hardware_encoder_works(*info);
  }

  // A VAAPI export of the noise project at kVaapiSize (scaled up: radeonsi encodes from 128x128, HEVC from 130
  // wide). Returns the decoded file; log gets FFmpeg's log of the export.
  static constexpr int kVaapiSize = 256;
  Decoded vaapi_export(Sequence* seq, const QString& file, int codec, const QString& encoder, int compression_type,
                       double value, AVPixelFormat sw_format, QString* log) {
    ExportParams params = video_params(dir_.filePath(file), seq, codec, encoder, compression_type, value);
    params.video_width = kVaapiSize;
    params.video_height = kVaapiSize;
    QString error;
    {
      AvLogCapture capture;
      error = export_sequence(seq, params, sw_format);
      *log = capture.text();
    }
    if (!error.isEmpty()) {
      qWarning("%s: %s", qPrintable(file), qPrintable(error));
      return Decoded();
    }
    return decode_file(params.filename);
  }

 private slots:
  // H.264 through VAAPI, quality-based: constant QP (rc_mode=CQP), every frame at the export size, and QP 40 a much
  // smaller file than QP 20
  void vaapiQualityMode() {
    amber::EncoderInfo info;
    if (!vaapi_encoder("h264_vaapi", &info)) QSKIP("no working h264_vaapi here");
    if (avcodec_find_decoder(AV_CODEC_ID_H264) == nullptr) QSKIP("no H.264 decoder");
    constexpr long kFrames = 30;
    SequencePtr seq;
    load_noise_project("vaapi_qp", kFrames, &seq);
    if (QTest::currentTestFailed()) return;
    qint64 sizes[2] = {0, 0};
    const int qps[2] = {20, 40};
    for (int i = 0; i < 2; i++) {
      const QString file = QString("vaapi_qp%1.mp4").arg(qps[i]);
      QString log;
      const Decoded d = vaapi_export(seq.get(), file, AV_CODEC_ID_H264, "h264_vaapi", COMPRESSION_TYPE_CFR, qps[i],
                                     AV_PIX_FMT_NV12, &log);
      QVERIFY(d.ok);
      QVERIFY(d.has_video);
      QCOMPARE(long(d.video_frames), kFrames);
      QCOMPARE(d.width, kVaapiSize);
      QCOMPARE(d.height, kVaapiSize);
      QCOMPARE(d.pix_fmt, int(AV_PIX_FMT_YUV420P));
      QVERIFY2(log.contains("RC mode: CQP"), qPrintable(log));
      sizes[i] = QFileInfo(dir_.filePath(file)).size();
    }
    qInfo("h264_vaapi QP 20: %lld bytes, QP 40: %lld bytes", sizes[0], sizes[1]);
    QVERIFY2(sizes[1] * 2 < sizes[0],
             qPrintable(QString("QP 40 %1 bytes, QP 20 %2 bytes").arg(sizes[1]).arg(sizes[0])));
    for (const auto& c : seq->clips) QVERIFY(!c->IsOpen());
  }

  // HEVC through VAAPI at an average bitrate: FFmpeg picks the rate control from bit_rate alone (VBR on radeonsi), and
  // the stream lands within 30 % of the target
  void vaapiAverageBitrate() {
    amber::EncoderInfo info;
    if (!vaapi_encoder("hevc_vaapi", &info)) QSKIP("no working hevc_vaapi here");
    if (avcodec_find_decoder(AV_CODEC_ID_HEVC) == nullptr) QSKIP("no HEVC decoder");
    constexpr long kFrames = 60;
    constexpr double kMbps = 1.0;
    SequencePtr seq;
    load_noise_project("vaapi_abr", kFrames, &seq);
    if (QTest::currentTestFailed()) return;
    QString log;
    const Decoded d = vaapi_export(seq.get(), "vaapi_abr.mp4", AV_CODEC_ID_HEVC, "hevc_vaapi",
                                   COMPRESSION_TYPE_TARGETBR, kMbps, AV_PIX_FMT_NV12, &log);
    QVERIFY(d.ok);
    QVERIFY(d.has_video);
    QCOMPARE(long(d.video_frames), kFrames);
    const QRegularExpressionMatch rc = QRegularExpression("RC mode: (\\w+)").match(log);
    const double target_kbps = kMbps * 1000;
    const double kbps = d.video_bytes * 8.0 / (kFrames / kFps) / 1000.0;
    qInfo("hevc_vaapi average bitrate (RC mode %s): %.1f kbps for a %.0f kbps target (%+.1f %%)",
          qPrintable(rc.captured(1)), kbps, target_kbps, (kbps / target_kbps - 1) * 100);
    QVERIFY(rc.hasMatch());
    QVERIFY(rc.captured(1) != "CQP");
    QVERIFY2(qAbs(kbps - target_kbps) <= 0.3 * target_kbps, qPrintable(QString("%1 kbps").arg(kbps)));
  }

  // HEVC through VAAPI from P010 frames, where the device encodes Main 10: the decoded stream is 10-bit
  void vaapiP010() {
    amber::EncoderInfo info;
    if (!vaapi_encoder("hevc_vaapi", &info)) QSKIP("no working hevc_vaapi here");
    if (!amber::hardware_encoder_formats(info).contains(AV_PIX_FMT_P010)) QSKIP("no HEVC Main 10 encode here");
    if (avcodec_find_decoder(AV_CODEC_ID_HEVC) == nullptr) QSKIP("no HEVC decoder");
    SequencePtr seq = solid_sequence(long(kFps));
    QString log;
    const Decoded d = vaapi_export(seq.get(), "vaapi_p010.mkv", AV_CODEC_ID_HEVC, "hevc_vaapi", COMPRESSION_TYPE_CFR,
                                   25, AV_PIX_FMT_P010, &log);
    QVERIFY(d.ok);
    QCOMPARE(long(d.video_frames), long(kFps));
    const AVPixFmtDescriptor* desc = av_pix_fmt_desc_get(AVPixelFormat(d.pix_fmt));
    QVERIFY(desc != nullptr);
    qInfo("hevc_vaapi P010: decoded as %s", desc->name);
    QCOMPARE(int(desc->comp[0].depth), 10);
    // Red where the solid is opaque, as the software encoders give it
    const int off = ((kVaapiSize / 2) * kVaapiSize + kVaapiSize / 2) * 4;
    const int r = uchar(d.first_rgba[off]), g = uchar(d.first_rgba[off + 1]), b = uchar(d.first_rgba[off + 2]);
    qInfo("first frame centre (%d, %d, %d)", r, g, b);
    QVERIFY(r > 180 && g < 80 && b < 80);
  }

  // Without a usable device (libva pointed at a driver that doesn't exist) the probe says no, and an export naming the
  // encoder anyway (a preset, a batch) ends with the hardware error and leaves no file. Also runs without a GPU.
  void vaapiWithoutDevice() {
    amber::EncoderInfo info;
    if (!amber::find_encoder_info("h264_vaapi", &info)) QSKIP("h264_vaapi not in this FFmpeg build");
    const QByteArray saved = qgetenv("LIBVA_DRIVER_NAME");
    const bool had = qEnvironmentVariableIsSet("LIBVA_DRIVER_NAME");
    qputenv("LIBVA_DRIVER_NAME", "amber_no_such_driver");
    const auto restore = qScopeGuard([&] {
      if (had) {
        qputenv("LIBVA_DRIVER_NAME", saved);
      } else {
        qunsetenv("LIBVA_DRIVER_NAME");
      }
    });
    QVERIFY(!amber::hardware_encode_test("h264_vaapi", AV_HWDEVICE_TYPE_VAAPI, AV_PIX_FMT_NV12));
    SequencePtr seq = solid_sequence(2);
    ExportParams params = video_params(dir_.filePath("vaapi_nodevice.mp4"), seq.get(), AV_CODEC_ID_H264, "h264_vaapi",
                                       COMPRESSION_TYPE_CFR, 20);
    params.video_width = kVaapiSize;
    params.video_height = kVaapiSize;
    const QString error = export_sequence(seq.get(), params, AV_PIX_FMT_NV12);
    qInfo("export error: %s", qPrintable(error));
    QVERIFY2(error.startsWith("hardware encoder failed: could not open the VAAPI device"), qPrintable(error));
    QVERIFY(!QFileInfo::exists(params.filename));
  }

  // FLAC at compression level 0 and 12: the same samples, bit for bit, and the higher level no larger
  void flacCompressionLevel() {
    if (avcodec_find_encoder_by_name("flac") == nullptr) QSKIP("flac encoder not in this FFmpeg build");
    const QString video = dir_.filePath("flac_checkerboard.y4m");
    {
      QFile f(video);
      QVERIFY(f.open(QFile::WriteOnly | QFile::Truncate));
      f.write(checkerboard_y4m());
    }
    const QString audio = dir_.filePath("flac_tone.wav");
    {
      QFile f(audio);
      QVERIFY(f.open(QFile::WriteOnly | QFile::Truncate));
      f.write(sine_wav());
    }
    const QString project = dir_.filePath("flac.ove");
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

    auto run = [&](const QString& level, Decoded* d, qint64* size) {
      ExportParams params{};
      params.filename = dir_.filePath("flac_" + level + ".mkv");
      params.video_enabled = false;
      params.audio_enabled = true;
      params.audio_codec = AV_CODEC_ID_FLAC;
      params.audio_encoder = "flac";
      params.audio_options = {{"compression_level", level}};
      params.audio_sampling_rate = kAudioRate;
      params.audio_bitrate = 1536;  // ignored by FLAC
      params.start_frame = 0;
      params.end_frame = seq->getEndFrame();
      const QString error = export_sequence(seq.get(), params);
      QVERIFY2(error.isEmpty(), qPrintable(error));
      *d = decode_file(params.filename);
      *size = QFileInfo(params.filename).size();
    };
    Decoded l0, l12;
    qint64 size0 = 0, size12 = 0;
    run("0", &l0, &size0);
    if (QTest::currentTestFailed()) return;
    run("12", &l12, &size12);
    if (QTest::currentTestFailed()) return;
    qInfo("FLAC level 0: %lld bytes, %ld samples per channel; level 12: %lld bytes, %ld samples per channel", size0,
          l0.audio_samples, size12, l12.audio_samples);
    QVERIFY(l0.ok && l12.ok);
    QVERIFY(l0.has_audio && l12.has_audio);
    // The export runs to the end of the encoder frame holding the last sample, and FLAC's frame size grows with the
    // level (1152 samples at 0, 4096 at 12): compare the export's span, which both files cover
    const qsizetype span_bytes = qRound64((seq->getEndFrame() / kFps) * kAudioRate) * 2 * 2;
    QVERIFY(l0.audio_s16.size() >= span_bytes);
    QVERIFY(l12.audio_s16.size() >= span_bytes);
    QVERIFY(l0.audio_s16.left(span_bytes) == l12.audio_s16.left(span_bytes));
    const qsizetype common = qMin(l0.audio_s16.size(), l12.audio_s16.size());
    QVERIFY(l0.audio_s16.left(common) == l12.audio_s16.left(common));
    QVERIFY2(size12 <= size0, qPrintable(QString("level 12 %1 > level 0 %2").arg(size12).arg(size0)));
  }

  // Fast start puts the MP4 index (moov) before the media data (mdat); without it the index comes last
  void mp4FastStart() {
    const long frames = 10;
    SequencePtr seq = solid_sequence(frames);
    auto run = [&](bool faststart, qint64* moov, qint64* mdat) {
      ExportParams params = video_params(dir_.filePath(faststart ? "faststart.mp4" : "plain.mp4"), seq.get(),
                                         AV_CODEC_ID_MPEG4, QString(), COMPRESSION_TYPE_CBR, 2.0);
      params.faststart = faststart;
      const QString error = export_sequence(seq.get(), params);
      QVERIFY2(error.isEmpty(), qPrintable(error));
      QFile f(params.filename);
      QVERIFY(f.open(QFile::ReadOnly));
      const QByteArray bytes = f.readAll();
      *moov = mp4_box_offset(bytes, "moov");
      *mdat = mp4_box_offset(bytes, "mdat");
      const Decoded d = decode_file(params.filename);
      QVERIFY(d.ok);
      QCOMPARE(long(d.video_frames), frames);
    };
    qint64 fast_moov = -1, fast_mdat = -1, plain_moov = -1, plain_mdat = -1;
    run(true, &fast_moov, &fast_mdat);
    if (QTest::currentTestFailed()) return;
    run(false, &plain_moov, &plain_mdat);
    if (QTest::currentTestFailed()) return;
    qInfo("fast start: moov at %lld, mdat at %lld; without: moov at %lld, mdat at %lld", fast_moov, fast_mdat,
          plain_moov, plain_mdat);
    QVERIFY(fast_moov >= 0 && fast_mdat >= 0 && plain_moov >= 0 && plain_mdat >= 0);
    QVERIFY(fast_moov < fast_mdat);
    QVERIFY(plain_mdat < plain_moov);
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

  // An image sequence leaves its frames and nothing else: image2 opens each frame's file itself, so no empty file
  // named after the pattern ("frame%05d.png")
  void exportImageSequenceWritesOnlyFrames() {
    if (avcodec_find_encoder(AV_CODEC_ID_PNG) == nullptr) QSKIP("PNG encoder not in this FFmpeg build");
    SequencePtr seq = solid_sequence(3);
    const QString folder = dir_.filePath("image_sequence");
    QVERIFY(QDir().mkpath(folder));
    ExportParams params;
    params.filename = QDir(folder).filePath("frame%05d.png");
    params.video_enabled = true;
    params.video_codec = AV_CODEC_ID_PNG;
    params.video_width = kSize;
    params.video_height = kSize;
    params.video_compression_type = COMPRESSION_TYPE_CBR;
    params.video_bitrate = 20.0;  // Mbps
    params.audio_enabled = false;
    params.start_frame = 0;
    params.end_frame = 3;
    const QString error = export_sequence(seq.get(), params, AV_PIX_FMT_RGB24);
    QVERIFY2(error.isEmpty(), qPrintable(error));

    const QStringList entries = dir_entries(folder);
    qInfo("image sequence: %s", qPrintable(entries.join(", ")));
    QCOMPARE(entries, QStringList({"frame00001.png", "frame00002.png", "frame00003.png"}));
    for (const QString& f : entries) {
      QCOMPARE(QImage(QDir(folder).filePath(f)).size(), QSize(kSize, kSize));
    }
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

  // Batch export: each sequence at its own size, length and frame rate, the in/out range of the one that has it, as
  // the active sequence while it exports; the user's active sequence and every playhead back at the end
  void batchExportEachSequenceAtItsOwnSettings() {
    BatchSequences b = batch_sequences();
    pio_->setSequence(b.user);
    const QString folder = dir_.filePath("batch_own_settings");
    QVERIFY(QDir().mkpath(folder));

    QVector<BatchExport::Job> jobs = batch_jobs({b.wide, b.tall}, folder, true);
    QCOMPARE(jobs.at(0).params.video_width, 64);
    QCOMPARE(jobs.at(0).params.video_height, 48);
    QCOMPARE(jobs.at(0).params.start_frame, 0L);
    QCOMPARE(jobs.at(0).params.end_frame, 10L);  // no in/out range: whole
    QCOMPARE(jobs.at(1).params.video_width, 32);
    QCOMPARE(jobs.at(1).params.video_height, 64);
    QCOMPARE(jobs.at(1).params.start_frame, 1L);
    QCOMPARE(jobs.at(1).params.end_frame, 4L);
    // "Entire Sequence": the in/out range is ignored
    const ExportParams whole = amber::batch_export_params(batch_base(), b.tall.get(), false, "x.mp4");
    QCOMPARE(whole.start_frame, 0L);
    QCOMPARE(whole.end_frame, 6L);

    BatchExport batch(jobs);
    QVector<SequencePtr> active;
    QVector<long> playheads;
    QVERIFY(run_batch(batch, [&](int index) {
      active.append(amber::ActiveSequence);
      playheads.append(jobs.at(index).seq->playhead);
    }));
    QCOMPARE(active, (QVector<SequencePtr>{b.wide, b.tall}));
    QCOMPARE(playheads, (QVector<long>{0, 1}));  // prepare_export() put them at the export start
    QCOMPARE(batch.ExportedCount(), 2);
    QVERIFY(!batch.WasCancelled());
    QCOMPARE(batch.Summary("/out"), QString("2 sequences exported to /out"));

    // The user's sequence and playheads back, out of rendering mode
    QCOMPARE(amber::ActiveSequence, b.user);
    QCOMPARE(b.user->playhead, 3L);
    QCOMPARE(b.wide->playhead, 7L);
    QCOMPARE(b.tall->playhead, 2L);
    QVERIFY(!audio_rendering);

    QCOMPARE(dir_entries(folder), (QStringList{"Tall.mp4", "Wide.mp4"}));
    const Decoded wide = decode_file(QDir(folder).filePath("Wide.mp4"));
    QVERIFY(wide.ok && wide.has_video);
    QCOMPARE(wide.width, 64);
    QCOMPARE(wide.height, 48);
    QCOMPARE(wide.video_frames, 10);
    QVERIFY2(qAbs(wide.video_fps - 30.0) < 0.001, qPrintable(QString::number(wide.video_fps)));
    const Decoded tall = decode_file(QDir(folder).filePath("Tall.mp4"));
    QVERIFY(tall.ok && tall.has_video);
    QCOMPARE(tall.width, 32);
    QCOMPARE(tall.height, 64);
    QCOMPARE(tall.video_frames, 3);
    QVERIFY2(qAbs(tall.video_fps - 25.0) < 0.001, qPrintable(QString::number(tall.video_fps)));

    // Again into the same folder: new names, nothing overwritten
    QVector<BatchExport::Job> again = batch_jobs({b.wide, b.tall}, folder, true);
    QCOMPARE(QFileInfo(again.at(0).params.filename).fileName(), QString("Wide (2).mp4"));
    QCOMPARE(QFileInfo(again.at(1).params.filename).fileName(), QString("Tall (2).mp4"));
    pio_->setSequence(nullptr);
  }

  // A sequence that fails (an encoder that doesn't exist) or can't start (no clips) doesn't stop the others. Started
  // with no active sequence (the Project panel's menu needs none), the batch leaves none.
  void batchExportContinuesPastFailures() {
    BatchSequences b = batch_sequences();
    SequencePtr empty = std::make_shared<Sequence>();
    empty->name = "Empty";
    empty->width = kSize;
    empty->height = kSize;
    empty->frame_rate = kFps;
    empty->audio_frequency = kAudioRate;
    pio_->setSequence(nullptr);
    const QString folder = dir_.filePath("batch_failures");
    QVERIFY(QDir().mkpath(folder));

    QVector<BatchExport::Job> jobs = batch_jobs({b.wide, empty, b.tall}, folder, false);
    jobs[0].params.video_encoder = "amber_no_such_encoder";
    BatchExport batch(jobs);
    QVector<int> started;
    QVERIFY(run_batch(batch, [&](int index) { started.append(index); }));
    QCOMPARE(started, (QVector<int>{0, 2}));

    const QVector<BatchExport::Outcome>& o = batch.Outcomes();
    QCOMPARE(o.at(0).status, BatchExport::Outcome::Failed);
    QVERIFY2(o.at(0).reason.contains("amber_no_such_encoder"), qPrintable(o.at(0).reason));
    QCOMPARE(o.at(1).status, BatchExport::Outcome::Skipped);
    QCOMPARE(o.at(2).status, BatchExport::Outcome::Exported);
    const QString summary = batch.Summary("/out");
    qInfo("%s", qPrintable(summary));
    QVERIFY(summary.startsWith("1 of 3 exported to /out."));
    QVERIFY(summary.contains("Wide failed: " + o.at(0).reason));
    QVERIFY(summary.contains("Empty skipped: "));

    QCOMPARE(dir_entries(folder), QStringList{"Tall.mp4"});  // the failed export left nothing
    const Decoded tall = decode_file(QDir(folder).filePath("Tall.mp4"));
    QVERIFY(tall.ok);
    QCOMPARE(tall.video_frames, 6);
    QCOMPARE(amber::ActiveSequence, SequencePtr());
    QCOMPARE(b.wide->playhead, 7L);
    QCOMPARE(b.tall->playhead, 2L);
    QVERIFY(!audio_rendering);
    pio_->setSequence(nullptr);
  }

  // An image sequence of a name ending in digits, exported twice into the same folder: the frame files of the first
  // ("Sequence 0100001.png"...) take its name, so the second gets " (2)" and the first's frames stay as they were
  void batchExportImageSequenceKeepsExistingFrames() {
    SequencePtr seq = solid_sequence(3);
    seq->name = "Sequence 01";
    const QString folder = dir_.filePath("batch_image_sequence");
    QVERIFY(QDir().mkpath(folder));
    VideoCodecParams vparams = yuv420p_params();
    vparams.pix_fmt = AV_PIX_FMT_RGB24;

    {
      BatchExport batch(batch_jobs({seq}, folder, false, true, vparams));
      QVERIFY(run_batch(batch));
      QCOMPARE(batch.ExportedCount(), 1);
    }
    // Every entry is a frame: no file under the pattern's own name (exportImageSequenceWritesOnlyFrames())
    const auto frames = [&folder]() { return dir_entries(folder); };
    qInfo("first export: %s", qPrintable(dir_entries(folder).join(", ")));
    const QStringList first = frames();
    QCOMPARE(first.size(), 3);
    for (const QString& f : first) {
      QVERIFY2(QRegularExpression("^Sequence 01\\d{5}\\.png$").match(f).hasMatch(), qPrintable(f));
    }
    QMap<QString, QByteArray> before;
    for (const QString& f : first) {
      QFile file(QDir(folder).filePath(f));
      QVERIFY(file.open(QFile::ReadOnly));
      before.insert(f, file.readAll());
    }

    seq->clips.first()->effects.at(1)->row(2)->Field(0)->SetValueAt(0.0, QVariant(QColor(Qt::blue)));  // Solid colour
    QVector<BatchExport::Job> again = batch_jobs({seq}, folder, false, true, vparams);
    QCOMPARE(QFileInfo(again.at(0).params.filename).fileName(), QString("Sequence 01 (2)%05d.png"));
    {
      BatchExport batch(again);
      QVERIFY(run_batch(batch));
      QCOMPARE(batch.ExportedCount(), 1);
    }
    qInfo("after the second export: %s", qPrintable(dir_entries(folder).join(", ")));
    const QStringList second = frames();
    QCOMPARE(second.size(), 6);
    for (auto it = before.cbegin(); it != before.cend(); ++it) {
      QFile file(QDir(folder).filePath(it.key()));
      QVERIFY(file.open(QFile::ReadOnly));
      QVERIFY2(file.readAll() == it.value(), qPrintable(it.key() + " was overwritten"));
    }
    pio_->setSequence(nullptr);
  }

  // Cancel stops the running export (its file removed) and skips the rest; the user's sequence comes back
  void batchExportCancel() {
    BatchSequences b = batch_sequences();
    pio_->setSequence(b.user);
    const QString folder = dir_.filePath("batch_cancel");
    QVERIFY(QDir().mkpath(folder));

    BatchExport batch(batch_jobs({b.wide, b.tall}, folder, false));
    connect(&batch, &BatchExport::ProgressChanged, &batch, &BatchExport::Cancel);
    QVector<int> started;
    QVERIFY(run_batch(batch, [&](int index) { started.append(index); }));
    QCOMPARE(started, QVector<int>{0});
    QVERIFY(batch.WasCancelled());
    QCOMPARE(batch.ExportedCount(), 0);
    QCOMPARE(batch.Outcomes().at(0).status, BatchExport::Outcome::Cancelled);
    QCOMPARE(batch.Outcomes().at(1).status, BatchExport::Outcome::Cancelled);
    QCOMPARE(dir_entries(folder), QStringList{});
    QCOMPARE(amber::ActiveSequence, b.user);
    QCOMPARE(b.wide->playhead, 7L);
    QVERIFY(!audio_rendering);
    pio_->setSequence(nullptr);
  }

  // Each job its own codec and Advanced settings: an MPEG-4 file in YUV 4:2:0 and a PNG image sequence in RGB, each
  // pixel format refused by the other's encoder (exportUnsupportedPixelFormatFails()): shared settings would fail one
  void batchExportPerJobCodecs() {
    if (avcodec_find_encoder(AV_CODEC_ID_PNG) == nullptr) QSKIP("PNG encoder not in this FFmpeg build");
    BatchSequences b = batch_sequences();
    const QString folder = dir_.filePath("batch_per_job_codecs");
    QVERIFY(QDir().mkpath(folder));
    VideoCodecParams rgb = yuv420p_params();
    rgb.pix_fmt = AV_PIX_FMT_RGB24;
    QVector<BatchExport::Job> jobs = batch_jobs({b.wide}, folder, false);
    jobs += batch_jobs({b.tall}, folder, false, true, rgb);

    BatchExport batch(jobs);
    QVERIFY(run_batch(batch));
    for (const BatchExport::Outcome& o : batch.Outcomes()) {
      QVERIFY2(o.status == BatchExport::Outcome::Exported, qPrintable(o.name + ": " + o.reason));
    }

    const Decoded wide = decode_file(QDir(folder).filePath("Wide.mp4"));
    QVERIFY(wide.ok && wide.has_video);
    QCOMPARE(wide.video_codec, int(AV_CODEC_ID_MPEG4));
    QCOMPARE(wide.pix_fmt, int(AV_PIX_FMT_YUV420P));
    QCOMPARE(wide.video_frames, 10);
    const Decoded tall = decode_file(jobs.at(1).params.filename);  // the image2 pattern
    QVERIFY(tall.ok && tall.has_video);
    QCOMPARE(tall.video_codec, int(AV_CODEC_ID_PNG));
    QCOMPARE(tall.pix_fmt, int(AV_PIX_FMT_RGB24));
    QCOMPARE(tall.video_frames, 6);
    pio_->setSequence(nullptr);
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
