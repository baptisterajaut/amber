#include "tests/test_render_harness.h"

#include <QApplication>
#include <QFile>
#include <QOffscreenSurface>
#include <QTest>
#include <QtMath>

#include "core/appcontext.h"
#include "effects/effect.h"
#include "effects/effectfield.h"
#include "effects/effectloaders.h"
#include "effects/effectrow.h"
#include "engine/clip.h"
#include "engine/sequence_fwd.h"
#include "global/config.h"
#include "project/footage.h"
#include "project/media.h"
#include "rendering/audio.h"
#include "rendering/renderfunctions.h"

// External entry points from effectloaders.cpp (no header export).
extern void load_internal_effects();
extern void load_shader_effects();

namespace {

QShader loadQsb(const QString& path) {
  QFile f(path);
  if (!f.open(QIODevice::ReadOnly)) {
    qFatal("Failed to load shader: %s", qPrintable(path));
  }
  return QShader::fromSerialized(f.readAll());
}

}  // namespace

TestRenderHarness::TestRenderHarness() {
  // 1. Set the env var BEFORE shader effect discovery so XML effects from the
  //    source tree are found regardless of where the test binary lives.
  qputenv("AMBER_EFFECTS_PATH", QByteArray(SOURCE_DIR "/effects/shaders"));

  // 2. CurrentConfig defaults (used by various effects via global)
  amber::CurrentConfig = {};

  // 3. Install the AppContext stub. Required BEFORE effect loading because
  //    Effect::Create's error path calls amber::app_ctx->showMessage().
  amber::app_ctx = &app_ctx_;

  // 4. Audio ring buffer — already statically allocated; just zero it.
  //    DO NOT call init_audio() — it opens a real QAudioSink and starts
  //    AudioSenderThread, which is unwanted in headless tests
  //    (rendering/audio.cpp:88-126).
  clear_audio_ibuffer();

  // 5. Effects registry (internal + XML). Call the loaders synchronously to
  //    avoid the EffectInit background thread (and to skip Frei0r discovery
  //    side effects that we don't need here).
  if (effects.isEmpty()) {
    load_internal_effects();
    load_shader_effects();
  }

  // 6. QRhi(OpenGLES2) with fallback surface.
  QRhiGles2InitParams params;
  fallback_surface_.reset(QRhiGles2InitParams::newFallbackSurface());
  params.fallbackSurface = fallback_surface_.get();
  rhi_.reset(QRhi::create(QRhi::OpenGLES2, &params));
  if (!rhi_) {
    qWarning("QRhi OpenGLES2 unavailable; tests will QSKIP");
    return;
  }

  // 7. Core shaders + shared sampler.
  load_core_shaders();
  sampler_ = rhi_->newSampler(QRhiSampler::Linear, QRhiSampler::Linear, QRhiSampler::None, QRhiSampler::ClampToEdge,
                              QRhiSampler::ClampToEdge);
  sampler_->create();
}

TestRenderHarness::~TestRenderHarness() {
  amber::ActiveSequence = nullptr;
  if (rhi_) {
    release_buffers();
    delete sampler_;
    sampler_ = nullptr;
  }
  rhi_.reset();
  fallback_surface_.reset();
  amber::app_ctx = nullptr;
}

void TestRenderHarness::load_core_shaders() {
  passthrough_vert_ = loadQsb(QStringLiteral(":/shaders/common.vert.qsb"));
  passthrough_frag_ = loadQsb(QStringLiteral(":/shaders/passthrough.frag.qsb"));
  blending_frag_ = loadQsb(QStringLiteral(":/shaders/blending.frag.qsb"));
  premultiply_frag_ = loadQsb(QStringLiteral(":/shaders/premultiply.frag.qsb"));
  yuv_frag_ = loadQsb(QStringLiteral(":/shaders/yuv2rgb.frag.qsb"));
}

void TestRenderHarness::ensure_buffers(int w, int h) {
  if (w == buf_w_ && h == buf_h_ && main_tex_) return;
  release_buffers();
  buf_w_ = w;
  buf_h_ = h;

  // main_tex: PreserveColorContents-capable (RenderTarget + UsedAsTransferSource for readback)
  main_tex_ = rhi_->newTexture(QRhiTexture::RGBA8, QSize(w, h), 1,
                               QRhiTexture::RenderTarget | QRhiTexture::UsedAsTransferSource);
  main_tex_->create();

  // main_target with PreserveColorContents flag (LoadOp::Load)
  QRhiColorAttachment att(main_tex_);
  main_target_ = rhi_->newTextureRenderTarget({att}, QRhiTextureRenderTarget::PreserveColorContents);
  main_rpd_ = main_target_->newCompatibleRenderPassDescriptor();
  main_target_->setRenderPassDescriptor(main_rpd_);
  main_target_->create();

  // main_target_clear: same texture, NO preserve flag — used for the initial clear pass.
  // (beginPass(clearColor) on a PreserveColorContents target is a no-op.)
  main_target_clear_ = rhi_->newTextureRenderTarget({att});
  main_target_clear_->setRenderPassDescriptor(main_rpd_);
  main_target_clear_->create();

  // backend_tex: scratch, no preserve.
  backend_tex_ = rhi_->newTexture(QRhiTexture::RGBA8, QSize(w, h), 1, QRhiTexture::RenderTarget);
  backend_tex_->create();
  QRhiColorAttachment batt(backend_tex_);
  backend_target_ = rhi_->newTextureRenderTarget({batt});
  backend_rpd_ = backend_target_->newCompatibleRenderPassDescriptor();
  backend_target_->setRenderPassDescriptor(backend_rpd_);
  backend_target_->create();
}

void TestRenderHarness::release_buffers() {
  delete main_target_clear_;
  main_target_clear_ = nullptr;
  delete main_target_;
  main_target_ = nullptr;
  delete main_rpd_;
  main_rpd_ = nullptr;
  delete main_tex_;
  main_tex_ = nullptr;
  delete backend_target_;
  backend_target_ = nullptr;
  delete backend_rpd_;
  backend_rpd_ = nullptr;
  delete backend_tex_;
  backend_tex_ = nullptr;
  buf_w_ = buf_h_ = 0;
}

SequencePtr TestRenderHarness::make_sequence(int w, int h, double fps) {
  auto seq = std::make_shared<Sequence>();
  seq->name = "Test Sequence";
  seq->width = w;
  seq->height = h;
  seq->frame_rate = fps;
  seq->audio_frequency = 48000;
  seq->audio_layout = 3;  // stereo, matches AV_CH_LAYOUT_STEREO
  seq->save_id = 0;       // Sequence::save_id is uninitialised in the default ctor; set explicitly.

  // Several effects (e.g. TimecodeEffect::redraw) read amber::ActiveSequence
  // directly instead of the compose_sequence params — install it so they don't
  // null-deref. Tests assume a single sequence at a time; the harness owns it.
  amber::ActiveSequence = seq;
  return seq;
}

Clip* TestRenderHarness::add_generator_clip(Sequence* seq, int track, long in, long out, EffectInternal generator) {
  ClipPtr c = std::make_shared<Clip>(seq);
  c->set_media(nullptr, 0);
  c->set_timeline_in(in);
  c->set_timeline_out(out);
  c->set_clip_in(0);
  c->set_track(track);  // NEGATIVE = video (-1 = topmost). See clip.cpp:203.
  c->set_name("Generator");

  // All internal effects are registered as EFFECT_TYPE_EFFECT (see
  // effectloaders.cpp:50); EFFECT_TYPE_TRANSITION is for transitions only.
  const EffectMeta* transform_meta = Effect::GetInternalMeta(EFFECT_INTERNAL_TRANSFORM, EFFECT_TYPE_EFFECT);
  Q_ASSERT(transform_meta);
  c->effects.append(Effect::Create(c.get(), transform_meta));

  const EffectMeta* gen_meta = Effect::GetInternalMeta(generator, EFFECT_TYPE_EFFECT);
  Q_ASSERT(gen_meta);
  c->effects.append(Effect::Create(c.get(), gen_meta));

  seq->clips.append(c);
  return c.get();
}

EffectPtr TestRenderHarness::attach_effect(Clip* clip, const EffectMeta* meta) {
  EffectPtr eff = Effect::Create(clip, meta);
  clip->effects.append(eff);
  return eff;
}

Media* TestRenderHarness::import_video_media(const QString& path, int width, int height, double frame_rate) {
  Q_ASSERT_X(QFile::exists(path), "import_video_media", qPrintable("fixture not found: " + path));
  MediaPtr m = std::make_shared<Media>();
  FootagePtr f = std::make_shared<Footage>();
  f->url = path;
  f->ready = true;
  f->invalid = false;
  f->using_inout = false;

  // Footage::length is in AV_TIME_BASE (microsecond) units. The ctor leaves it
  // value-initialised to 0, which makes Clip::media_length() return 0 and
  // IsActiveAt(playhead) always false — compose_sequence then never calls
  // Clip::Open() and the Cacher never decodes the file. Production code path
  // is PreviewGenerator → m->length; in the test build that path is stubbed,
  // so we set length here. 30 frames @ frame_rate seconds is enough for the
  // [0, 30) clip range used by the YUV regression test.
  f->length = static_cast<int64_t>((30.0 / frame_rate) * AV_TIME_BASE);

  // Footage ctor calls ready_lock.lock() (footage.cpp:41) expecting PreviewGenerator
  // to unlock once analysis completes. With PreviewGenerator stubbed in tests,
  // unlock it ourselves so ~Footage() doesn't warn "destroying locked mutex".
  f->ready_lock.unlock();

  FootageStream s{};
  s.file_index = 0;
  s.video_width = width;
  s.video_height = height;
  s.video_frame_rate = frame_rate;
  s.video_interlacing = VIDEO_PROGRESSIVE;
  s.infinite_length = false;
  s.enabled = true;
  f->video_tracks.append(s);

  m->set_footage(f);
  owned_media_.append(m);
  return m.get();
}

Clip* TestRenderHarness::add_video_clip(Sequence* seq, long in, long out, Media* media) {
  ClipPtr c = std::make_shared<Clip>(seq);
  c->set_media(media, /*stream=*/0);
  c->set_timeline_in(in);
  c->set_timeline_out(out);
  c->set_clip_in(0);
  c->set_track(-1);
  c->set_name("VideoClip");

  // Transform on top so compose_sequence's CoordsFlag path runs and the clip
  // texture is composited to the main target.
  const EffectMeta* transform_meta = Effect::GetInternalMeta(EFFECT_INTERNAL_TRANSFORM, EFFECT_TYPE_EFFECT);
  Q_ASSERT(transform_meta);
  c->effects.append(Effect::Create(c.get(), transform_meta));

  seq->clips.append(c);
  return c.get();
}

EffectPtr TestRenderHarness::attach_internal(Clip* clip, EffectInternal id, int type) {
  const EffectMeta* meta = Effect::GetInternalMeta(id, type);
  Q_ASSERT(meta);
  return attach_effect(clip, meta);
}

EffectPtr TestRenderHarness::attach_xml_effect(Clip* clip, const QString& effect_name) {
  const EffectMeta* meta = get_meta_from_name(effect_name);
  Q_ASSERT_X(meta, "attach_xml_effect", qPrintable(effect_name));
  return attach_effect(clip, meta);
}

EffectPtr TestRenderHarness::attach_xml_shader(Clip* clip, const QString& name) {
  for (int i = 0; i < effects.size(); ++i) {
    // Skip internal effects. The audio Noise registers with name "Noise" and
    // internal=EFFECT_INTERNAL_NOISE (>= 0); the XML shader Noise has
    // internal == -1. Without this filter the audio one wins.
    if (effects[i].internal != -1) continue;
    if (effects[i].name == name) {
      return attach_effect(clip, &effects[i]);
    }
  }
  Q_ASSERT_X(false, "attach_xml_shader", qPrintable(QString("effect not found: ") + name));
  return nullptr;
}

void TestRenderHarness::set_field_double(Effect* e, int row, int field, double value) {
  e->row(row)->Field(field)->SetValueAt(0.0, QVariant(value));
}

void TestRenderHarness::set_field_color(Effect* e, int row, int field, QColor value) {
  e->row(row)->Field(field)->SetValueAt(0.0, QVariant(value));
}

void TestRenderHarness::set_field_bool(Effect* e, int row, int field, bool value) {
  e->row(row)->Field(field)->SetValueAt(0.0, QVariant(value));
}

void TestRenderHarness::set_field_string(Effect* e, int row, int field, const QString& value) {
  e->row(row)->Field(field)->SetValueAt(0.0, QVariant(value));
}

void TestRenderHarness::set_field_combo(Effect* e, int row, int field, int index) {
  e->row(row)->Field(field)->SetValueAt(0.0, QVariant(index));
}

QByteArray TestRenderHarness::render_frame(Sequence* seq, long playhead) {
  if (!rhi_) {
    QTest::qFail("render_frame called but QRhi unavailable", __FILE__, __LINE__);
    return {};
  }
  ensure_buffers(seq->width, seq->height);
  seq->playhead = playhead;

  QRhiCommandBuffer* cb = nullptr;
  if (rhi_->beginOffscreenFrame(&cb) != QRhi::FrameOpSuccess) {
    QTest::qFail("beginOffscreenFrame failed", __FILE__, __LINE__);
    return {};
  }

  // Initial clear of main_tex via the non-preserve alias.
  cb->beginPass(main_target_clear_, QColor(0, 0, 0, 0), {1.0f, 0});
  cb->endPass();

  // Build params, mirroring RenderThread::paint().
  ComposeSequenceParams params;
  params.rhi = rhi_.get();
  params.cb = cb;
  params.seq = seq;
  params.video = true;
  params.scrubbing = false;
  params.playback_speed = 1;
  params.wait_for_mutexes = true;
  params.texture_failed = false;
  params.gizmos = nullptr;
  params.sampler = sampler_;
  params.passthroughVert = passthrough_vert_;
  params.passthroughFrag = passthrough_frag_;
  params.blendingFrag = blending_frag_;
  params.premultiplyFrag = premultiply_frag_;
  params.yuvFrag = yuv_frag_;
  params.main_tex = main_tex_;
  params.main_target = main_target_;
  params.main_rpd = main_rpd_;
  params.backend_tex1 = backend_tex_;
  params.backend_target1 = backend_target_;
  params.backend_rpd = backend_rpd_;

  amber::rendering::compose_sequence(params);

  // Read back main_tex.
  QRhiReadbackResult readback;
  bool done = false;
  readback.completed = [&done] { done = true; };
  QRhiResourceUpdateBatch* u = rhi_->nextResourceUpdateBatch();
  u->readBackTexture(QRhiReadbackDescription(main_tex_), &readback);
  cb->resourceUpdate(u);

  rhi_->endOffscreenFrame();  // blocks until GPU done + readback complete

  // Now safe to delete transient pipelines/SRBs/UBOs that compose_sequence created.
  // Production code does the same drain in DeferRhiResourceDeletion's consumer.
  qDeleteAll(params.transientResources);
  params.transientResources.clear();

  if (!done || readback.data.isEmpty()) {
    QTest::qFail("readback failed", __FILE__, __LINE__);
    return {};
  }
  return readback.data;
}

void TestRenderHarness::assert_pixel(const QByteArray& pixels, int w, int x, int y, QColor expected, int tolerance) {
  const int stride = w * 4;
  const int offset = y * stride + x * 4;
  QVERIFY2(pixels.size() >= offset + 4, "pixel offset out of range");
  const uchar* p = reinterpret_cast<const uchar*>(pixels.constData()) + offset;
  const int r = p[0], g = p[1], b = p[2], a = p[3];
  QVERIFY2(
      qAbs(r - expected.red()) <= tolerance,
      qPrintable(
          QString("pixel (%1,%2) R=%3 expected %4 +/-%5").arg(x).arg(y).arg(r).arg(expected.red()).arg(tolerance)));
  QVERIFY2(
      qAbs(g - expected.green()) <= tolerance,
      qPrintable(
          QString("pixel (%1,%2) G=%3 expected %4 +/-%5").arg(x).arg(y).arg(g).arg(expected.green()).arg(tolerance)));
  QVERIFY2(
      qAbs(b - expected.blue()) <= tolerance,
      qPrintable(
          QString("pixel (%1,%2) B=%3 expected %4 +/-%5").arg(x).arg(y).arg(b).arg(expected.blue()).arg(tolerance)));
  QVERIFY2(
      qAbs(a - expected.alpha()) <= tolerance,
      qPrintable(
          QString("pixel (%1,%2) A=%3 expected %4 +/-%5").arg(x).arg(y).arg(a).arg(expected.alpha()).arg(tolerance)));
}

void TestRenderHarness::assert_solid_color(const QByteArray& pixels, int w, int h, QColor expected, int tolerance) {
  // Spot-check 9 points (corners + edge midpoints + center) — full-image scan
  // is overkill for a "did everything paint" assertion and slow on large frames.
  const int xs[] = {0, w / 2, w - 1};
  const int ys[] = {0, h / 2, h - 1};
  for (int y : ys) {
    for (int x : xs) {
      assert_pixel(pixels, w, x, y, expected, tolerance);
    }
  }
}

Clip* TestRenderHarness::add_audio_generator_clip(Sequence* seq, int track, long in, long out,
                                                  EffectInternal generator) {
  Q_ASSERT(track >= 0);  // Olive convention: track >= 0 == audio. See clip.cpp:203.
  ClipPtr c = std::make_shared<Clip>(seq);
  c->set_media(nullptr, 0);
  c->set_timeline_in(in);
  c->set_timeline_out(out);
  c->set_clip_in(0);
  c->set_track(track);
  c->set_name("Audio Generator");

  // All internal audio effects register with type == EFFECT_TYPE_EFFECT (their
  // subtype is EFFECT_TYPE_AUDIO) — see effectloaders.cpp:50-75. The
  // GetInternalMeta helper filters on `type`, not `subtype`, so we pass
  // EFFECT_TYPE_EFFECT here just like the video path.
  const EffectMeta* gen_meta = Effect::GetInternalMeta(generator, EFFECT_TYPE_EFFECT);
  Q_ASSERT(gen_meta);
  c->effects.append(Effect::Create(c.get(), gen_meta));

  seq->clips.append(c);
  return c.get();
}

QByteArray TestRenderHarness::render_audio(Clip* clip, double timecode_start, int duration_ms) {
  Q_ASSERT(clip != nullptr);
  Q_ASSERT(clip->sequence != nullptr);
  const int sample_rate = clip->sequence->audio_frequency;
  const int channels = 2;
  const int frame_count = duration_ms * sample_rate / 1000;
  const int byte_count = frame_count * channels * static_cast<int>(sizeof(qint16));

  // Zero-init: generators with "mix" enabled start from silence so their
  // output equals the generated signal alone.
  QByteArray buf(byte_count, '\0');
  const double timecode_end = timecode_start + duration_ms / 1000.0;

  // Walk the effect chain in order — same contract as the production
  // Cacher::CacheAudioWorker, minus the threading and the global ring buffer.
  for (EffectPtr& fx : clip->effects) {
    if (!fx) continue;
    if (!fx->IsEnabled()) continue;
    fx->process_audio(timecode_start, timecode_end, reinterpret_cast<quint8*>(buf.data()), byte_count, channels);
  }
  return buf;
}

void TestRenderHarness::assert_audio_silence(const QByteArray& samples, int tolerance) {
  const qint16* p = reinterpret_cast<const qint16*>(samples.constData());
  const int n = samples.size() / static_cast<int>(sizeof(qint16));
  for (int i = 0; i < n; ++i) {
    QVERIFY2(qAbs(p[i]) <= tolerance,
             qPrintable(QString("sample %1 = %2, expected silence +/-%3").arg(i).arg(p[i]).arg(tolerance)));
  }
}

void TestRenderHarness::assert_audio_non_silence(const QByteArray& samples, int min_abs_threshold) {
  const qint16* p = reinterpret_cast<const qint16*>(samples.constData());
  const int n = samples.size() / static_cast<int>(sizeof(qint16));
  for (int i = 0; i < n; ++i) {
    if (qAbs(p[i]) >= min_abs_threshold) return;
  }
  QFAIL(qPrintable(QString("no sample exceeded +/-%1 in %2 samples").arg(min_abs_threshold).arg(n)));
}

void TestRenderHarness::assert_channel_silence(const QByteArray& samples, int channel, int tolerance) {
  Q_ASSERT(channel == 0 || channel == 1);
  const qint16* p = reinterpret_cast<const qint16*>(samples.constData());
  const int n = samples.size() / 4;  // stereo frames
  for (int i = 0; i < n; ++i) {
    qint16 s = p[2 * i + channel];
    QVERIFY2(
        qAbs(s) <= tolerance,
        qPrintable(QString("frame %1 ch %2 = %3, expected silence +/-%4").arg(i).arg(channel).arg(s).arg(tolerance)));
  }
}

void TestRenderHarness::assert_channels_equal(const QByteArray& samples, int tolerance) {
  const qint16* p = reinterpret_cast<const qint16*>(samples.constData());
  const int n = samples.size() / 4;
  for (int i = 0; i < n; ++i) {
    qint16 l = p[2 * i + 0];
    qint16 r = p[2 * i + 1];
    QVERIFY2(qAbs(l - r) <= tolerance,
             qPrintable(QString("frame %1 L=%2 R=%3 differ by +/-%4").arg(i).arg(l).arg(r).arg(tolerance)));
  }
}
