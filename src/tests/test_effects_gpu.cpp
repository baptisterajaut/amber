#include <QGuiApplication>
#include <QSet>
#include <QSurfaceFormat>
#include <QTemporaryDir>
#include <QTest>
#include <QTextStream>
#include <QVector3D>
#include <cmath>
#include <functional>

#include <memory>

extern "C" {
#include <libavcodec/avcodec.h>
}

#include "effects/effect.h"
#include "effects/internal/srtparser.h"
#include "effects/internal/subtitleeffect.h"
#include "engine/clip.h"
#include "rendering/renderfunctions.h"
#include "tests/test_render_harness.h"
#include "tests/test_gpu_required.h"

class TestEffectsGpu : public QObject {
  Q_OBJECT
 private slots:
  void initTestCase();
  void cleanupTestCase();

  void solidColorPassthrough();
  void multiEffectChain();
  void multiFrameConsistency();
  void sceneSwitchConsistency();
  void steadyStateCreatesNothing();

  void effectPassthroughSweep_data();
  void effectPassthroughSweep();

  // Bespoke video effect tests (Phase 2).
  void transformTranslate();
  void transformScale();
  void transformRotate180();
  void keyframedTransform();
  void boundaryTimecodes();
  void textRenders();
  void textSizeZeroShowsNothing();
  void richTextRenders();
  void richTextShadowOpacity();
  void shakeChangesOutput();
  void timecodeRenders();
  void cornerpinDistorts();
  void cornerpinDefaultsPassthrough();
  void cornerpinMovesOneCorner();
  void subtitleActiveEntry();
  void subtitleOutlineSurroundsText();
  void gradientRendersTwoStops();

  // Bespoke audio effect tests (Phase 2).
  void toneGeneratesAudio();
  void audioNoiseNotSilent();
  void volumeMute();
  void volumePassthrough();
  void panHardLeft();
  void audioFillLeftRightRestoresChannel();

  // Phase 3a — Blur family (XML shader effects).
  void boxblurRadiusZero();
  void boxblurSolidColor();
  void boxblurNonZero();
  void gaussianblurRadiusZero();
  void gaussianblurSolidColor();
  void gaussianblurNonZero();
  void directionalblurRadiusZero();
  void directionalblurSolidColor();
  void directionalblurNonZero();
  void radialblurRadiusZero();
  void radialblurSolidColor();
  void radialblurNonZero();

  // Phase 3b — Distortions (XML shader effects).
  void bulgeDistorts();
  void bulgePreservesCenter();
  void fisheyeDistorts();
  void fisheyePreservesCenter();
  void sphereDistorts();
  void spherePreservesCenter();
  void swirlDistorts();
  void rippleDistorts();
  void waveDistorts();
  void chromaticAberrationShifts();
  void tileRepeats();
  void turbulentDisplaces();

  // Phase 3c — Color & key (XML shader effects).
  void invertSwapsChannels();
  void lutIdentityPassthrough();
  void lutInvertFullStrength();
  void lutHalfStrengthBlends();
  void lutSemiTransparentGradesStraightColour();
  void lutNoFilePassthrough();
  void huesatbriSaturationZero();
  void huesatbriContrast();
  void huesatbriHueRotate();
  void colorCorrectionShift();
  void colorselIsolates();
  void colorselHueRanges();
  void posterizeReducesLevels();
  void chromakeyTransparent();
  void chromakeyCompositeMode();
  void chromakeyOriginalMode();
  void lumakeyBands();
  void despillReducesGreen();

  // Phase 3d — Effects, crop & misc (XML shader effects).
  void flipHorizontal();
  void flipVertical();
  void cropTransparentBorders();
  void cropKeepsSemiTransparentColour();
  void embossOutputDiffers();
  void toonifyOutputDiffers();
  void noiseShaderOutputDiffers();
  void vignetteDarkensCorners();
  void pixelateBlocks();
  void crossstitchOutputDiffers();
  void volumetricLightOutputDiffers();

  // F-1 batch 1 — native replacements for common Frei0r color/lens plugins.
  void levelsNeutralIdentity();
  void levelsInputWhite();
  void levelsGamma();
  void levelsChannelRed();
  void liftGammaGainNeutralIdentity();
  void liftGammaGainGainRed();
  void liftGammaGainLiftWhite();
  void tintAmountZero();
  void tintShadowsHighlights();
  void thresholdLevel();
  void thresholdInvert();
  void lensCorrectionNeutralIdentity();
  void lensCorrectionK1Distorts();
  void colorShadersKeepTransparentClipTransparent();
  void liftGammaGainGamma();
  void thresholdCustomLevel();
  void lensCorrectionNonSquareAspect();

  // F-1 batch 2 — Find Edges, Colorize, Alpha Adjust.
  void findEdgesFlatSolid();
  void findEdgesHardEdge();
  void findEdgesStrengthZero();
  void findEdgesColorMode();
  void colorizeSaturationZero();
  void colorizeRedHue();
  void colorizeLightnessWhite();
  void alphaAdjustInvert();
  void alphaAdjustThreshold();
  void alphaAdjustShowAlpha();

  // F-1 batch 3 — original input for multipass shaders, Glow.
  void glowIntensityZeroIdentity();
  void glowThresholdFullIdentity();
  void glowCheckerboardHalo();
  void glowScreenMode();
  void glowSnapshotPingPongInput();

  // F-1 batch 4 — Denoise (spatial bilateral).
  void denoiseStrengthZeroIdentity();
  void denoiseFlatSolid();
  void denoiseReducesNoise();
  void denoiseKeepsHardEdge();
  void denoiseBilateralWeights();
  void denoiseIgnoresTransparentTaps();

  // Phase 4 — YUV decode regression.
  void yuvDecodeRoundtrip();
  void yuvDecodeNV12();

 private:
  std::unique_ptr<TestRenderHarness> h_;
};

void TestEffectsGpu::initTestCase() {
  h_ = std::make_unique<TestRenderHarness>(
      qEnvironmentVariable("AMBER_TEST_RHI") == QLatin1String("vulkan") ? QRhi::Vulkan : QRhi::OpenGLES2);
  if (!h_->initialized()) AMBER_SKIP_NO_GPU("QRhi OpenGLES2 unavailable");
}

void TestEffectsGpu::cleanupTestCase() { h_.reset(); }

void TestEffectsGpu::solidColorPassthrough() {
  auto seq = h_->make_sequence(64, 64, 30.0);
  // -1 = topmost video track (Olive's negative-is-video convention).
  Clip* c = h_->add_generator_clip(seq.get(), -1, 0, 30, EFFECT_INTERNAL_SOLID);

  // c->effects[0] = Transform, c->effects[1] = Solid
  Effect* solid = c->effects[1].get();
  // Row 2 = Color, field 0 = the ColorField itself.
  h_->set_field_color(solid, 2, 0, QColor(255, 128, 0, 255));

  QByteArray pixels = h_->render_frame(seq.get(), 0);
  QVERIFY(!pixels.isEmpty());
  h_->assert_solid_color(pixels, 64, 64, QColor(255, 128, 0, 255), 2);
}

void TestEffectsGpu::effectPassthroughSweep_data() {
  QTest::addColumn<int>("effect_index");

  // `effects` is the global QVector<EffectMeta> from effect.h:60 (no namespace).
  // We pass the vector index — looking up by name via get_meta_from_name() fails
  // for effect names containing '/' (e.g. "Hue/Saturation/Brightness"), since
  // that function interprets the first '/' as a category/name separator.
  for (int i = 0; i < effects.size(); ++i) {
    const EffectMeta& meta = effects[i];
    // Skip transitions (they need a different setup with two clips).
    if (meta.type != EFFECT_TYPE_EFFECT) continue;
    // Skip audio-subtype effects — sweep is video-only. Audio effects are
    // exercised in Phase 2 once render_audio() exists.
    if (meta.subtype == EFFECT_TYPE_AUDIO) continue;
    // Skip deprecated / GUI-only / disabled internals.
    if (meta.internal == EFFECT_INTERNAL_FREI0R) continue;
    if (meta.internal == EFFECT_INTERNAL_VST) continue;
    if (meta.internal == EFFECT_INTERNAL_MASK) continue;  // disabled
    QTest::newRow(qPrintable(meta.name)) << i;
  }
}

void TestEffectsGpu::effectPassthroughSweep() {
  QFETCH(int, effect_index);
  const EffectMeta* meta = &effects[effect_index];

  auto seq = h_->make_sequence(64, 64, 30.0);
  Clip* c = h_->add_generator_clip(seq.get(), -1, 0, 30, EFFECT_INTERNAL_SOLID);
  // Set Solid to a non-black colour so we can detect "all-zero output" as a failure mode.
  Effect* solid = c->effects[1].get();
  h_->set_field_color(solid, 2, 0, QColor(128, 128, 128, 255));

  // Attach the effect under test on top of the Solid generator.
  // Skip if it's the same internal as the generator (Solid already there).
  if (meta->internal != EFFECT_INTERNAL_TRANSFORM && meta->internal != EFFECT_INTERNAL_SOLID) {
    h_->attach_effect(c, meta);
  }

  QByteArray pixels = h_->render_frame(seq.get(), 0);
  QVERIFY2(!pixels.isEmpty(), qPrintable("readback empty for " + meta->name));
  QCOMPARE(pixels.size(), 64 * 64 * 4);

  // Smoke check: the buffer is not all-zero, which would indicate a crash
  // mid-render that left main_tex untouched.
  bool any_non_zero = false;
  for (int i = 0; i < pixels.size(); ++i) {
    if (pixels[i] != 0) {
      any_non_zero = true;
      break;
    }
  }
  QVERIFY2(any_non_zero, qPrintable("output all zeros for " + meta->name));
}

// ---------------------------------------------------------------------------
// Bespoke video effect tests
// ---------------------------------------------------------------------------
//
// Pattern: configure a small (64x64 unless noted) sequence with a Solid or
// Gradient generator, optionally attach the effect under test on top, render
// one frame, then assert on the readback pixels.
//
// Field row/field indices are taken from the corresponding effect ctor in
// src/effects/internal/<effect>.cpp. SetValueAt(0.0, value) writes
// persistent_data_, which is what GetValueAt reads at every timecode for
// non-keyframable fields (the default). SetDefaultData would NOT work — it
// writes default_data_ which the render path never reads.

namespace {

// Returns the alpha byte of pixel (x,y) in an RGBA8 readback (row-major).
inline int alpha_of(const QByteArray& pixels, int w, int x, int y) {
  return static_cast<int>(static_cast<uchar>(pixels[(y * w + x) * 4 + 3]));
}

// Returns the channels (R,G,B,A) at pixel (x,y).
struct Rgba {
  int r, g, b, a;
};
inline Rgba pixel_at(const QByteArray& pixels, int w, int x, int y) {
  const uchar* p = reinterpret_cast<const uchar*>(pixels.constData()) + (y * w + x) * 4;
  return {p[0], p[1], p[2], p[3]};
}

// Sum of absolute byte differences between two equally-sized buffers. Used by
// Phase 3 slots to test "effect changed output" (Pattern B) and "identity at
// neutral parameter" (Pattern A). Never assert on alpha — the compositing
// pipeline forces final-readback alpha=255 regardless of source (Phase 2
// lesson, subtitleActiveEntry).
inline int buf_diff(const QByteArray& a, const QByteArray& b) {
  Q_ASSERT(a.size() == b.size());
  int total = 0;
  for (int i = 0; i < a.size(); ++i) {
    total += qAbs(static_cast<int>(static_cast<uchar>(a[i])) - static_cast<int>(static_cast<uchar>(b[i])));
  }
  return total;
}

// True if every channel of every pixel matches within `tolerance`.
inline bool buffers_within(const QByteArray& a, const QByteArray& b, int tolerance) {
  if (a.size() != b.size()) return false;
  for (int i = 0; i < a.size(); ++i) {
    if (qAbs(static_cast<int>(static_cast<uchar>(a[i])) - static_cast<int>(static_cast<uchar>(b[i]))) > tolerance) {
      return false;
    }
  }
  return true;
}

// Writes a 3D .cube LUT of `size`^3 entries, red fastest, mapping each grid point through `fn`.
bool write_cube(const QString& path, int size, const std::function<QVector3D(QVector3D)>& fn) {
  QFile f(path);
  if (!f.open(QFile::WriteOnly | QFile::Truncate | QFile::Text)) return false;
  QTextStream out(&f);
  out << "LUT_3D_SIZE " << size << "\n";
  for (int b = 0; b < size; b++) {
    for (int g = 0; g < size; g++) {
      for (int r = 0; r < size; r++) {
        const QVector3D v = fn(QVector3D(r, g, b) / float(size - 1));
        out << v.x() << ' ' << v.y() << ' ' << v.z() << "\n";
      }
    }
  }
  return true;
}

}  // namespace

void TestEffectsGpu::multiEffectChain() {
  // Solid red -> Box Blur (no-op on uniform) -> Invert. Invert at amount=100 maps
  // rgb=(1,0,0) -> (0,1,1) = cyan. Verifies effects chained in series both fire:
  // if either is passthrough, output stays red and fails `mid.r < 30`.
  auto seq = h_->make_sequence(64, 64, 30.0);
  Clip* c = h_->add_generator_clip(seq.get(), -1, 0, 30, EFFECT_INTERNAL_SOLID);
  h_->set_field_color(c->effects[1].get(), 2, 0, QColor(255, 0, 0, 255));

  h_->attach_xml_shader(c, "Box Blur");
  h_->attach_xml_shader(c, "Invert");
  QByteArray pixels = h_->render_frame(seq.get(), 0);

  Rgba mid = pixel_at(pixels, 64, 32, 32);
  QVERIFY2(mid.r < 30 && mid.g > 200 && mid.b > 200,
           qPrintable(QString("expected red -> cyan after chain, got (%1,%2,%3)").arg(mid.r).arg(mid.g).arg(mid.b)));
}

void TestEffectsGpu::multiFrameConsistency() {
  // Same frame rendered three times must be byte-identical AND match pixels pinned from the pre-cache build: guards
  // pooled/cached RHI resources (a stale or shared vertex buffer/UBO, a wrong cached pipeline). Bottom clip is a
  // checkerboard so both Box Blur passes (iteration 0 = horizontal, 1 = vertical) and the HSB hue shift change pixels;
  // the semi-transparent top clip exercises the srcover blend onto it.
  SequencePtr seq = h_->make_sequence(64, 64, 30);
  Clip* bottom = h_->add_generator_clip(seq.get(), -1, 0, 30, EFFECT_INTERNAL_SOLID);
  Effect* checker = bottom->effects[1].get();
  h_->set_field_combo(checker, 0, 0, 2);                     // Type = SOLID_TYPE_CHECKERBOARD
  h_->set_field_color(checker, 2, 0, QColor(240, 120, 30));  // odd cells; (0,0) cell is black
  h_->set_field_double(checker, 3, 0, 16.0);                 // cell size 16 px
  EffectPtr blur = h_->attach_xml_shader(bottom, "Box Blur");
  h_->set_field_double(blur.get(), 0, 0, 8.0);  // radius
  EffectPtr hsb = h_->attach_xml_shader(bottom, "Hue/Saturation/Brightness");
  h_->set_field_double(hsb.get(), 0, 0, 60.0);  // hue
  // Second clip, semi-transparent, shifted right by 24 px (covers x >= 24), with its own Box Blur passes in the pool
  Clip* top = h_->add_generator_clip(seq.get(), -2, 0, 30, EFFECT_INTERNAL_SOLID);
  h_->set_field_double(top->effects[1].get(), 1, 0, 50.0);  // Solid opacity 50%
  h_->set_field_color(top->effects[1].get(), 2, 0, QColor(20, 60, 220));
  h_->set_field_double(top->effects[0].get(), 0, 0, 64.0 / 2 + 24);  // posx
  h_->attach_xml_shader(top, "Box Blur");
  QByteArray f1 = h_->render_frame(seq.get(), 5);
  QByteArray f2 = h_->render_frame(seq.get(), 5);
  QByteArray f3 = h_->render_frame(seq.get(), 5);
  QVERIFY(!f1.isEmpty());
  // Values captured from the pre-cache build (per-pass pipeline/buffer create+destroy), tolerance +/-2
  // (15,8) horizontal pass only, (8,15) vertical pass only, (15,15) both, (8,24) cell interior: HSB only
  h_->assert_pixel(f1, 64, 15, 8, QColor(65, 105, 13, 255), 2);
  h_->assert_pixel(f1, 64, 8, 15, QColor(65, 105, 13, 255), 2);
  h_->assert_pixel(f1, 64, 15, 15, QColor(73, 118, 15, 255), 2);
  h_->assert_pixel(f1, 64, 8, 24, QColor(150, 240, 30, 255), 2);
  // (22,8) bottom only near the top clip's edge (x=24), (26,8) and (40,24) srcover of the 50% top clip
  h_->assert_pixel(f1, 64, 22, 8, QColor(131, 210, 26, 255), 2);
  h_->assert_pixel(f1, 64, 26, 8, QColor(76, 135, 123, 255), 2);
  h_->assert_pixel(f1, 64, 40, 24, QColor(85, 150, 125, 255), 2);
  QCOMPARE(f2, f1);
  QCOMPARE(f3, f1);
}

void TestEffectsGpu::sceneSwitchConsistency() {
  // make_sequence() points amber::ActiveSequence at the newest sequence (B); harmless here because Solid,
  // Box Blur and Hue/Saturation/Brightness don't read it. Don't add Timecode-like effects to this test.
  // A and B use different shaders and UBOs: a cache key that ignored the shader or a buffer reused across scenes
  // would give B A's pipeline (or the reverse), which the pinned pixels catch even when a2 == a1.
  SequencePtr a = h_->make_sequence(64, 64, 30);
  Clip* ca = h_->add_generator_clip(a.get(), -1, 0, 30, EFFECT_INTERNAL_SOLID);
  Effect* checker = ca->effects[1].get();
  h_->set_field_combo(checker, 0, 0, 2);  // Type = SOLID_TYPE_CHECKERBOARD
  h_->set_field_color(checker, 2, 0, QColor(250, 200, 10));
  h_->set_field_double(checker, 3, 0, 16.0);
  h_->set_field_double(ca->effects[0].get(), 0, 0, 64.0 / 2 + 16);  // posx: covers x >= 16
  EffectPtr blur = h_->attach_xml_shader(ca, "Box Blur");
  h_->set_field_double(blur.get(), 0, 0, 8.0);  // radius
  SequencePtr b = h_->make_sequence(64, 64, 30);
  Clip* cb = h_->add_generator_clip(b.get(), -1, 0, 30, EFFECT_INTERNAL_SOLID);
  h_->set_field_color(cb->effects[1].get(), 2, 0, QColor(10, 120, 90));
  EffectPtr hsb = h_->attach_xml_shader(cb, "Hue/Saturation/Brightness");
  h_->set_field_double(hsb.get(), 0, 0, 90.0);  // hue
  QByteArray a1 = h_->render_frame(a.get(), 5);
  QByteArray b1 = h_->render_frame(b.get(), 5);
  QByteArray a2 = h_->render_frame(a.get(), 5);
  QVERIFY(!a1.isEmpty() && !b1.isEmpty());
  // Values captured from the pre-cache build (per-pass pipeline/buffer create+destroy), tolerance +/-2
  // A: (8,32) left of the shifted clip (cleared), (31,8) horizontal blur edge, (24,40) black cell, (56,24) colour
  h_->assert_pixel(a1, 64, 8, 32, QColor(0, 0, 0, 0), 2);
  h_->assert_pixel(a1, 64, 31, 8, QColor(109, 88, 4, 255), 2);
  h_->assert_pixel(a1, 64, 24, 40, QColor(0, 0, 0, 255), 2);
  h_->assert_pixel(a1, 64, 56, 24, QColor(250, 200, 10, 255), 2);
  // B: hue-shifted solid, so B drawn with A's pipeline (or no HSB) shows
  h_->assert_pixel(b1, 64, 32, 32, QColor(35, 10, 120, 255), 2);
  QVERIFY(a1 != b1);  // the switch really changes what the pooled buffers hold
  QCOMPARE(a2, a1);
}

void TestEffectsGpu::steadyStateCreatesNothing() {
  // After warm-up, frames of an unchanged scene must create no pipeline and no buffer: neither through the cache
  // (cache_stats) nor per pass outside it (pipelines/buffers left in transientResources).
  // Each scene starts on an empty cache, so its warm-up must create pipelines and buffers itself (NV12 would
  // otherwise reuse the YUV420P scene's pipeline, and earlier slots' entries would hide a scene missing the cache).
  auto check_steady = [this](Sequence* seq, long first, long step, const char* scene, QByteArray& frame0) {
    h_->reset_cache();
    frame0 = h_->render_frame(seq, first);
    QVERIFY(!frame0.isEmpty());
    h_->render_frame(seq, first + step);  // warm-up
    const RhiResourceCache::Stats before = h_->cache_stats();
    QVERIFY2(before.pipelinesCreated > 0 && before.buffersCreated > 0,
             qPrintable(QStringLiteral("%1: warm-up created %2 pipelines, %3 buffers through the cache")
                            .arg(scene)
                            .arg(before.pipelinesCreated)
                            .arg(before.buffersCreated)));
    for (int i = 2; i < 12; i++) {
      QVERIFY(!h_->render_frame(seq, first + i * step).isEmpty());
      QVERIFY2(h_->last_frame_transient_pipelines_and_buffers() == 0,
               qPrintable(QStringLiteral("%1: frame %2 created %3 pipelines/buffers outside the cache")
                              .arg(scene)
                              .arg(i)
                              .arg(h_->last_frame_transient_pipelines_and_buffers())));
    }
    const RhiResourceCache::Stats after = h_->cache_stats();
    QVERIFY2(after.pipelinesCreated == before.pipelinesCreated && after.buffersCreated == before.buffersCreated,
             qPrintable(QStringLiteral("%1: steady state created %2 pipelines, %3 buffers")
                            .arg(scene)
                            .arg(after.pipelinesCreated - before.pipelinesCreated)
                            .arg(after.buffersCreated - before.buffersCreated)));
  };
  // The 8x8-block checkerboard fixtures really decoded and drew: block (4,4) white, block (3,4) black, both opaque
  auto check_checkerboard = [this](const QByteArray& frame0) {
    h_->assert_pixel(frame0, 64, 36, 36, QColor(255, 255, 255, 255), 55);
    h_->assert_pixel(frame0, 64, 28, 36, QColor(0, 0, 0, 255), 55);
  };
  QByteArray frame0;

  // Generator clip + two XML effects: render_clip_to_backbuffer, rhi_blit and rhi_blit_srcover
  SequencePtr seq = h_->make_sequence(64, 64, 30);
  Clip* c = h_->add_generator_clip(seq.get(), -1, 0, 30, EFFECT_INTERNAL_SOLID);
  h_->set_field_color(c->effects[1].get(), 2, 0, QColor(200, 40, 20));
  h_->attach_xml_shader(c, "Box Blur");
  h_->attach_xml_shader(c, "Hue/Saturation/Brightness");
  h_->attach_xml_shader(c, "Glow");  // original="1" on a ping-pong input: copy into the lazy ClipRhiResources::orig
  check_steady(seq.get(), 5, 1, "generator", frame0);
  if (QTest::currentTestFailed()) return;
  // Textures aren't counted above: the snapshot target must be created once, then reused
  QRhiTexture* orig = c->fbo_rhi ? static_cast<ClipRhiResources*>(c->fbo_rhi)->orig : nullptr;
  QVERIFY2(orig != nullptr, "Glow after Box Blur should snapshot its input into ClipRhiResources::orig");
  QVERIFY(!h_->render_frame(seq.get(), 20).isEmpty());
  QCOMPARE(static_cast<ClipRhiResources*>(c->fbo_rhi)->orig, orig);

  // Decoded footage: the YUV->RGB pass in Clip::retrieveYuvGpuPath (1-frame fixtures, so stay on frame 0)
  if (avcodec_find_decoder(AV_CODEC_ID_H264)) {
    SequencePtr yuv = h_->make_sequence(64, 64, 30);
    h_->add_video_clip(yuv.get(), 0, 30,
                       h_->import_video_media(SOURCE_DIR "/tests/fixtures/t1_yuv_input.mp4", 64, 64, 30));
    check_steady(yuv.get(), 0, 0, "yuv420p", frame0);
    if (QTest::currentTestFailed()) return;
    check_checkerboard(frame0);
    if (QTest::currentTestFailed()) return;
  }
  if (avcodec_find_decoder(AV_CODEC_ID_RAWVIDEO)) {
    SequencePtr nv12 = h_->make_sequence(64, 64, 30);
    h_->add_video_clip(nv12.get(), 0, 30,
                       h_->import_video_media(SOURCE_DIR "/tests/fixtures/t2_nv12_input.mkv", 64, 64, 30));
    check_steady(nv12.get(), 0, 0, "nv12", frame0);
    if (QTest::currentTestFailed()) return;
    check_checkerboard(frame0);
  }
}

void TestEffectsGpu::transformTranslate() {
  // Solid red generator + Transform translated +16 in X. Without translation
  // every pixel is red. With +16 translation, the leftmost 16 columns of the
  // output should be transparent (texture sampled outside ClampToEdge would
  // still be red, but the clip's quad vertices are translated so the area
  // outside the quad receives no draw). We use the "core area still red"
  // assertion: pixel (32, 32) is well inside the translated quad and stays red.
  auto seq = h_->make_sequence(64, 64, 30.0);
  Clip* c = h_->add_generator_clip(seq.get(), -1, 0, 30, EFFECT_INTERNAL_SOLID);
  Effect* solid = c->effects[1].get();
  h_->set_field_color(solid, 2, 0, QColor(255, 0, 0, 255));

  Effect* transform = c->effects[0].get();
  // Transform row 0 = Position (posx, posy); default at refresh() is (w/2, h/2).
  // Adding +16 to posx moves the clip's quad right by 16 px.
  h_->set_field_double(transform, 0, 0, 64.0 / 2.0 + 16.0);  // posx
  h_->set_field_double(transform, 0, 1, 64.0 / 2.0);         // posy

  QByteArray pixels = h_->render_frame(seq.get(), 0);
  QVERIFY(!pixels.isEmpty());

  // Center of the translated quad: (32, 32) should still be solid red.
  h_->assert_pixel(pixels, 64, 32, 32, QColor(255, 0, 0, 255), 4);
  // A pixel inside the quad's new position only (e.g. (40, 32)): still red.
  h_->assert_pixel(pixels, 64, 40, 32, QColor(255, 0, 0, 255), 4);
  // Outside-quad witness: with posx=+16 the quad spans x ∈ [16, 80]; pixel (4, 32) is
  // outside → main_tex stays at the clear (alpha=0). Catches passthrough Transform
  // (identity quad covers everything → pixel (4, 32) inside → solid red, alpha=255).
  const int a_out = alpha_of(pixels, 64, 4, 32);
  QVERIFY2(a_out < 50,
           qPrintable(QString("expected transparent outside translated quad at (4,32), alpha=%1").arg(a_out)));
}

void TestEffectsGpu::transformScale() {
  // Solid + Transform scale 50% → bounding box becomes 32x32 centered. Outside
  // (e.g. corner (0,0)) is transparent; inside (e.g. (32,32)) is red.
  auto seq = h_->make_sequence(64, 64, 30.0);
  Clip* c = h_->add_generator_clip(seq.get(), -1, 0, 30, EFFECT_INTERNAL_SOLID);
  Effect* solid = c->effects[1].get();
  h_->set_field_color(solid, 2, 0, QColor(255, 0, 0, 255));

  Effect* transform = c->effects[0].get();
  // Row 1 = Scale (scalex, scaley) as percent. Row 2 = UniformScale bool (default true).
  h_->set_field_double(transform, 1, 0, 50.0);  // scalex
  h_->set_field_double(transform, 1, 1, 50.0);  // scaley

  QByteArray pixels = h_->render_frame(seq.get(), 0);
  QVERIFY(!pixels.isEmpty());

  // Center stays red.
  h_->assert_pixel(pixels, 64, 32, 32, QColor(255, 0, 0, 255), 4);
  // Far corner is outside the half-size quad → transparent.
  QVERIFY2(alpha_of(pixels, 64, 1, 1) < 50,
           qPrintable(QString("expected transparent corner, alpha=%1").arg(alpha_of(pixels, 64, 1, 1))));
}

void TestEffectsGpu::transformRotate180() {
  // Gradient (black→white top→bottom) + Transform rotated 180. After rotation
  // the top of the gradient (black) ends up at the bottom and vice versa.
  auto seq = h_->make_sequence(64, 64, 30.0);
  Clip* c = h_->add_generator_clip(seq.get(), -1, 0, 30, EFFECT_INTERNAL_GRADIENT);
  Effect* gradient = c->effects[1].get();
  // Gradient row 0 = type (combo, default LINEAR), 1 = start_color, 2 = end_color, 3 = angle.
  h_->set_field_color(gradient, 1, 0, QColor(Qt::black));
  h_->set_field_color(gradient, 2, 0, QColor(Qt::white));
  h_->set_field_double(gradient, 3, 0, 90.0);  // top→bottom

  Effect* transform = c->effects[0].get();
  h_->set_field_double(transform, 3, 0, 180.0);  // rotation row=3 field=0

  QByteArray pixels = h_->render_frame(seq.get(), 0);
  QVERIFY(!pixels.isEmpty());

  // After 180 rotation the gradient is flipped: top of output should now be
  // bright (was bottom of gradient = white), bottom should be dark.
  Rgba top = pixel_at(pixels, 64, 32, 4);
  Rgba bot = pixel_at(pixels, 64, 32, 60);
  QVERIFY2(
      top.r > bot.r + 50,
      qPrintable(
          QString("expected top brighter than bottom after 180 rotation, top.r=%1 bot.r=%2").arg(top.r).arg(bot.r)));
}

void TestEffectsGpu::keyframedTransform() {
  // Transform.position_x keyframed: posx=32 at frame 0 (quad centred -> pixel (4,32) inside),
  // posx=52 at frame 15 (quad shifted +20 -> pixel (4,32) outside, alpha~=0). Uses direct
  // EffectField::keyframes.append() -- SetValueAt() on an empty keyframe array falls through
  // to persistent_data_ instead of creating keyframes. Pattern mirrors effect.cpp:144-147.
  auto seq = h_->make_sequence(64, 64, 30.0);
  Clip* c = h_->add_generator_clip(seq.get(), -1, 0, 30, EFFECT_INTERNAL_SOLID);
  h_->set_field_color(c->effects[1].get(), 2, 0, QColor(255, 0, 0, 255));

  Effect* transform = c->effects[0].get();
  EffectRow* row = transform->row(0);  // Position row
  row->SetKeyframingInternal(true);
  EffectField* posx = row->Field(0);
  EffectKeyframe k0;
  k0.time = 0;
  k0.data = QVariant(32.0);
  k0.type = EFFECT_KEYFRAME_LINEAR;
  EffectKeyframe k1;
  k1.time = 15;
  k1.data = QVariant(52.0);
  k1.type = EFFECT_KEYFRAME_LINEAR;
  posx->keyframes.append(k0);
  posx->keyframes.append(k1);

  QByteArray pixels0 = h_->render_frame(seq.get(), 0);
  QVERIFY(!pixels0.isEmpty());
  Rgba inside_t0 = pixel_at(pixels0, 64, 4, 32);
  QVERIFY2(inside_t0.r > 200 && inside_t0.a > 200,
           qPrintable(QString("expected red at t=0 (4,32) inside quad, got (%1,%2,%3,%4)")
                          .arg(inside_t0.r)
                          .arg(inside_t0.g)
                          .arg(inside_t0.b)
                          .arg(inside_t0.a)));

  QByteArray pixels15 = h_->render_frame(seq.get(), 15);
  QVERIFY(!pixels15.isEmpty());
  const int a_t15 = alpha_of(pixels15, 64, 4, 32);
  QVERIFY2(a_t15 < 50,
           qPrintable(QString("expected transparent at t=15 (4,32) outside translated quad, alpha=%1").arg(a_t15)));
}

void TestEffectsGpu::boundaryTimecodes() {
  // Solid red clip spanning [0, 30) with default Transform. Render at both
  // edges of the clip's lifetime: playhead=0 (first frame) and playhead=29
  // (last frame). Both must yield red at the centre — guards against
  // off-by-one bugs at clip boundaries (e.g. compose_sequence() skipping
  // the in-point frame or the last frame).
  auto seq = h_->make_sequence(64, 64, 30.0);
  Clip* c = h_->add_generator_clip(seq.get(), -1, 0, 30, EFFECT_INTERNAL_SOLID);
  h_->set_field_color(c->effects[1].get(), 2, 0, QColor(255, 0, 0, 255));

  for (long playhead : {0L, 29L}) {
    QByteArray pixels = h_->render_frame(seq.get(), playhead);
    QVERIFY2(!pixels.isEmpty(), qPrintable(QString("render_frame returned empty at playhead=%1").arg(playhead)));
    Rgba px = pixel_at(pixels, 64, 32, 32);
    QVERIFY2(px.r > 200 && px.a > 200, qPrintable(QString("expected red at playhead=%1 (32,32), got (%2,%3,%4,%5)")
                                                      .arg(playhead)
                                                      .arg(px.r)
                                                      .arg(px.g)
                                                      .arg(px.b)
                                                      .arg(px.a)));
  }
}

void TestEffectsGpu::textRenders() {
  // Text "HELLO" white centered on a transparent clip — assert at least one
  // central pixel is bright and at least one border pixel is dim.
  auto seq = h_->make_sequence(64, 64, 30.0);
  Clip* c = h_->add_generator_clip(seq.get(), -1, 0, 30, EFFECT_INTERNAL_SOLID);
  // Make Solid transparent so we measure only the text contribution.
  Effect* solid = c->effects[1].get();
  h_->set_field_color(solid, 2, 0, QColor(0, 0, 0, 0));

  // Text rows: 0=text, 1=font, 2=size, 3=color, 4=alignment, 5=wordwrap.
  EffectPtr text_eff = h_->attach_internal(c, EFFECT_INTERNAL_TEXT, EFFECT_TYPE_EFFECT);
  h_->set_field_string(text_eff.get(), 0, 0, "HELLO");
  h_->set_field_double(text_eff.get(), 2, 0, 16.0);  // size
  h_->set_field_color(text_eff.get(), 3, 0, QColor(Qt::white));

  QByteArray pixels = h_->render_frame(seq.get(), 0);
  QVERIFY(!pixels.isEmpty());

  // At least one pixel in the central horizontal band has high alpha (text glyph).
  bool central_bright = false;
  for (int y = 25; y <= 38 && !central_bright; ++y) {
    for (int x = 8; x < 56; ++x) {
      if (alpha_of(pixels, 64, x, y) > 200) {
        central_bright = true;
        break;
      }
    }
  }
  QVERIFY2(central_bright, "expected at least one bright text pixel in central band");
}

void TestEffectsGpu::textSizeZeroShowsNothing() {
  // Text drawn, then its size set to 0: the clip must go blank, not keep showing the previous text.
  auto seq = h_->make_sequence(64, 64, 30.0);
  Clip* c = h_->add_generator_clip(seq.get(), -1, 0, 30, EFFECT_INTERNAL_SOLID);
  h_->set_field_color(c->effects[1].get(), 2, 0, QColor(0, 0, 0, 0));
  EffectPtr text_eff = h_->attach_internal(c, EFFECT_INTERNAL_TEXT, EFFECT_TYPE_EFFECT);
  // Rows: 0=text, 6=size, 7=colour
  h_->set_field_string(text_eff.get(), 0, 0, "HELLO");
  h_->set_field_double(text_eff.get(), 6, 0, 16.0);
  h_->set_field_color(text_eff.get(), 7, 0, QColor(Qt::white));

  auto count_bright = [](const QByteArray& buf) {
    int n = 0;
    for (int i = 0; i + 3 < buf.size(); i += 4) {
      if (static_cast<uchar>(buf[i]) > 200 && static_cast<uchar>(buf[i + 1]) > 200) ++n;
    }
    return n;
  };
  QByteArray drawn = h_->render_frame(seq.get(), 0);
  QVERIFY2(count_bright(drawn) > 20, "reference text missing");

  h_->set_field_double(text_eff.get(), 6, 0, 0.0);
  QByteArray blank = h_->render_frame(seq.get(), 0);
  QCOMPARE(count_bright(blank), 0);
}

void TestEffectsGpu::richTextRenders() {
  // RichText with simple bold HTML. Same "non-background pixels in center" check.
  auto seq = h_->make_sequence(64, 64, 30.0);
  Clip* c = h_->add_generator_clip(seq.get(), -1, 0, 30, EFFECT_INTERNAL_SOLID);
  Effect* solid = c->effects[1].get();
  h_->set_field_color(solid, 2, 0, QColor(0, 0, 0, 0));

  // RichText row 0 = text (HTML string).
  EffectPtr rich = h_->attach_internal(c, EFFECT_INTERNAL_RICHTEXT, EFFECT_TYPE_EFFECT);
  h_->set_field_string(rich.get(), 0, 0,
                       "<html><body style=\"color:#ffffff; font-size:20pt;\"><center>X</center></body></html>");

  QByteArray pixels = h_->render_frame(seq.get(), 0);
  QVERIFY(!pixels.isEmpty());

  // Look anywhere in the frame for a non-background pixel (RichText positions
  // by document layout; the glyph X lands somewhere central).
  bool any_glyph = false;
  for (int y = 10; y < 54 && !any_glyph; ++y) {
    for (int x = 10; x < 54; ++x) {
      if (alpha_of(pixels, 64, x, y) > 100) {
        any_glyph = true;
        break;
      }
    }
  }
  QVERIFY2(any_glyph, "expected at least one non-background pixel from RichText glyph");
}

void TestEffectsGpu::richTextShadowOpacity() {
  // Shadow Opacity used to be ignored. It now dims the shadow, and a project saved before the fix (its dead
  // "shadowopacity" field holds 0) still loads with a full-strength shadow.
  auto make_rich = [this](SequencePtr& seq) {
    seq = h_->make_sequence(64, 64, 30.0);
    Clip* c = h_->add_generator_clip(seq.get(), -1, 0, 30, EFFECT_INTERNAL_SOLID);
    h_->set_field_color(c->effects[1].get(), 2, 0, QColor(0, 0, 0, 0));
    return h_->attach_internal(c, EFFECT_INTERNAL_RICHTEXT, EFFECT_TYPE_EFFECT);
  };
  // Brightest red in the frame: the red shadow, away from the white glyph. Over the black background a dimmer
  // shadow reads as a darker red.
  auto shadow_red = [](const QByteArray& buf) {
    int best = 0;
    for (int i = 0; i + 3 < buf.size(); i += 4) {
      const int r = static_cast<uchar>(buf[i]);
      if (static_cast<uchar>(buf[i + 1]) < 60 && static_cast<uchar>(buf[i + 2]) < 60) best = qMax(best, r);
    }
    return best;
  };

  SequencePtr seq;
  EffectPtr rich = make_rich(seq);
  // Rows: 0=text, 6=shadow, 7=shadow colour, 9=distance, 11=opacity (angle 0 and softness 0 by default)
  h_->set_field_string(rich.get(), 0, 0,
                       "<html><body style=\"color:#ffffff; font-size:20pt;\"><center>X</center></body></html>");
  h_->set_field_bool(rich.get(), 6, 0, true);
  h_->set_field_color(rich.get(), 7, 0, QColor(Qt::red));
  h_->set_field_double(rich.get(), 9, 0, 6.0);

  const int full = shadow_red(h_->render_frame(seq.get(), 0));
  QVERIFY2(full > 200, qPrintable(QString("expected a full-strength red shadow at default opacity, got %1").arg(full)));

  h_->set_field_double(rich.get(), 11, 0, 50.0);
  const int half = shadow_red(h_->render_frame(seq.get(), 0));
  QVERIFY2(half > 80 && half < 180, qPrintable(QString("expected a half-strength shadow, got %1").arg(half)));

  // Same settings as saved by a pre-fix project: the opacity under its old id, at the 0 it always held.
  QByteArray legacy = rich->save_to_string();
  QVERIFY(legacy.contains("id=\"shadow_opacity\" value=\"50\""));
  legacy.replace("id=\"shadow_opacity\" value=\"50\"", "id=\"shadowopacity\" value=\"0\"");
  SequencePtr seq2;
  EffectPtr loaded = make_rich(seq2);
  loaded->load_from_string(legacy);
  const int legacy_red = shadow_red(h_->render_frame(seq2.get(), 0));
  QVERIFY2(legacy_red > 200,
           qPrintable(QString("expected a pre-fix project to keep its full shadow, got %1").arg(legacy_red)));
}

void TestEffectsGpu::shakeChangesOutput() {
  // Solid + Gradient + Shake at default amplitude. Compare two adjacent frames
  // — at non-zero intensity the shake offset must differ between them.
  auto seq = h_->make_sequence(64, 64, 30.0);
  Clip* c = h_->add_generator_clip(seq.get(), -1, 0, 30, EFFECT_INTERNAL_GRADIENT);
  Effect* gradient = c->effects[1].get();
  h_->set_field_color(gradient, 1, 0, QColor(Qt::black));
  h_->set_field_color(gradient, 2, 0, QColor(Qt::white));
  h_->set_field_double(gradient, 3, 0, 90.0);

  // Shake defaults: intensity=25, rotation=10, frequency=5. Plenty to move
  // pixels between two adjacent timecodes.
  h_->attach_internal(c, EFFECT_INTERNAL_SHAKE, EFFECT_TYPE_EFFECT);

  QByteArray frame0 = h_->render_frame(seq.get(), 0);
  QByteArray frame1 = h_->render_frame(seq.get(), 1);
  QVERIFY(!frame0.isEmpty());
  QVERIFY(!frame1.isEmpty());
  QCOMPARE(frame0.size(), frame1.size());

  // At least some pixels must differ.
  bool any_diff = false;
  for (int i = 0; i < frame0.size(); ++i) {
    if (frame0[i] != frame1[i]) {
      any_diff = true;
      break;
    }
  }
  QVERIFY2(any_diff, "expected shake to produce different pixels between frame 0 and frame 1");
}

void TestEffectsGpu::timecodeRenders() {
  // Timecode effect on a transparent clip — assert text-shaped pixels appear
  // in the lower band where the timecode is drawn.
  auto seq = h_->make_sequence(64, 64, 30.0);
  Clip* c = h_->add_generator_clip(seq.get(), -1, 0, 30, EFFECT_INTERNAL_SOLID);
  Effect* solid = c->effects[1].get();
  h_->set_field_color(solid, 2, 0, QColor(0, 0, 0, 0));

  h_->attach_internal(c, EFFECT_INTERNAL_TIMECODE, EFFECT_TYPE_EFFECT);

  QByteArray pixels = h_->render_frame(seq.get(), 0);
  QVERIFY(!pixels.isEmpty());

  // Timecode draws at offset_y + height - height/10 = 64 - 6 = 58 with the
  // default scale 100 and a small background rect just above. Look for any
  // non-background pixel in the bottom band [40, 63].
  bool any_pixel = false;
  for (int y = 40; y < 64 && !any_pixel; ++y) {
    for (int x = 0; x < 64; ++x) {
      if (alpha_of(pixels, 64, x, y) > 100) {
        any_pixel = true;
        break;
      }
    }
  }
  QVERIFY2(any_pixel, "expected timecode text/background pixels in the bottom band");
}

void TestEffectsGpu::cornerpinDistorts() {
  // Solid + Gradient + CornerPin with corners moved inward 16 px each. The
  // warped quad covers only the center half of the sequence — pixel (0,0)
  // is outside and stays transparent; pixel (32, 32) is inside.
  //
  // CornerPin OFFSETS are added to default vertex coords. For a 64x64 clip
  // the default coords go from (-32,-32) to (32,32). Moving TL by (+16,+16)
  // shifts to (-16,-16); other corners analogously.
  auto seq = h_->make_sequence(64, 64, 30.0);
  Clip* c = h_->add_generator_clip(seq.get(), -1, 0, 30, EFFECT_INTERNAL_GRADIENT);
  Effect* gradient = c->effects[1].get();
  h_->set_field_color(gradient, 1, 0, QColor(Qt::black));
  h_->set_field_color(gradient, 2, 0, QColor(Qt::white));
  h_->set_field_double(gradient, 3, 0, 90.0);

  EffectPtr corner = h_->attach_internal(c, EFFECT_INTERNAL_CORNERPIN, EFFECT_TYPE_EFFECT);
  // Rows 0..3: TL, TR, BL, BR. Each row has two doubles (x, y). Row 4 is the
  // perspective bool (default true).
  h_->set_field_double(corner.get(), 0, 0, +16.0);  // TL x
  h_->set_field_double(corner.get(), 0, 1, +16.0);  // TL y
  h_->set_field_double(corner.get(), 1, 0, -16.0);  // TR x
  h_->set_field_double(corner.get(), 1, 1, +16.0);  // TR y
  h_->set_field_double(corner.get(), 2, 0, +16.0);  // BL x
  h_->set_field_double(corner.get(), 2, 1, -16.0);  // BL y
  h_->set_field_double(corner.get(), 3, 0, -16.0);  // BR x
  h_->set_field_double(corner.get(), 3, 1, -16.0);  // BR y

  QByteArray pixels = h_->render_frame(seq.get(), 0);
  QVERIFY(!pixels.isEmpty());

  // Center is inside the warped quad → non-transparent.
  QVERIFY2(alpha_of(pixels, 64, 32, 32) > 50,
           qPrintable(QString("expected center inside warped quad, alpha=%1").arg(alpha_of(pixels, 64, 32, 32))));
  // Corner is outside → transparent (no draw).
  QVERIFY2(alpha_of(pixels, 64, 1, 1) < 50,
           qPrintable(QString("expected transparent outside warp, alpha=%1").arg(alpha_of(pixels, 64, 1, 1))));
}

void TestEffectsGpu::cornerpinDefaultsPassthrough() {
  auto seq = h_->make_sequence(64, 64, 30.0);
  Clip* c = h_->add_generator_clip(seq.get(), -1, 0, 30, EFFECT_INTERNAL_SOLID);
  h_->set_field_color(c->effects[1].get(), 2, 0, QColor(50, 100, 150, 255));
  h_->attach_internal(c, EFFECT_INTERNAL_CORNERPIN, EFFECT_TYPE_EFFECT);
  QByteArray pixels = h_->render_frame(seq.get(), 0);
  h_->assert_solid_color(pixels, 64, 64, QColor(50, 100, 150, 255));
}

void TestEffectsGpu::cornerpinMovesOneCorner() {
  // Only TR moves, by (-16, +16): from pixel (64,0) to (48,16). The quad becomes (0,0) (48,16) (64,64) (0,64).
  // Near TR (60,4) is now outside; near BL (4,60) and the centre-left (16,40) are untouched.
  // Row 1 = TR, fields 0/1 = x/y (cornerpineffect.cpp:35-36); +y moves down, as in cornerpinDistorts.
  auto seq = h_->make_sequence(64, 64, 30.0);
  Clip* c = h_->add_generator_clip(seq.get(), -1, 0, 30, EFFECT_INTERNAL_SOLID);
  h_->set_field_color(c->effects[1].get(), 2, 0, QColor(50, 100, 150, 255));
  EffectPtr corner = h_->attach_internal(c, EFFECT_INTERNAL_CORNERPIN, EFFECT_TYPE_EFFECT);
  h_->set_field_double(corner.get(), 1, 0, -16.0);  // TR x
  h_->set_field_double(corner.get(), 1, 1, +16.0);  // TR y
  QByteArray pixels = h_->render_frame(seq.get(), 0);
  QVERIFY2(alpha_of(pixels, 64, 60, 4) < 50,
           qPrintable(QString("expected transparent near moved TR, alpha(60,4)=%1 (56,8)=%2 (48,20)=%3")
                          .arg(alpha_of(pixels, 64, 60, 4))
                          .arg(alpha_of(pixels, 64, 56, 8))
                          .arg(alpha_of(pixels, 64, 48, 20))));
  h_->assert_pixel(pixels, 64, 4, 60, QColor(50, 100, 150, 255), 3);
  h_->assert_pixel(pixels, 64, 16, 40, QColor(50, 100, 150, 255), 3);
}

void TestEffectsGpu::subtitleActiveEntry() {
  // Subtitle effect with a single cue active over [0ms, 500ms). At 30 fps:
  //   frame 7  → 233 ms (active)  → text visible
  //   frame 30 → 1000 ms (past)   → fully transparent
  //
  // The second assertion exercises the AlwaysUpdate() fix: without it the
  // base Effect::valueHasChanged() short-circuit may skip a redraw when no
  // EffectField changes between frames, leaving the cached overlay.
  auto seq = h_->make_sequence(64, 64, 30.0);
  Clip* c = h_->add_generator_clip(seq.get(), -1, 0, 60, EFFECT_INTERNAL_SOLID);
  Effect* solid = c->effects[1].get();
  h_->set_field_color(solid, 2, 0, QColor(0, 0, 0, 0));

  EffectPtr sub_eff = h_->attach_internal(c, EFFECT_INTERNAL_SUBTITLE, EFFECT_TYPE_EFFECT);
  auto* subtitle = static_cast<SubtitleEffect*>(sub_eff.get());

  QVector<SubtitleCue> cues;
  cues.append(SubtitleCue{0, 500, "X"});
  subtitle->SetCues(cues);

  // Compositing pipeline produces opaque output regardless of source alpha
  // (Solid's transparent fill is composited onto an opaque framebuffer), so
  // alpha-based assertions don't work. Instead, count "text-like" pixels by
  // colour: the subtitle's default colour is white (255,255,255). At frame 7
  // the cue is active so white pixels are written. At frame 30 the cue has
  // ended and the subtitle texture must clear, leaving only the Solid
  // background colour — i.e. essentially no white pixels.
  auto count_white_pixels = [](const QByteArray& buf) {
    int n = 0;
    for (int i = 0; i + 3 < buf.size(); i += 4) {
      const uchar r = static_cast<uchar>(buf[i + 0]);
      const uchar g = static_cast<uchar>(buf[i + 1]);
      const uchar b = static_cast<uchar>(buf[i + 2]);
      if (r > 200 && g > 200 && b > 200) ++n;
    }
    return n;
  };

  QByteArray inside = h_->render_frame(seq.get(), 7);
  QVERIFY(!inside.isEmpty());
  const int n_text = count_white_pixels(inside);
  QVERIFY2(n_text > 20, qPrintable(QString("expected subtitle text pixels when cue is active, got %1").arg(n_text)));

  QByteArray after = h_->render_frame(seq.get(), 30);
  QVERIFY(!after.isEmpty());
  const int n_after = count_white_pixels(after);
  // Measured leak (frames 28/30/32) on Arch + Qt 6.10 2026-05-16: 0%/0%/0%
  // (n_text=222, n_after=0). Threshold max(2×, 5%) = 5% → n_after * 20 < n_text.
  QVERIFY2(n_after * 20 < n_text,
           qPrintable(QString("expected subtitle to clear past cue: inside=%1 after=%2").arg(n_text).arg(n_after)));
}

void TestEffectsGpu::subtitleOutlineSurroundsText() {
  // The outline (on by default) must sit around the text. It used to be offset a second time by the text's
  // padding/alignment translation, which pushed it off a bottom-aligned subtitle.
  auto seq = h_->make_sequence(64, 64, 30.0);
  Clip* c = h_->add_generator_clip(seq.get(), -1, 0, 60, EFFECT_INTERNAL_SOLID);
  h_->set_field_color(c->effects[1].get(), 2, 0, QColor(0, 0, 0, 0));

  EffectPtr sub_eff = h_->attach_internal(c, EFFECT_INTERNAL_SUBTITLE, EFFECT_TYPE_EFFECT);
  auto* subtitle = static_cast<SubtitleEffect*>(sub_eff.get());
  QVector<SubtitleCue> cues;
  cues.append(SubtitleCue{0, 500, "XX"});
  subtitle->SetCues(cues);
  // Rows: 1=size, 6=padding, 9=outline colour (red, to tell it from the white text and the black background)
  h_->set_field_double(sub_eff.get(), 1, 0, 16.0);
  h_->set_field_double(sub_eff.get(), 6, 0, 0.0);
  h_->set_field_color(sub_eff.get(), 9, 0, QColor(Qt::red));

  QByteArray pixels = h_->render_frame(seq.get(), 7);
  QVERIFY(!pixels.isEmpty());
  // Count red pixels on the rows the white text occupies. The doubly offset outline landed below the text (rows
  // 62-63 of this frame), so on those rows there was none.
  int n_white = 0;
  int n_red_by_text = 0;
  for (int y = 0; y < 64; y++) {
    int row_white = 0;
    int row_red = 0;
    for (int x = 0; x < 64; x++) {
      Rgba px = pixel_at(pixels, 64, x, y);
      if (px.r > 200 && px.g > 200 && px.b > 200) ++row_white;
      if (px.r > 150 && px.g < 80 && px.b < 80) ++row_red;
    }
    n_white += row_white;
    if (row_white > 0) n_red_by_text += row_red;
  }
  QVERIFY2(n_white > 20, qPrintable(QString("expected subtitle text pixels, got %1").arg(n_white)));
  QVERIFY2(n_red_by_text > 20,
           qPrintable(QString("expected red outline pixels beside the text, got %1").arg(n_red_by_text)));
}

void TestEffectsGpu::gradientRendersTwoStops() {
  // Linear gradient red→blue, top→bottom. Top row near red, bottom row near
  // blue, middle near (R+B)/2 purple.
  auto seq = h_->make_sequence(64, 64, 30.0);
  Clip* c = h_->add_generator_clip(seq.get(), -1, 0, 30, EFFECT_INTERNAL_GRADIENT);
  Effect* gradient = c->effects[1].get();
  // Rows: 0=type (combo LINEAR default), 1=start, 2=end, 3=angle.
  h_->set_field_color(gradient, 1, 0, QColor(Qt::red));
  h_->set_field_color(gradient, 2, 0, QColor(Qt::blue));
  h_->set_field_double(gradient, 3, 0, 90.0);  // top→bottom

  QByteArray pixels = h_->render_frame(seq.get(), 0);
  QVERIFY(!pixels.isEmpty());

  // Top pixel: red dominates.
  Rgba top = pixel_at(pixels, 64, 32, 2);
  QVERIFY2(top.r > 150 && top.b < 80,
           qPrintable(QString("expected red near top, got R=%1 G=%2 B=%3").arg(top.r).arg(top.g).arg(top.b)));
  // Bottom pixel: blue dominates.
  Rgba bot = pixel_at(pixels, 64, 32, 61);
  QVERIFY2(bot.b > 150 && bot.r < 80,
           qPrintable(QString("expected blue near bottom, got R=%1 G=%2 B=%3").arg(bot.r).arg(bot.g).arg(bot.b)));
  // Mid pixel: roughly equal red and blue.
  Rgba mid = pixel_at(pixels, 64, 32, 32);
  QVERIFY2(mid.r > 40 && mid.b > 40,
           qPrintable(QString("expected purple near middle, got R=%1 G=%2 B=%3").arg(mid.r).arg(mid.g).arg(mid.b)));
}

// ---------------------------------------------------------------------------
// Bespoke audio effect tests
// ---------------------------------------------------------------------------
//
// All audio tests use TestRenderHarness::add_audio_generator_clip() +
// render_audio(), which bypasses compose_audio() and the global audio_ibuffer
// ring. Each effect's process_audio() is invoked directly on a local
// stereo S16 LE buffer. See test_render_harness.cpp::render_audio for the
// exact contract.

void TestEffectsGpu::toneGeneratesAudio() {
  auto seq = h_->make_sequence(64, 64, 30.0);
  // track=0 is an audio track (Olive: track >= 0 == audio).
  Clip* c = h_->add_audio_generator_clip(seq.get(), 0, 0, 30, EFFECT_INTERNAL_TONE);
  // ToneEffect rows: 0=Type (Sine), 1=Frequency (default 1000 Hz), 2=Amount
  // (default 25, range 0-100), 3=Mix (default true). Bump amount to 50 to
  // make sure samples cross the +/-1000 threshold.
  Effect* tone = c->effects[0].get();
  h_->set_field_double(tone, 2, 0, 50.0);

  QByteArray buf = h_->render_audio(c, 0.0, 100);
  h_->assert_audio_non_silence(buf, /*min_abs_threshold=*/1000);
}

void TestEffectsGpu::audioNoiseNotSilent() {
  auto seq = h_->make_sequence(64, 64, 30.0);
  // EFFECT_INTERNAL_NOISE registers under subtype EFFECT_TYPE_AUDIO (audio
  // noise via AudioNoiseEffect), with type EFFECT_TYPE_EFFECT — the same
  // pattern as Tone/Volume/Pan. The harness's GetInternalMeta filter on
  // type == EFFECT_TYPE_EFFECT resolves it correctly.
  Clip* c = h_->add_audio_generator_clip(seq.get(), 0, 0, 30, EFFECT_INTERNAL_NOISE);
  // AudioNoise default amount is 20 — already audible.
  QByteArray buf = h_->render_audio(c, 0.0, 100);
  h_->assert_audio_non_silence(buf, /*min_abs_threshold=*/1000);
}

void TestEffectsGpu::volumeMute() {
  // Tone → Volume = 0. VolumeEffect's process_audio multiplies samples by
  // the raw field value (`left_samp *= vol_val`). The DisplayType::Decibel
  // setting in the ctor only affects the UI representation, not the stored
  // number. So setting the field to 0 gives a literal zero multiplier and
  // produces silence regardless of dB/linear interpretation.
  auto seq = h_->make_sequence(64, 64, 30.0);
  Clip* c = h_->add_audio_generator_clip(seq.get(), 0, 0, 30, EFFECT_INTERNAL_TONE);
  Effect* tone = c->effects[0].get();
  h_->set_field_double(tone, 2, 0, 50.0);  // amount

  EffectPtr vol_eff = h_->attach_internal(c, EFFECT_INTERNAL_VOLUME, EFFECT_TYPE_EFFECT);
  // Volume row 0, field 0 = volume (double). 0 → silence.
  h_->set_field_double(vol_eff.get(), 0, 0, 0.0);

  QByteArray buf = h_->render_audio(c, 0.0, 100);
  // Multiplication by exactly 0 yields exact 0; allow tiny rounding tolerance.
  h_->assert_audio_silence(buf, /*tolerance=*/4);
}

void TestEffectsGpu::volumePassthrough() {
  // Two parallel clips, separate sequences:
  //   A: Tone only.
  //   B: Tone + Volume = 1.0 (literal multiplier; passthrough).
  // Both Tones share the same params (amount=50, frequency=1000) and start
  // with sinX = INT_MIN, so the two output buffers should be sample-identical
  // up to int16 round-trip tolerance.
  auto seq_a = h_->make_sequence(64, 64, 30.0);
  Clip* a = h_->add_audio_generator_clip(seq_a.get(), 0, 0, 30, EFFECT_INTERNAL_TONE);
  h_->set_field_double(a->effects[0].get(), 2, 0, 50.0);
  QByteArray buf_a = h_->render_audio(a, 0.0, 50);

  auto seq_b = h_->make_sequence(64, 64, 30.0);
  Clip* b = h_->add_audio_generator_clip(seq_b.get(), 0, 0, 30, EFFECT_INTERNAL_TONE);
  h_->set_field_double(b->effects[0].get(), 2, 0, 50.0);
  EffectPtr vol_eff = h_->attach_internal(b, EFFECT_INTERNAL_VOLUME, EFFECT_TYPE_EFFECT);
  h_->set_field_double(vol_eff.get(), 0, 0, 1.0);  // passthrough multiplier
  QByteArray buf_b = h_->render_audio(b, 0.0, 50);

  QCOMPARE(buf_a.size(), buf_b.size());
  const qint16* pa = reinterpret_cast<const qint16*>(buf_a.constData());
  const qint16* pb = reinterpret_cast<const qint16*>(buf_b.constData());
  const int n = buf_a.size() / static_cast<int>(sizeof(qint16));
  // Tolerance ±32 absorbs the int16 round-trip in VolumeEffect (multiply,
  // clamp, re-pack via 8-bit shifts).
  for (int i = 0; i < n; ++i) {
    QVERIFY2(qAbs(pa[i] - pb[i]) <= 32,
             qPrintable(QString("sample %1: A=%2 B=%3 diff=%4").arg(i).arg(pa[i]).arg(pb[i]).arg(pa[i] - pb[i])));
  }
}

void TestEffectsGpu::panHardLeft() {
  // Tone (stereo identical L/R) + Pan = -100 → right channel multiplied by
  // (1.0 - log_volume(1.0)) = 0; right channel goes silent.
  auto seq = h_->make_sequence(64, 64, 30.0);
  Clip* c = h_->add_audio_generator_clip(seq.get(), 0, 0, 30, EFFECT_INTERNAL_TONE);
  Effect* tone = c->effects[0].get();
  h_->set_field_double(tone, 2, 0, 50.0);

  EffectPtr pan_eff = h_->attach_internal(c, EFFECT_INTERNAL_PAN, EFFECT_TYPE_EFFECT);
  // Pan row 0, field 0 = pan (-100 .. 100, default 0).
  h_->set_field_double(pan_eff.get(), 0, 0, -100.0);

  QByteArray buf = h_->render_audio(c, 0.0, 100);
  // Right channel (index 1) must be silent.
  h_->assert_channel_silence(buf, /*channel=*/1, /*tolerance=*/4);
}

void TestEffectsGpu::audioFillLeftRightRestoresChannel() {
  // Tone → Pan(-100) silences the right channel. FillLeftRight with mode=1
  // ("Fill Right with Left") copies the left channel onto the right, so the
  // resulting buffer must be non-silent AND have equal L/R samples.
  auto seq = h_->make_sequence(64, 64, 30.0);
  Clip* c = h_->add_audio_generator_clip(seq.get(), 0, 0, 30, EFFECT_INTERNAL_TONE);
  Effect* tone = c->effects[0].get();
  h_->set_field_double(tone, 2, 0, 50.0);

  EffectPtr pan = h_->attach_internal(c, EFFECT_INTERNAL_PAN, EFFECT_TYPE_EFFECT);
  h_->set_field_double(pan.get(), 0, 0, -100.0);

  EffectPtr fill = h_->attach_internal(c, EFFECT_INTERNAL_FILLLEFTRIGHT, EFFECT_TYPE_EFFECT);
  // FillLeftRight row 0 = Type combo. Index 1 = FILL_TYPE_RIGHT ("Fill Right with Left").
  h_->set_field_combo(fill.get(), 0, 0, 1);

  QByteArray buf = h_->render_audio(c, 0.0, 100);
  h_->assert_audio_non_silence(buf, /*min_abs_threshold=*/1000);
  h_->assert_channels_equal(buf, /*tolerance=*/2);
}

// ---------------------------------------------------------------------------
// Phase 3a — Blur family (XML shader effects)
// ---------------------------------------------------------------------------
//
// Each blur effect gets three slots:
//   <name>RadiusZero  — Pattern A: neutral radius/length/sigma ⇒ passthrough
//                       (every shader has a `radius > 0` short-circuit).
//   <name>SolidColor  — solid colour input ⇒ centre pixel preserves colour
//                       even after blurring (edges may darken).
//   <name>NonZero     — Pattern B: non-zero parameter ⇒ output differs from
//                       baseline by a wide margin.
//
// All field row/field indices were read from src/effects/shaders/<name>.xml.

void TestEffectsGpu::boxblurRadiusZero() {
  // boxblur.xml: row 0 = Radius (double, default 10). Setting to 0 takes the
  // `radius > 0` short-circuit in boxblur.frag → texture passthrough.
  auto seq = h_->make_sequence(64, 64, 30.0);
  Clip* c = h_->add_generator_clip(seq.get(), -1, 0, 30, EFFECT_INTERNAL_GRADIENT);
  Effect* gradient = c->effects[1].get();
  h_->set_field_color(gradient, 1, 0, QColor(Qt::black));
  h_->set_field_color(gradient, 2, 0, QColor(Qt::white));
  h_->set_field_double(gradient, 3, 0, 90.0);
  QByteArray baseline = h_->render_frame(seq.get(), 0);

  EffectPtr fx = h_->attach_xml_shader(c, "Box Blur");
  h_->set_field_double(fx.get(), 0, 0, 0.0);  // radius=0
  QByteArray with_fx = h_->render_frame(seq.get(), 0);

  QVERIFY2(buffers_within(baseline, with_fx, /*tolerance=*/4),
           qPrintable(QString("expected radius=0 passthrough, diff=%1").arg(buf_diff(baseline, with_fx))));
}

void TestEffectsGpu::boxblurSolidColor() {
  // Blur on uniform red: every sample of the kernel is the same red, so the
  // centre pixel (far from the edge fall-off) must remain ~red.
  auto seq = h_->make_sequence(64, 64, 30.0);
  Clip* c = h_->add_generator_clip(seq.get(), -1, 0, 30, EFFECT_INTERNAL_SOLID);
  h_->set_field_color(c->effects[1].get(), 2, 0, QColor(255, 0, 0, 255));

  EffectPtr fx = h_->attach_xml_shader(c, "Box Blur");
  // default radius=10 is fine.
  QByteArray pixels = h_->render_frame(seq.get(), 0);

  Rgba mid = pixel_at(pixels, 64, 32, 32);
  QVERIFY2(mid.r > 230 && mid.g < 30 && mid.b < 30,
           qPrintable(
               QString("expected solid red preserved at centre, got R=%1 G=%2 B=%3").arg(mid.r).arg(mid.g).arg(mid.b)));
}

void TestEffectsGpu::boxblurNonZero() {
  // Shape-aware: SOLID checkerboard (size=16 → 4×4 cells of 16 px on 64×64).
  // Cell (0,0) is black, cell (1,0) is white. Pixels just inside each cell at
  // x=15 / x=17 are pure black / white in baseline. A real box blur with
  // radius=8 mixes both cells → both pixels land in mid-luma. A passthrough-
  // regressed shader would leave 0 / 255, failing the [60,200] bound.
  auto seq = h_->make_sequence(64, 64, 30.0);
  Clip* c = h_->add_generator_clip(seq.get(), -1, 0, 30, EFFECT_INTERNAL_SOLID);
  Effect* solid = c->effects[1].get();
  h_->set_field_combo(solid, 0, 0, 2);                  // Type = SOLID_TYPE_CHECKERBOARD
  h_->set_field_color(solid, 2, 0, QColor(Qt::white));  // even cells; odd cells hardcoded black
  h_->set_field_double(solid, 3, 0, 16.0);              // cell size 16 px

  EffectPtr fx = h_->attach_xml_shader(c, "Box Blur");
  h_->set_field_double(fx.get(), 0, 0, 8.0);  // radius
  QByteArray pixels = h_->render_frame(seq.get(), 0);

  // (15,4) is 1 px inside the black cell on its right edge.
  // (17,4) is 1 px inside the white cell on its left edge.
  Rgba left = pixel_at(pixels, 64, 15, 4);
  Rgba right = pixel_at(pixels, 64, 17, 4);
  const int luma_left = (left.r + left.g + left.b) / 3;
  const int luma_right = (right.r + right.g + right.b) / 3;
  QVERIFY2(luma_left >= 60 && luma_left <= 200,
           qPrintable(QString("expected blurred luma in [60,200] at (15,4), got %1 (RGB=%2,%3,%4)")
                          .arg(luma_left)
                          .arg(left.r)
                          .arg(left.g)
                          .arg(left.b)));
  QVERIFY2(luma_right >= 60 && luma_right <= 200,
           qPrintable(QString("expected blurred luma in [60,200] at (17,4), got %1 (RGB=%2,%3,%4)")
                          .arg(luma_right)
                          .arg(right.r)
                          .arg(right.g)
                          .arg(right.b)));
}

void TestEffectsGpu::gaussianblurRadiusZero() {
  // gaussianblur.xml: row 0 = Sigma. Shader explicitly short-circuits at
  // sigma == 0 (radius_is_zero branch → texture(image, vTexCoord)).
  auto seq = h_->make_sequence(64, 64, 30.0);
  Clip* c = h_->add_generator_clip(seq.get(), -1, 0, 30, EFFECT_INTERNAL_GRADIENT);
  Effect* gradient = c->effects[1].get();
  h_->set_field_color(gradient, 1, 0, QColor(Qt::black));
  h_->set_field_color(gradient, 2, 0, QColor(Qt::white));
  h_->set_field_double(gradient, 3, 0, 90.0);
  QByteArray baseline = h_->render_frame(seq.get(), 0);

  EffectPtr fx = h_->attach_xml_shader(c, "Gaussian Blur");
  h_->set_field_double(fx.get(), 0, 0, 0.0);  // sigma=0
  QByteArray with_fx = h_->render_frame(seq.get(), 0);

  QVERIFY2(buffers_within(baseline, with_fx, /*tolerance=*/4),
           qPrintable(QString("expected sigma=0 passthrough, diff=%1").arg(buf_diff(baseline, with_fx))));
}

void TestEffectsGpu::gaussianblurSolidColor() {
  auto seq = h_->make_sequence(64, 64, 30.0);
  Clip* c = h_->add_generator_clip(seq.get(), -1, 0, 30, EFFECT_INTERNAL_SOLID);
  h_->set_field_color(c->effects[1].get(), 2, 0, QColor(255, 0, 0, 255));

  EffectPtr fx = h_->attach_xml_shader(c, "Gaussian Blur");
  // default sigma=5.5
  QByteArray pixels = h_->render_frame(seq.get(), 0);

  Rgba mid = pixel_at(pixels, 64, 32, 32);
  QVERIFY2(mid.r > 230 && mid.g < 30 && mid.b < 30,
           qPrintable(
               QString("expected solid red preserved at centre, got R=%1 G=%2 B=%3").arg(mid.r).arg(mid.g).arg(mid.b)));
}

void TestEffectsGpu::gaussianblurNonZero() {
  // Same checkerboard pattern as boxblurNonZero: sigma=4 must drag pixels
  // inside the cells (15,4)/(17,4) into mid-luma. Passthrough regression keeps
  // them at 0/255.
  auto seq = h_->make_sequence(64, 64, 30.0);
  Clip* c = h_->add_generator_clip(seq.get(), -1, 0, 30, EFFECT_INTERNAL_SOLID);
  Effect* solid = c->effects[1].get();
  h_->set_field_combo(solid, 0, 0, 2);  // Type = SOLID_TYPE_CHECKERBOARD
  h_->set_field_color(solid, 2, 0, QColor(Qt::white));
  h_->set_field_double(solid, 3, 0, 16.0);

  EffectPtr fx = h_->attach_xml_shader(c, "Gaussian Blur");
  h_->set_field_double(fx.get(), 0, 0, 4.0);  // sigma
  QByteArray pixels = h_->render_frame(seq.get(), 0);

  Rgba left = pixel_at(pixels, 64, 15, 4);
  Rgba right = pixel_at(pixels, 64, 17, 4);
  const int luma_left = (left.r + left.g + left.b) / 3;
  const int luma_right = (right.r + right.g + right.b) / 3;
  QVERIFY2(luma_left >= 60 && luma_left <= 200,
           qPrintable(QString("expected blurred luma in [60,200] at (15,4), got %1 (RGB=%2,%3,%4)")
                          .arg(luma_left)
                          .arg(left.r)
                          .arg(left.g)
                          .arg(left.b)));
  QVERIFY2(luma_right >= 60 && luma_right <= 200,
           qPrintable(QString("expected blurred luma in [60,200] at (17,4), got %1 (RGB=%2,%3,%4)")
                          .arg(luma_right)
                          .arg(right.r)
                          .arg(right.g)
                          .arg(right.b)));
}

void TestEffectsGpu::directionalblurRadiusZero() {
  // directionalblur.xml: row 0 = Length (default 10). length=0 → texture
  // passthrough (else branch in directionalblur.frag).
  auto seq = h_->make_sequence(64, 64, 30.0);
  Clip* c = h_->add_generator_clip(seq.get(), -1, 0, 30, EFFECT_INTERNAL_GRADIENT);
  Effect* gradient = c->effects[1].get();
  h_->set_field_color(gradient, 1, 0, QColor(Qt::black));
  h_->set_field_color(gradient, 2, 0, QColor(Qt::white));
  h_->set_field_double(gradient, 3, 0, 90.0);
  QByteArray baseline = h_->render_frame(seq.get(), 0);

  EffectPtr fx = h_->attach_xml_shader(c, "Directional Blur");
  h_->set_field_double(fx.get(), 0, 0, 0.0);  // length=0
  QByteArray with_fx = h_->render_frame(seq.get(), 0);

  QVERIFY2(buffers_within(baseline, with_fx, /*tolerance=*/4),
           qPrintable(QString("expected length=0 passthrough, diff=%1").arg(buf_diff(baseline, with_fx))));
}

void TestEffectsGpu::directionalblurSolidColor() {
  auto seq = h_->make_sequence(64, 64, 30.0);
  Clip* c = h_->add_generator_clip(seq.get(), -1, 0, 30, EFFECT_INTERNAL_SOLID);
  h_->set_field_color(c->effects[1].get(), 2, 0, QColor(255, 0, 0, 255));

  EffectPtr fx = h_->attach_xml_shader(c, "Directional Blur");
  // default length=10, angle=0
  QByteArray pixels = h_->render_frame(seq.get(), 0);

  Rgba mid = pixel_at(pixels, 64, 32, 32);
  QVERIFY2(mid.r > 230 && mid.g < 30 && mid.b < 30,
           qPrintable(
               QString("expected solid red preserved at centre, got R=%1 G=%2 B=%3").arg(mid.r).arg(mid.g).arg(mid.b)));
}

void TestEffectsGpu::directionalblurNonZero() {
  // SMPTE bars input breaks checkerboard row-symmetry so we can witness 1D
  // directionality. Top zone (y < 43) has pure vertical stripes of width 10:
  //   x∈[0,10) grey (192,192,192), x∈[10,20) yellow (192,192,0), …
  // Horizontal blur (angle=0) crosses the grey↔yellow boundary at x=10 →
  // blue channel drops at (9,10) (mixes grey B=192 with yellow B=0).
  // Vertical blur (angle=90) at the same x=9 only samples grey along Y → B
  // stays ≈ 192. Two renders, two assertions: directionality is proven.
  auto seq = h_->make_sequence(64, 64, 30.0);
  Clip* c = h_->add_generator_clip(seq.get(), -1, 0, 30, EFFECT_INTERNAL_SOLID);
  Effect* solid = c->effects[1].get();
  h_->set_field_combo(solid, 0, 0, 1);  // Type = SOLID_TYPE_BARS

  EffectPtr fx = h_->attach_xml_shader(c, "Directional Blur");
  h_->set_field_double(fx.get(), 0, 0, 10.0);  // blur_len
  h_->set_field_double(fx.get(), 1, 0, 0.0);   // angle = 0° → horizontal
  QByteArray horiz = h_->render_frame(seq.get(), 0);

  // (9,10): grey side, 1 px before the yellow boundary. Horizontal kernel
  // reaches into yellow (B=0) → B drops below 150. Passthrough keeps B=192.
  Rgba h_a = pixel_at(horiz, 64, 9, 10);
  QVERIFY2(h_a.b < 150, qPrintable(QString("expected horizontal blur to pull B<150 at (9,10), got RGB=%1,%2,%3")
                                       .arg(h_a.r)
                                       .arg(h_a.g)
                                       .arg(h_a.b)));
  // (9,35): same column, deeper Y but still in top-zone (first_bar_height≈43).
  // SMPTE bars are row-invariant in this band so horizontal blur sees the
  // identical X-pattern → B again drops. Confirms blur is X-uniform.
  Rgba h_b = pixel_at(horiz, 64, 9, 35);
  QVERIFY2(h_b.b < 150, qPrintable(QString("expected horizontal blur to pull B<150 at (9,35), got RGB=%1,%2,%3")
                                       .arg(h_b.r)
                                       .arg(h_b.g)
                                       .arg(h_b.b)));

  // Second render: same clip, change angle to 90° → vertical blur.
  h_->set_field_double(fx.get(), 1, 0, 90.0);
  QByteArray vert = h_->render_frame(seq.get(), 0);

  // (9,10) under vertical blur: kernel samples column 9 (grey) along Y for
  // [0, 20], all grey (top-zone runs to y≈43). B stays ≈ 192. Directionality
  // witness: angle=0 dropped B, angle=90 leaves it intact.
  Rgba v_a = pixel_at(vert, 64, 9, 10);
  QVERIFY2(v_a.b > 150, qPrintable(QString("expected vertical blur to leave B>150 at (9,10), got RGB=%1,%2,%3")
                                       .arg(v_a.r)
                                       .arg(v_a.g)
                                       .arg(v_a.b)));
}

void TestEffectsGpu::radialblurRadiusZero() {
  // radialblur.xml: row 0 = Radius. radius=0 → texture passthrough.
  auto seq = h_->make_sequence(64, 64, 30.0);
  Clip* c = h_->add_generator_clip(seq.get(), -1, 0, 30, EFFECT_INTERNAL_GRADIENT);
  Effect* gradient = c->effects[1].get();
  h_->set_field_color(gradient, 1, 0, QColor(Qt::black));
  h_->set_field_color(gradient, 2, 0, QColor(Qt::white));
  h_->set_field_double(gradient, 3, 0, 90.0);
  QByteArray baseline = h_->render_frame(seq.get(), 0);

  EffectPtr fx = h_->attach_xml_shader(c, "Radial Blur");
  h_->set_field_double(fx.get(), 0, 0, 0.0);  // radius=0
  QByteArray with_fx = h_->render_frame(seq.get(), 0);

  QVERIFY2(buffers_within(baseline, with_fx, /*tolerance=*/4),
           qPrintable(QString("expected radius=0 passthrough, diff=%1").arg(buf_diff(baseline, with_fx))));
}

void TestEffectsGpu::radialblurSolidColor() {
  auto seq = h_->make_sequence(64, 64, 30.0);
  Clip* c = h_->add_generator_clip(seq.get(), -1, 0, 30, EFFECT_INTERNAL_SOLID);
  h_->set_field_color(c->effects[1].get(), 2, 0, QColor(255, 0, 0, 255));

  EffectPtr fx = h_->attach_xml_shader(c, "Radial Blur");
  // default radius=100, center=(0,0)
  QByteArray pixels = h_->render_frame(seq.get(), 0);

  Rgba mid = pixel_at(pixels, 64, 32, 32);
  QVERIFY2(mid.r > 230 && mid.g < 30 && mid.b < 30,
           qPrintable(
               QString("expected solid red preserved at centre, got R=%1 G=%2 B=%3").arg(mid.r).arg(mid.g).arg(mid.b)));
}

void TestEffectsGpu::radialblurNonZero() {
  // White-on-black checkerboard, cell size 13. Radial blur centred at image
  // pixel (20, 6) — sits in WHITE cell (1, 0). Radius=20 makes the kernel
  // integrate ~3 taps across a ~2 px window along the radial direction.
  //
  // The previous "alpha < 10 at image centre" witness was wrong: gl_FragCoord
  // is half-pixel offset, so even at the image centre `distance` is (0.5, 0.5)
  // and the loop runs at least once — the shader never emits a clean vec4(0).
  // Instead we pick two pixels straddling cell boundaries so the kernel
  // integrates WHITE+BLACK; a passthrough regression would return the exact
  // cell colour at each pixel (luma=255 or 0).
  auto seq = h_->make_sequence(64, 64, 30.0);
  Clip* c = h_->add_generator_clip(seq.get(), -1, 0, 30, EFFECT_INTERNAL_SOLID);
  Effect* solid = c->effects[1].get();
  h_->set_field_combo(solid, 0, 0, 2);  // Type = SOLID_TYPE_CHECKERBOARD
  h_->set_field_color(solid, 2, 0, QColor(Qt::white));
  h_->set_field_double(solid, 3, 0, 13.0);

  EffectPtr fx = h_->attach_xml_shader(c, "Radial Blur");
  h_->set_field_double(fx.get(), 0, 0, 20.0);   // radius (pixel-scaled, gives ~3-tap kernel near edges)
  h_->set_field_double(fx.get(), 1, 0, -12.0);  // center_x: image centre + (-12) = pixel 20
  h_->set_field_double(fx.get(), 1, 1, -26.0);  // center_y: image centre + (-26) = pixel 6
  QByteArray pixels = h_->render_frame(seq.get(), 0);

  // Witness 1: pixel (20, 12). In WHITE cell (1, 0). Radial direction from
  // (20, 6) is straight +Y. Kernel reaches y≈14 → bleeds into BLACK cell
  // (1, 1) at y>=13. Expected luma ≈ 170 (two WHITE taps + one BLACK).
  // Passthrough returns WHITE (255) → fails the <220 bound.
  Rgba w1 = pixel_at(pixels, 64, 20, 12);
  const int luma_w1 = (w1.r + w1.g + w1.b) / 3;
  QVERIFY2(luma_w1 > 30 && luma_w1 < 220,
           qPrintable(QString("expected radial smear at (20,12) — 30<luma<220, got RGBA=%1,%2,%3,%4 (luma=%5)")
                          .arg(w1.r)
                          .arg(w1.g)
                          .arg(w1.b)
                          .arg(w1.a)
                          .arg(luma_w1)));

  // Witness 2: pixel (26, 6). In BLACK cell (2, 0). Radial direction from
  // (20, 6) is straight +X. Kernel reaches x≈24 → bleeds into WHITE cell
  // (1, 0) at x<26. Expected luma ≈ 85-127 (one BLACK + one boundary + one
  // WHITE tap). Passthrough returns BLACK (0) → fails the >30 bound.
  Rgba w2 = pixel_at(pixels, 64, 26, 6);
  const int luma_w2 = (w2.r + w2.g + w2.b) / 3;
  QVERIFY2(luma_w2 > 30 && luma_w2 < 220,
           qPrintable(QString("expected radial smear at (26,6) — 30<luma<220, got RGBA=%1,%2,%3,%4 (luma=%5)")
                          .arg(w2.r)
                          .arg(w2.g)
                          .arg(w2.b)
                          .arg(w2.a)
                          .arg(luma_w2)));
}

// ---------------------------------------------------------------------------
// Phase 3b — Distortions (XML shader effects)
// ---------------------------------------------------------------------------
//
// Centre-preserving distortions (bulge, fisheye, sphere) sample the input at
// (0.5, 0.5) when the distortion is centred at the origin (xoff=yoff=0). We
// assert the output centre pixel ≈ input centre pixel within ±8.
//
// Non-symmetric distortions (swirl, ripple, wave, turbulent) only require
// Pattern B: output differs from baseline.

void TestEffectsGpu::bulgeDistorts() {
  // bulge.xml: row 0 = Amount, row 1 = Center (xoff, yoff).
  auto seq = h_->make_sequence(64, 64, 30.0);
  Clip* c = h_->add_generator_clip(seq.get(), -1, 0, 30, EFFECT_INTERNAL_GRADIENT);
  Effect* gradient = c->effects[1].get();
  h_->set_field_color(gradient, 1, 0, QColor(Qt::black));
  h_->set_field_color(gradient, 2, 0, QColor(Qt::white));
  h_->set_field_double(gradient, 3, 0, 90.0);
  QByteArray baseline = h_->render_frame(seq.get(), 0);

  EffectPtr fx = h_->attach_xml_shader(c, "Bulge");
  // default amount=100 already produces a noticeable bulge.
  QByteArray with_fx = h_->render_frame(seq.get(), 0);

  const int diff = buf_diff(baseline, with_fx);
  QVERIFY2(diff > 1000, qPrintable(QString("expected bulge to change output, diff=%1").arg(diff)));
}

void TestEffectsGpu::bulgePreservesCenter() {
  // At xoff=yoff=0 the bulge is centred. At texCoord (0.5, 0.5) the radial
  // distortion maps to (0.5, 0.5) → output centre samples input centre.
  auto seq = h_->make_sequence(64, 64, 30.0);
  Clip* c = h_->add_generator_clip(seq.get(), -1, 0, 30, EFFECT_INTERNAL_GRADIENT);
  Effect* gradient = c->effects[1].get();
  h_->set_field_color(gradient, 1, 0, QColor(Qt::black));
  h_->set_field_color(gradient, 2, 0, QColor(Qt::white));
  h_->set_field_double(gradient, 3, 0, 90.0);
  QByteArray baseline = h_->render_frame(seq.get(), 0);

  EffectPtr fx = h_->attach_xml_shader(c, "Bulge");
  QByteArray with_fx = h_->render_frame(seq.get(), 0);

  Rgba mid_in = pixel_at(baseline, 64, 32, 32);
  Rgba mid_out = pixel_at(with_fx, 64, 32, 32);
  const int d = qAbs(mid_in.r - mid_out.r) + qAbs(mid_in.g - mid_out.g) + qAbs(mid_in.b - mid_out.b);
  QVERIFY2(d <= 24, qPrintable(QString("expected centre preserved, channel sum diff=%1").arg(d)));

  // Off-centre witness: pixel (32, 8) is on the Y-axis (vTexCoord ≈ (0.508,
  // 0.133) with gl_FragCoord half-pixel offset). The shader redistorts the
  // sample down the +Y radial. On a vertical gradient (slope ≈ 2.82 luma/px)
  // reading a different y → meaningfully different luma. Passthrough would
  // read (32, 8) unchanged → diff = 0.
  //   bulge: reads ~(32.3, 14.7), expected diff ≈ 19
  Rgba off_in = pixel_at(baseline, 64, 32, 8);
  Rgba off_out = pixel_at(with_fx, 64, 32, 8);
  const int luma_in = (off_in.r + off_in.g + off_in.b) / 3;
  const int luma_out = (off_out.r + off_out.g + off_out.b) / 3;
  const int luma_diff = qAbs(luma_in - luma_out);
  QVERIFY2(luma_diff >= 15, qPrintable(QString("off-centre (32,8) expected distorted luma diff >= 15, "
                                               "got %1 (baseline %2 → effect %3)")
                                           .arg(luma_diff)
                                           .arg(luma_in)
                                           .arg(luma_out)));
}

void TestEffectsGpu::fisheyeDistorts() {
  // fisheye.xml: row 0 = Size (default 100).
  auto seq = h_->make_sequence(64, 64, 30.0);
  Clip* c = h_->add_generator_clip(seq.get(), -1, 0, 30, EFFECT_INTERNAL_GRADIENT);
  Effect* gradient = c->effects[1].get();
  h_->set_field_color(gradient, 1, 0, QColor(Qt::black));
  h_->set_field_color(gradient, 2, 0, QColor(Qt::white));
  h_->set_field_double(gradient, 3, 0, 90.0);
  QByteArray baseline = h_->render_frame(seq.get(), 0);

  EffectPtr fx = h_->attach_xml_shader(c, "Fisheye");
  QByteArray with_fx = h_->render_frame(seq.get(), 0);

  const int diff = buf_diff(baseline, with_fx);
  QVERIFY2(diff > 1000, qPrintable(QString("expected fisheye to change output, diff=%1").arg(diff)));
}

void TestEffectsGpu::fisheyePreservesCenter() {
  // Fisheye is centred on (0.5, 0.5) by design — no offset field. At texCoord
  // (0.5, 0.5) the shader samples uv=(0.5, 0.5).
  auto seq = h_->make_sequence(64, 64, 30.0);
  Clip* c = h_->add_generator_clip(seq.get(), -1, 0, 30, EFFECT_INTERNAL_GRADIENT);
  Effect* gradient = c->effects[1].get();
  h_->set_field_color(gradient, 1, 0, QColor(Qt::black));
  h_->set_field_color(gradient, 2, 0, QColor(Qt::white));
  h_->set_field_double(gradient, 3, 0, 90.0);
  QByteArray baseline = h_->render_frame(seq.get(), 0);

  EffectPtr fx = h_->attach_xml_shader(c, "Fisheye");
  QByteArray with_fx = h_->render_frame(seq.get(), 0);

  Rgba mid_in = pixel_at(baseline, 64, 32, 32);
  Rgba mid_out = pixel_at(with_fx, 64, 32, 32);
  const int d = qAbs(mid_in.r - mid_out.r) + qAbs(mid_in.g - mid_out.g) + qAbs(mid_in.b - mid_out.b);
  QVERIFY2(d <= 24, qPrintable(QString("expected centre preserved, channel sum diff=%1").arg(d)));

  // Off-centre witness: pixel (32, 8) is on the Y-axis (vTexCoord ≈ (0.508,
  // 0.133) with gl_FragCoord half-pixel offset). The shader redistorts the
  // sample down the +Y radial. On a vertical gradient (slope ≈ 2.82 luma/px)
  // reading a different y → meaningfully different luma. Passthrough would
  // read (32, 8) unchanged → diff = 0.
  //   fisheye: reads ~(32.0, 15.2), expected diff ≈ 20
  Rgba off_in = pixel_at(baseline, 64, 32, 8);
  Rgba off_out = pixel_at(with_fx, 64, 32, 8);
  const int luma_in = (off_in.r + off_in.g + off_in.b) / 3;
  const int luma_out = (off_out.r + off_out.g + off_out.b) / 3;
  const int luma_diff = qAbs(luma_in - luma_out);
  QVERIFY2(luma_diff >= 15, qPrintable(QString("off-centre (32,8) expected distorted luma diff >= 15, "
                                               "got %1 (baseline %2 → effect %3)")
                                           .arg(luma_diff)
                                           .arg(luma_in)
                                           .arg(luma_out)));
}

void TestEffectsGpu::sphereDistorts() {
  // sphere.xml: row 0 = Center (xoff,yoff), row 1 = Scale, plus bool rows.
  auto seq = h_->make_sequence(64, 64, 30.0);
  Clip* c = h_->add_generator_clip(seq.get(), -1, 0, 30, EFFECT_INTERNAL_GRADIENT);
  Effect* gradient = c->effects[1].get();
  h_->set_field_color(gradient, 1, 0, QColor(Qt::black));
  h_->set_field_color(gradient, 2, 0, QColor(Qt::white));
  h_->set_field_double(gradient, 3, 0, 90.0);
  QByteArray baseline = h_->render_frame(seq.get(), 0);

  EffectPtr fx = h_->attach_xml_shader(c, "Sphere");
  QByteArray with_fx = h_->render_frame(seq.get(), 0);

  const int diff = buf_diff(baseline, with_fx);
  QVERIFY2(diff > 1000, qPrintable(QString("expected sphere to change output, diff=%1").arg(diff)));
}

void TestEffectsGpu::spherePreservesCenter() {
  // At texCoord (0.5, 0.5) with default xoff=yoff=0, scale=75, the sphere
  // shader computes uv near (0.5, 0.5) → centre sample roughly preserved.
  auto seq = h_->make_sequence(64, 64, 30.0);
  Clip* c = h_->add_generator_clip(seq.get(), -1, 0, 30, EFFECT_INTERNAL_GRADIENT);
  Effect* gradient = c->effects[1].get();
  h_->set_field_color(gradient, 1, 0, QColor(Qt::black));
  h_->set_field_color(gradient, 2, 0, QColor(Qt::white));
  h_->set_field_double(gradient, 3, 0, 90.0);
  QByteArray baseline = h_->render_frame(seq.get(), 0);

  EffectPtr fx = h_->attach_xml_shader(c, "Sphere");
  QByteArray with_fx = h_->render_frame(seq.get(), 0);

  Rgba mid_in = pixel_at(baseline, 64, 32, 32);
  Rgba mid_out = pixel_at(with_fx, 64, 32, 32);
  const int d = qAbs(mid_in.r - mid_out.r) + qAbs(mid_in.g - mid_out.g) + qAbs(mid_in.b - mid_out.b);
  // Loosened to 48 — the sphere's `f = (1-sqrt(1-r))/r` term contributes a small
  // shift even at the centre because of `1.5 - scale*0.01` and adj_tc==0 only
  // exactly at the screen centre, which falls on a pixel boundary.
  QVERIFY2(d <= 48, qPrintable(QString("expected centre preserved, channel sum diff=%1").arg(d)));

  // Off-centre witness: pixel (32, 8) is on the Y-axis (vTexCoord ≈ (0.508,
  // 0.133) with gl_FragCoord half-pixel offset). The shader redistorts the
  // sample down the +Y radial. On a vertical gradient (slope ≈ 2.82 luma/px)
  // reading a different y → meaningfully different luma. Passthrough would
  // read (32, 8) unchanged → diff = 0.
  //   sphere: reads ~(32.4, 11.0), expected diff ≈ 10
  Rgba off_in = pixel_at(baseline, 64, 32, 8);
  Rgba off_out = pixel_at(with_fx, 64, 32, 8);
  const int luma_in = (off_in.r + off_in.g + off_in.b) / 3;
  const int luma_out = (off_out.r + off_out.g + off_out.b) / 3;
  const int luma_diff = qAbs(luma_in - luma_out);
  QVERIFY2(luma_diff >= 5, qPrintable(QString("off-centre (32,8) expected distorted luma diff >= 5, "
                                              "got %1 (baseline %2 → effect %3)")
                                          .arg(luma_diff)
                                          .arg(luma_in)
                                          .arg(luma_out)));
}

void TestEffectsGpu::swirlDistorts() {
  // swirl.xml: row 0 = Radius (default 200), row 1 = Angle (default 10).
  auto seq = h_->make_sequence(64, 64, 30.0);
  Clip* c = h_->add_generator_clip(seq.get(), -1, 0, 30, EFFECT_INTERNAL_GRADIENT);
  Effect* gradient = c->effects[1].get();
  h_->set_field_color(gradient, 1, 0, QColor(Qt::black));
  h_->set_field_color(gradient, 2, 0, QColor(Qt::white));
  h_->set_field_double(gradient, 3, 0, 90.0);
  QByteArray baseline = h_->render_frame(seq.get(), 0);

  EffectPtr fx = h_->attach_xml_shader(c, "Swirl");
  // Defaults already swirl (radius=200, angle=10).
  QByteArray with_fx = h_->render_frame(seq.get(), 0);

  const int diff = buf_diff(baseline, with_fx);
  QVERIFY2(diff > 1000, qPrintable(QString("expected swirl to change output, diff=%1").arg(diff)));
}

void TestEffectsGpu::rippleDistorts() {
  // ripple.xml: defaults intensity=100, frequency=100, speed=100 → clear ripple.
  auto seq = h_->make_sequence(64, 64, 30.0);
  Clip* c = h_->add_generator_clip(seq.get(), -1, 0, 30, EFFECT_INTERNAL_GRADIENT);
  Effect* gradient = c->effects[1].get();
  h_->set_field_color(gradient, 1, 0, QColor(Qt::black));
  h_->set_field_color(gradient, 2, 0, QColor(Qt::white));
  h_->set_field_double(gradient, 3, 0, 90.0);
  QByteArray baseline = h_->render_frame(seq.get(), 0);

  EffectPtr fx = h_->attach_xml_shader(c, "Ripple");
  QByteArray with_fx = h_->render_frame(seq.get(), 0);

  const int diff = buf_diff(baseline, with_fx);
  QVERIFY2(diff > 1000, qPrintable(QString("expected ripple to change output, diff=%1").arg(diff)));
}

void TestEffectsGpu::waveDistorts() {
  // wave.xml: defaults frequency=10, intensity=10.
  auto seq = h_->make_sequence(64, 64, 30.0);
  Clip* c = h_->add_generator_clip(seq.get(), -1, 0, 30, EFFECT_INTERNAL_GRADIENT);
  Effect* gradient = c->effects[1].get();
  h_->set_field_color(gradient, 1, 0, QColor(Qt::black));
  h_->set_field_color(gradient, 2, 0, QColor(Qt::white));
  h_->set_field_double(gradient, 3, 0, 90.0);
  QByteArray baseline = h_->render_frame(seq.get(), 0);

  EffectPtr fx = h_->attach_xml_shader(c, "Wave");
  QByteArray with_fx = h_->render_frame(seq.get(), 0);

  const int diff = buf_diff(baseline, with_fx);
  QVERIFY2(diff > 1000, qPrintable(QString("expected wave to change output, diff=%1").arg(diff)));
}

void TestEffectsGpu::chromaticAberrationShifts() {
  // chromaticaberration.xml: rows R/G/B amounts. Defaults already non-zero.
  // On a uniform white input the per-channel UV shifts sample the same white
  // texels — output stays mostly white. Use a gradient so channels separate
  // visibly at the colour boundary.
  auto seq = h_->make_sequence(64, 64, 30.0);
  Clip* c = h_->add_generator_clip(seq.get(), -1, 0, 30, EFFECT_INTERNAL_GRADIENT);
  Effect* gradient = c->effects[1].get();
  h_->set_field_color(gradient, 1, 0, QColor(Qt::black));
  h_->set_field_color(gradient, 2, 0, QColor(Qt::white));
  h_->set_field_double(gradient, 3, 0, 90.0);
  QByteArray baseline = h_->render_frame(seq.get(), 0);

  EffectPtr fx = h_->attach_xml_shader(c, "Chromatic Aberration");
  QByteArray with_fx = h_->render_frame(seq.get(), 0);

  // Output must differ from a grey-gradient baseline (where R==G==B per pixel)
  // because aberration shifts each channel independently.
  const int diff = buf_diff(baseline, with_fx);
  QVERIFY2(diff > 1000, qPrintable(QString("expected aberration to change output, diff=%1").arg(diff)));

  // Look for at least one pixel where channels visibly differ (R != B).
  bool found_split = false;
  for (int i = 0; i + 3 < with_fx.size(); i += 4) {
    const int r = static_cast<uchar>(with_fx[i + 0]);
    const int b = static_cast<uchar>(with_fx[i + 2]);
    if (qAbs(r - b) > 10) {
      found_split = true;
      break;
    }
  }
  QVERIFY2(found_split, "expected at least one pixel with R != B by > 10 (channels separated)");
}

void TestEffectsGpu::tileRepeats() {
  // tile.xml row 0 = Scale (percent). adj_scale = scale/100. Output coord
  // sampler does `vTexCoord/adj_scale` so tile_count_across = 1/adj_scale.
  // Scale=25 → adj_scale=0.25 → 4 tiles across 64 px → tile width = 16 px.
  auto seq = h_->make_sequence(64, 64, 30.0);
  Clip* c = h_->add_generator_clip(seq.get(), -1, 0, 30, EFFECT_INTERNAL_GRADIENT);
  Effect* gradient = c->effects[1].get();
  h_->set_field_color(gradient, 1, 0, QColor(Qt::black));
  h_->set_field_color(gradient, 2, 0, QColor(Qt::red));
  h_->set_field_double(gradient, 3, 0, 0.0);  // left→right

  EffectPtr fx = h_->attach_xml_shader(c, "Tile");
  h_->set_field_double(fx.get(), 0, 0, 25.0);  // Scale=25 → 16-px tiles
  // Mirror flags default to false.
  QByteArray with_fx = h_->render_frame(seq.get(), 0);

  // Two pixels 16 px apart at the same y should sample equivalent texCoord
  // due to the mod(coord, 1.0) wrapping in tile.frag.
  Rgba a = pixel_at(with_fx, 64, 16, 32);
  Rgba b = pixel_at(with_fx, 64, 32, 32);
  const int d = qAbs(a.r - b.r) + qAbs(a.g - b.g) + qAbs(a.b - b.b);
  QVERIFY2(d <= 30, qPrintable(QString("expected tile repeat: (16,32)=(%1,%2,%3) (32,32)=(%4,%5,%6) sum-diff=%7")
                                   .arg(a.r)
                                   .arg(a.g)
                                   .arg(a.b)
                                   .arg(b.r)
                                   .arg(b.g)
                                   .arg(b.b)
                                   .arg(d)));
}

void TestEffectsGpu::turbulentDisplaces() {
  // turbulentdisplacement.xml: defaults amplitude_x=5, amplitude_y=5, scale=4.
  auto seq = h_->make_sequence(64, 64, 30.0);
  Clip* c = h_->add_generator_clip(seq.get(), -1, 0, 30, EFFECT_INTERNAL_GRADIENT);
  Effect* gradient = c->effects[1].get();
  h_->set_field_color(gradient, 1, 0, QColor(Qt::black));
  h_->set_field_color(gradient, 2, 0, QColor(Qt::white));
  h_->set_field_double(gradient, 3, 0, 90.0);
  QByteArray baseline = h_->render_frame(seq.get(), 0);

  EffectPtr fx = h_->attach_xml_shader(c, "Turbulent Displacement");
  QByteArray with_fx = h_->render_frame(seq.get(), 0);

  const int diff = buf_diff(baseline, with_fx);
  QVERIFY2(diff > 1000, qPrintable(QString("expected turbulent to change output, diff=%1").arg(diff)));
}

// ---------------------------------------------------------------------------
// Phase 3c — Color & key (XML shader effects)
// ---------------------------------------------------------------------------

void TestEffectsGpu::invertSwapsChannels() {
  // invert.xml row 0 = Amount (default 100). At amount=100 the shader computes
  // col = rgb + (1 - 2*rgb) = 1 - rgb → exact channel-wise inversion.
  // Input (50, 100, 150) → Output (205, 155, 105) ±2.
  auto seq = h_->make_sequence(64, 64, 30.0);
  Clip* c = h_->add_generator_clip(seq.get(), -1, 0, 30, EFFECT_INTERNAL_SOLID);
  h_->set_field_color(c->effects[1].get(), 2, 0, QColor(50, 100, 150, 255));

  h_->attach_xml_shader(c, "Invert");
  QByteArray pixels = h_->render_frame(seq.get(), 0);

  Rgba mid = pixel_at(pixels, 64, 32, 32);
  QVERIFY2(qAbs(mid.r - 205) <= 4 && qAbs(mid.g - 155) <= 4 && qAbs(mid.b - 105) <= 4,
           qPrintable(QString("expected (205,155,105), got (%1,%2,%3)").arg(mid.r).arg(mid.g).arg(mid.b)));
}

void TestEffectsGpu::lutIdentityPassthrough() {
  QTemporaryDir dir;
  const QString cube = dir.filePath("t.cube");
  QVERIFY(write_cube(cube, 2, [](QVector3D v) { return v; }));
  auto seq = h_->make_sequence(64, 64, 30.0);
  Clip* c = h_->add_generator_clip(seq.get(), -1, 0, 30, EFFECT_INTERNAL_SOLID);
  h_->set_field_color(c->effects[1].get(), 2, 0, QColor(50, 100, 150, 255));
  EffectPtr lut = h_->attach_xml_shader(c, "LUT");
  h_->set_field_string(lut.get(), 0, 0, cube);
  QByteArray pixels = h_->render_frame(seq.get(), 0);
  Rgba mid = pixel_at(pixels, 64, 32, 32);
  QVERIFY2(qAbs(mid.r - 50) <= 3 && qAbs(mid.g - 100) <= 3 && qAbs(mid.b - 150) <= 3,
           qPrintable(QString("expected (50,100,150), got (%1,%2,%3)").arg(mid.r).arg(mid.g).arg(mid.b)));
}

void TestEffectsGpu::lutInvertFullStrength() {
  QTemporaryDir dir;
  const QString cube = dir.filePath("t.cube");
  QVERIFY(write_cube(cube, 2, [](QVector3D v) { return QVector3D(1, 1, 1) - v; }));
  auto seq = h_->make_sequence(64, 64, 30.0);
  Clip* c = h_->add_generator_clip(seq.get(), -1, 0, 30, EFFECT_INTERNAL_SOLID);
  h_->set_field_color(c->effects[1].get(), 2, 0, QColor(50, 100, 150, 255));
  EffectPtr lut = h_->attach_xml_shader(c, "LUT");
  h_->set_field_string(lut.get(), 0, 0, cube);
  QByteArray pixels = h_->render_frame(seq.get(), 0);
  Rgba mid = pixel_at(pixels, 64, 32, 32);
  QVERIFY2(qAbs(mid.r - 205) <= 3 && qAbs(mid.g - 155) <= 3 && qAbs(mid.b - 105) <= 3,
           qPrintable(QString("expected (205,155,105), got (%1,%2,%3)").arg(mid.r).arg(mid.g).arg(mid.b)));
}

void TestEffectsGpu::lutHalfStrengthBlends() {
  // Strength 50 blends original and LUT output: (50+205)/2, (100+155)/2, (150+105)/2 = 127.5 each.
  QTemporaryDir dir;
  const QString cube = dir.filePath("t.cube");
  QVERIFY(write_cube(cube, 2, [](QVector3D v) { return QVector3D(1, 1, 1) - v; }));
  auto seq = h_->make_sequence(64, 64, 30.0);
  Clip* c = h_->add_generator_clip(seq.get(), -1, 0, 30, EFFECT_INTERNAL_SOLID);
  h_->set_field_color(c->effects[1].get(), 2, 0, QColor(50, 100, 150, 255));
  EffectPtr lut = h_->attach_xml_shader(c, "LUT");
  h_->set_field_string(lut.get(), 0, 0, cube);
  h_->set_field_double(lut.get(), 1, 0, 50.0);
  QByteArray pixels = h_->render_frame(seq.get(), 0);
  Rgba mid = pixel_at(pixels, 64, 32, 32);
  QVERIFY2(qAbs(mid.r - 128) <= 3 && qAbs(mid.g - 128) <= 3 && qAbs(mid.b - 128) <= 3,
           qPrintable(QString("expected ~(128,128,128), got (%1,%2,%3)").arg(mid.r).arg(mid.g).arg(mid.b)));
}

void TestEffectsGpu::lutSemiTransparentGradesStraightColour() {
  // Premultiplied input: the LUT must grade the straight colour, so an invert LUT on (50,100,150) at half alpha gives
  // (205,155,105) premultiplied by that alpha. Grading the premultiplied rgb gives rgb > alpha: a veil.
  QTemporaryDir dir;
  const QString cube = dir.filePath("t.cube");
  QVERIFY(write_cube(cube, 2, [](QVector3D v) { return QVector3D(1, 1, 1) - v; }));
  auto seq = h_->make_sequence(64, 64, 30.0);
  Clip* c = h_->add_generator_clip(seq.get(), -1, 0, 30, EFFECT_INTERNAL_SOLID);
  h_->set_field_double(c->effects[1].get(), 1, 0, 50.0);  // Solid opacity 50% → alpha 128
  h_->set_field_color(c->effects[1].get(), 2, 0, QColor(50, 100, 150));
  EffectPtr lut = h_->attach_xml_shader(c, "LUT");
  h_->set_field_string(lut.get(), 0, 0, cube);
  QByteArray pixels = h_->render_frame(seq.get(), 0);
  // The readback forces alpha to 255 (never assert alpha), its rgb is the premultiplied value: (205,155,105) * 128/255
  Rgba mid = pixel_at(pixels, 64, 32, 32);
  QVERIFY2(qAbs(mid.r - 103) <= 4 && qAbs(mid.g - 78) <= 4 && qAbs(mid.b - 53) <= 4,
           qPrintable(QString("expected ~(103,78,53), got (%1,%2,%3)").arg(mid.r).arg(mid.g).arg(mid.b)));
}

void TestEffectsGpu::lutNoFilePassthrough() {
  // A LUT effect just added, no file picked yet: the identity fallback must bind, the image passes through unchanged.
  // A gradient, not a solid: with no LUT texture bound, GL samples the image itself as the LUT, which a uniform colour
  // would hide.
  auto seq = h_->make_sequence(64, 64, 30.0);
  Clip* c = h_->add_generator_clip(seq.get(), -1, 0, 30, EFFECT_INTERNAL_GRADIENT);
  Effect* gradient = c->effects[1].get();
  h_->set_field_color(gradient, 1, 0, QColor(Qt::black));
  h_->set_field_color(gradient, 2, 0, QColor(Qt::white));
  h_->set_field_double(gradient, 3, 0, 90.0);  // top→bottom
  QByteArray before = h_->render_frame(seq.get(), 0);
  h_->attach_xml_shader(c, "LUT");
  QByteArray after = h_->render_frame(seq.get(), 0);
  QVERIFY(!before.isEmpty());
  QCOMPARE(after.size(), before.size());
  Rgba top = pixel_at(before, 64, 32, 4);
  Rgba bot = pixel_at(before, 64, 32, 60);
  QVERIFY2(bot.r > top.r + 100, "reference gradient missing");
  // 2x2x2 identity LUT through linear filtering: allow a couple of levels per channel
  const int diff = buf_diff(before, after);
  QVERIFY2(diff <= 64 * 64 * 3 * 2, qPrintable(QString("LUT with no file changed the image, diff=%1").arg(diff)));
}

void TestEffectsGpu::huesatbriSaturationZero() {
  // huesatbri.xml row 1 = Saturation (default 100, range 0..∞). Setting to 0
  // multiplies HSV S by 0 → greyscale: R == G == B per pixel.
  auto seq = h_->make_sequence(64, 64, 30.0);
  Clip* c = h_->add_generator_clip(seq.get(), -1, 0, 30, EFFECT_INTERNAL_SOLID);
  h_->set_field_color(c->effects[1].get(), 2, 0, QColor(200, 50, 50, 255));

  EffectPtr fx = h_->attach_xml_shader(c, "Hue/Saturation/Brightness");
  h_->set_field_double(fx.get(), 1, 0, 0.0);  // saturation=0
  QByteArray pixels = h_->render_frame(seq.get(), 0);

  Rgba mid = pixel_at(pixels, 64, 32, 32);
  // Per-pixel R≈G≈B within ±4 (HSV round-trip has small float drift).
  QVERIFY2(qAbs(mid.r - mid.g) <= 4 && qAbs(mid.g - mid.b) <= 4 && qAbs(mid.r - mid.b) <= 4,
           qPrintable(QString("expected greyscale, got (%1,%2,%3)").arg(mid.r).arg(mid.g).arg(mid.b)));
}

void TestEffectsGpu::huesatbriContrast() {
  // huesatbri contrast: rgb = (rgb-0.5)*(contrast*0.01)+0.5. With contrast=200
  // (2x) on a black→white vertical gradient, low pixel (y=10, luma≈0.262) is
  // pushed toward black and high pixel (y=54, luma≈0.749) toward white.
  // Predicted R 8-bit values: low ≈ 6, high ≈ 254.
  auto seq = h_->make_sequence(64, 64, 30.0);
  Clip* c = h_->add_generator_clip(seq.get(), -1, 0, 30, EFFECT_INTERNAL_GRADIENT);
  Effect* gradient = c->effects[1].get();
  h_->set_field_color(gradient, 1, 0, QColor(Qt::black));
  h_->set_field_color(gradient, 2, 0, QColor(Qt::white));
  h_->set_field_double(gradient, 3, 0, 90.0);

  EffectPtr fx = h_->attach_xml_shader(c, "Hue/Saturation/Brightness");
  h_->set_field_double(fx.get(), 3, 0, 200.0);  // contrast=200 (row 3)
  QByteArray pixels = h_->render_frame(seq.get(), 0);

  Rgba low = pixel_at(pixels, 64, 32, 10);
  Rgba high = pixel_at(pixels, 64, 32, 54);
  QVERIFY2(low.r < 30, qPrintable(QString("expected contrast push low pixel toward black, low.r=%1").arg(low.r)));
  QVERIFY2(high.r > 220, qPrintable(QString("expected contrast push high pixel toward white, high.r=%1").arg(high.r)));
}

void TestEffectsGpu::huesatbriHueRotate() {
  // huesatbri row 0 = Hue. Shader applies `hsv.r += hue/360`. Red (1,0,0) -> HSV (0,1,1).
  // Hue=+120 -> HSV (1/3, 1, 1) -> RGB (0,1,0) = green. Passthrough fails `mid.g > 200`.
  auto seq = h_->make_sequence(64, 64, 30.0);
  Clip* c = h_->add_generator_clip(seq.get(), -1, 0, 30, EFFECT_INTERNAL_SOLID);
  h_->set_field_color(c->effects[1].get(), 2, 0, QColor(255, 0, 0, 255));

  EffectPtr fx = h_->attach_xml_shader(c, "Hue/Saturation/Brightness");
  h_->set_field_double(fx.get(), 0, 0, 120.0);  // hue +120 -> red -> green
  QByteArray pixels = h_->render_frame(seq.get(), 0);

  Rgba mid = pixel_at(pixels, 64, 32, 32);
  QVERIFY2(mid.g > 200 && mid.r < 30 && mid.b < 30,
           qPrintable(QString("expected red rotated to green, got (%1,%2,%3)").arg(mid.r).arg(mid.g).arg(mid.b)));
}

void TestEffectsGpu::colorCorrectionShift() {
  // colorcorrection.xml rows: 0=temperature, 8=saturation, 2=exposure. Three
  // sub-scopes verify independent shader steps — a passthrough regression
  // would fail B (R≠G after sat=0 → intensity grey) and C (channels unchanged
  // by exposure).
  //
  // A. Temperature=5000 → temp=50, warm branch (R=1, G≈0.89, B≈0.81).
  {
    auto seq = h_->make_sequence(64, 64, 30.0);
    Clip* c = h_->add_generator_clip(seq.get(), -1, 0, 30, EFFECT_INTERNAL_SOLID);
    h_->set_field_color(c->effects[1].get(), 2, 0, QColor(100, 100, 100, 255));
    EffectPtr fx = h_->attach_xml_shader(c, "Color Correction");
    h_->set_field_double(fx.get(), 0, 0, 5000.0);  // temperature
    QByteArray pixels = h_->render_frame(seq.get(), 0);
    Rgba mid = pixel_at(pixels, 64, 32, 32);
    QVERIFY2(mid.r > mid.g && mid.g > mid.b && (mid.r - mid.b) > 5,
             qPrintable(QString("expected warm shift R>G>B, got (%1,%2,%3)").arg(mid.r).arg(mid.g).arg(mid.b)));
  }
  // B. Saturation=0 on (200,50,50) → intensity grey via Rec.709 weights
  //    (0.2125·R + 0.7154·G + 0.0721·B) ≈ 0.321 → ≈ 82 across all channels.
  {
    auto seq = h_->make_sequence(64, 64, 30.0);
    Clip* c = h_->add_generator_clip(seq.get(), -1, 0, 30, EFFECT_INTERNAL_SOLID);
    h_->set_field_color(c->effects[1].get(), 2, 0, QColor(200, 50, 50, 255));
    EffectPtr fx = h_->attach_xml_shader(c, "Color Correction");
    h_->set_field_double(fx.get(), 8, 0, 0.0);  // saturation
    QByteArray pixels = h_->render_frame(seq.get(), 0);
    Rgba mid = pixel_at(pixels, 64, 32, 32);
    QVERIFY2(qAbs(mid.r - mid.g) <= 5 && qAbs(mid.g - mid.b) <= 5,
             qPrintable(QString("expected sat=0 → grey, got (%1,%2,%3)").arg(mid.r).arg(mid.g).arg(mid.b)));
  }
  // C. Exposure=50 on (100,100,100) → rgb *= pow(2, 0.5) ≈ 1.414 → ≈ 141.
  {
    auto seq = h_->make_sequence(64, 64, 30.0);
    Clip* c = h_->add_generator_clip(seq.get(), -1, 0, 30, EFFECT_INTERNAL_SOLID);
    h_->set_field_color(c->effects[1].get(), 2, 0, QColor(100, 100, 100, 255));
    EffectPtr fx = h_->attach_xml_shader(c, "Color Correction");
    h_->set_field_double(fx.get(), 2, 0, 50.0);  // exposure
    QByteArray pixels = h_->render_frame(seq.get(), 0);
    Rgba mid = pixel_at(pixels, 64, 32, 32);
    QVERIFY2(mid.r > 120 && mid.g > 120 && mid.b > 120,
             qPrintable(QString("expected exposure brighten, got (%1,%2,%3)").arg(mid.r).arg(mid.g).arg(mid.b)));
  }
}

void TestEffectsGpu::colorselIsolates() {
  // colorsel.xml: row 0 = Find by (combo, default 0=Luminance), row 1 = Lower
  // Limit (0..100, default 0), row 2 = Upper Limit (0..100, default 100), row
  // 3 = Invert (malformed in XML — leave at default).
  //
  // Solid (255, 0, 0). rgb2luma = (255+0)/2 = 127.5 → 50%.
  // Test A: limits [0..100] (defaults) include 50% → output stays red.
  // Test B: limits [80..100] exclude 50% → alpha=0, premultiplied to
  //         (0,0,0,0); compositor forces final alpha=255 → reads as black.
  {
    auto seq = h_->make_sequence(64, 64, 30.0);
    Clip* c = h_->add_generator_clip(seq.get(), -1, 0, 30, EFFECT_INTERNAL_SOLID);
    h_->set_field_color(c->effects[1].get(), 2, 0, QColor(255, 0, 0, 255));
    h_->attach_xml_shader(c, "Color Finder");
    QByteArray pixels = h_->render_frame(seq.get(), 0);
    Rgba mid = pixel_at(pixels, 64, 32, 32);
    QVERIFY2(mid.r > 200 && mid.g < 30 && mid.b < 30,
             qPrintable(QString("expected red kept, got (%1,%2,%3)").arg(mid.r).arg(mid.g).arg(mid.b)));
  }
  {
    auto seq = h_->make_sequence(64, 64, 30.0);
    Clip* c = h_->add_generator_clip(seq.get(), -1, 0, 30, EFFECT_INTERNAL_SOLID);
    h_->set_field_color(c->effects[1].get(), 2, 0, QColor(255, 0, 0, 255));
    EffectPtr fx = h_->attach_xml_shader(c, "Color Finder");
    h_->set_field_double(fx.get(), 1, 0, 80.0);   // Lower Limit
    h_->set_field_double(fx.get(), 2, 0, 100.0);  // Upper Limit
    QByteArray pixels = h_->render_frame(seq.get(), 0);
    Rgba mid = pixel_at(pixels, 64, 32, 32);
    QVERIFY2(mid.r < 30 && mid.g < 30 && mid.b < 30,
             qPrintable(QString("expected red filtered, got (%1,%2,%3)").arg(mid.r).arg(mid.g).arg(mid.b)));
  }
}

void TestEffectsGpu::colorselHueRanges() {
  // colorsel.xml row 0 = Find by (combo, set to 2 = Hue). Row 1 = Lower Limit,
  // row 2 = Upper Limit. Shader maps HSV.x (degrees) / 3.6 → toCheck in 0..100.
  // Pure-saturated corners have integer-valued hues (0, 60, 120, 180, 240, 300),
  // so toCheck is exactly 0, 16.7, 33.3, 50, 66.7, 83.3. Each iteration runs
  // two sub-scopes: A feeds the target colour with a ±5 window around its hue
  // (preserved), B feeds a non-target colour against the same window (suppressed
  // → premultiplied black). Sub-scope B is the passthrough catcher for the slot.
  struct HueCase {
    const char* name;
    QColor target;
    QColor other;
    double loc;
    double hic;
    // Witness for the *target* (preserved) sub-scope: dominant channel(s) high,
    // other channels low. Encoded as four expected min/max ranges (r/g/b).
    int r_min, r_max;
    int g_min, g_max;
    int b_min, b_max;
  };
  const HueCase cases[] = {
      {"red", QColor(255, 0, 0), QColor(0, 255, 0), 0.0, 5.0, 200, 255, 0, 30, 0, 30},
      {"green", QColor(0, 255, 0), QColor(255, 0, 0), 28.0, 38.0, 0, 30, 200, 255, 0, 30},
      {"blue", QColor(0, 0, 255), QColor(255, 0, 0), 62.0, 72.0, 0, 30, 0, 30, 200, 255},
      {"yellow", QColor(255, 255, 0), QColor(255, 0, 0), 12.0, 22.0, 200, 255, 200, 255, 0, 30},
      {"magenta", QColor(255, 0, 255), QColor(255, 0, 0), 78.0, 88.0, 200, 255, 0, 30, 200, 255},
  };
  for (const auto& c : cases) {
    // Sub-scope A: target colour inside the hue window → preserved.
    {
      auto seq = h_->make_sequence(64, 64, 30.0);
      Clip* clip = h_->add_generator_clip(seq.get(), -1, 0, 30, EFFECT_INTERNAL_SOLID);
      h_->set_field_color(clip->effects[1].get(), 2, 0, c.target);
      EffectPtr fx = h_->attach_xml_shader(clip, "Color Finder");
      h_->set_field_combo(fx.get(), 0, 0, 2);  // Find by = Hue
      h_->set_field_double(fx.get(), 1, 0, c.loc);
      h_->set_field_double(fx.get(), 2, 0, c.hic);
      QByteArray pixels = h_->render_frame(seq.get(), 0);
      Rgba mid = pixel_at(pixels, 64, 32, 32);
      QVERIFY2(mid.r >= c.r_min && mid.r <= c.r_max && mid.g >= c.g_min && mid.g <= c.g_max && mid.b >= c.b_min &&
                   mid.b <= c.b_max,
               qPrintable(
                   QString("[%1] expected target preserved in range r=[%2..%3] g=[%4..%5] b=[%6..%7], got (%8,%9,%10)")
                       .arg(c.name)
                       .arg(c.r_min)
                       .arg(c.r_max)
                       .arg(c.g_min)
                       .arg(c.g_max)
                       .arg(c.b_min)
                       .arg(c.b_max)
                       .arg(mid.r)
                       .arg(mid.g)
                       .arg(mid.b)));
    }
    // Sub-scope B: non-target colour against same window → suppressed (black).
    {
      auto seq = h_->make_sequence(64, 64, 30.0);
      Clip* clip = h_->add_generator_clip(seq.get(), -1, 0, 30, EFFECT_INTERNAL_SOLID);
      h_->set_field_color(clip->effects[1].get(), 2, 0, c.other);
      EffectPtr fx = h_->attach_xml_shader(clip, "Color Finder");
      h_->set_field_combo(fx.get(), 0, 0, 2);  // Find by = Hue
      h_->set_field_double(fx.get(), 1, 0, c.loc);
      h_->set_field_double(fx.get(), 2, 0, c.hic);
      QByteArray pixels = h_->render_frame(seq.get(), 0);
      Rgba mid = pixel_at(pixels, 64, 32, 32);
      QVERIFY2(mid.r < 30 && mid.g < 30 && mid.b < 30,
               qPrintable(QString("[%1] expected non-target suppressed → black, got (%2,%3,%4)")
                              .arg(c.name)
                              .arg(mid.r)
                              .arg(mid.g)
                              .arg(mid.b)));
    }
  }
}

void TestEffectsGpu::posterizeReducesLevels() {
  // Posterize with gamma_cent=100 → gamma=1 → output = floor(c·N)/N. Loop
  // over numColors ∈ {2,4,8}; gradient t-range [0.152, 0.848] yields N (or
  // N-2) distinct R values per iter. Assert ≤ N+2 to absorb 8-bit boundary
  // aliasing. Passthrough would produce ~64 distinct values → fails all N.
  const int test_levels[] = {2, 4, 8};
  for (int N : test_levels) {
    auto seq = h_->make_sequence(64, 64, 30.0);
    Clip* c = h_->add_generator_clip(seq.get(), -1, 0, 30, EFFECT_INTERNAL_GRADIENT);
    Effect* gradient = c->effects[1].get();
    h_->set_field_color(gradient, 1, 0, QColor(Qt::black));
    h_->set_field_color(gradient, 2, 0, QColor(Qt::white));
    h_->set_field_double(gradient, 3, 0, 90.0);

    EffectPtr fx = h_->attach_xml_shader(c, "Posterize");
    h_->set_field_double(fx.get(), 0, 0, double(N));  // numColors
    h_->set_field_double(fx.get(), 1, 0, 100.0);      // gamma_cent → gamma=1
    QByteArray pixels = h_->render_frame(seq.get(), 0);

    QSet<int> r_values;
    for (int i = 0; i + 3 < pixels.size(); i += 4) {
      r_values.insert(static_cast<uchar>(pixels[i + 0]));
    }
    QVERIFY2(
        r_values.size() <= N + 2,
        qPrintable(
            QString("posterize N=%1 expected ≤ %2 distinct R values, got %3").arg(N).arg(N + 2).arg(r_values.size())));
  }
}

void TestEffectsGpu::chromakeyTransparent() {
  // chromakey.xml row 0 = Mode (combo, default 0=Composite). Setting Mode=1
  // (Alpha) — keyed pixel renders as vec3(mask=0) → black after compositing.
  // Iterate over three key colours with a fresh seq+clip+fx per iter to catch
  // "key channel hardcoded to green" regressions.
  struct KeyCase {
    QColor key;
    const char* name;
  };
  const KeyCase cases[] = {
      {QColor(0, 255, 0), "green"},
      {QColor(0, 0, 255), "blue"},
      {QColor(255, 0, 255), "magenta"},
  };
  for (const auto& kc : cases) {
    auto seq = h_->make_sequence(64, 64, 30.0);
    Clip* c = h_->add_generator_clip(seq.get(), -1, 0, 30, EFFECT_INTERNAL_SOLID);
    h_->set_field_color(c->effects[1].get(), 2, 0, kc.key);

    EffectPtr fx = h_->attach_xml_shader(c, "Chroma Key");
    h_->set_field_combo(fx.get(), 0, 0, 1);       // Mode = Alpha
    h_->set_field_color(fx.get(), 1, 0, kc.key);  // Key Color
    QByteArray pixels = h_->render_frame(seq.get(), 0);

    Rgba mid = pixel_at(pixels, 64, 32, 32);
    QVERIFY2(mid.r < 30 && mid.g < 30 && mid.b < 30,
             qPrintable(
                 QString("[%1] expected keyed → black, got (%2,%3,%4)").arg(kc.name).arg(mid.r).arg(mid.g).arg(mid.b)));
  }
}

void TestEffectsGpu::chromakeyCompositeMode() {
  // chromakey.xml row 0 = Mode (combo, default 0 = Composite). Default key=green
  // from row 1; do NOT override key_color — the default IS what we want.
  // Composite mode: matched pixel → alpha=0, rgb premultiplied to (0,0,0);
  // unmatched pixel → preserved at full alpha. Sub-scope A is load-bearing
  // against a passthrough regression (green input would survive); sub-scope B
  // asserts the unmatched branch keeps non-key colours intact.
  // A: solid green + default key=green + Mode=Composite → premultiplied black.
  {
    auto seq = h_->make_sequence(64, 64, 30.0);
    Clip* c = h_->add_generator_clip(seq.get(), -1, 0, 30, EFFECT_INTERNAL_SOLID);
    h_->set_field_color(c->effects[1].get(), 2, 0, QColor(0, 255, 0, 255));
    EffectPtr fx = h_->attach_xml_shader(c, "Chroma Key");
    h_->set_field_combo(fx.get(), 0, 0, 0);  // Mode = Composite
    QByteArray pixels = h_->render_frame(seq.get(), 0);
    Rgba mid = pixel_at(pixels, 64, 32, 32);
    QVERIFY2(
        mid.r < 30 && mid.g < 30 && mid.b < 30,
        qPrintable(
            QString("expected green keyed → premultiplied black, got (%1,%2,%3)").arg(mid.r).arg(mid.g).arg(mid.b)));
  }
  // B: solid red + default key=green + Mode=Composite → red preserved.
  {
    auto seq = h_->make_sequence(64, 64, 30.0);
    Clip* c = h_->add_generator_clip(seq.get(), -1, 0, 30, EFFECT_INTERNAL_SOLID);
    h_->set_field_color(c->effects[1].get(), 2, 0, QColor(255, 0, 0, 255));
    EffectPtr fx = h_->attach_xml_shader(c, "Chroma Key");
    h_->set_field_combo(fx.get(), 0, 0, 0);  // Mode = Composite
    QByteArray pixels = h_->render_frame(seq.get(), 0);
    Rgba mid = pixel_at(pixels, 64, 32, 32);
    QVERIFY2(mid.r > 200 && mid.g < 30 && mid.b < 30,
             qPrintable(QString("expected red preserved (non-key), got (%1,%2,%3)").arg(mid.r).arg(mid.g).arg(mid.b)));
  }
}

void TestEffectsGpu::chromakeyOriginalMode() {
  // chromakey.xml Mode=2 (Original) is a no-op branch by spec — texture_color
  // passes through unchanged. This slot does NOT catch passthrough regressions
  // of Original mode itself (Original IS passthrough) — it catches MODE-CONFUSION
  // regressions where mode=2 wrongly executes the mode=0 (Composite, would emit
  // black for matching pixels, premultiplied rgb for non-matching) or mode=1
  // (Alpha, would emit mask grey ≈ white) branch.
  //
  // Input (50, 200, 100): far from key=green (0,255,0) in YCbCr; if Composite
  // wrongly fired, mask≈1 and we'd see premultiplied rgb ≈ input — so the
  // safer regression target here is mode=1 (Alpha) which would output ≈ white.
  // We assert the input is preserved within ±5 per channel.
  auto seq = h_->make_sequence(64, 64, 30.0);
  Clip* c = h_->add_generator_clip(seq.get(), -1, 0, 30, EFFECT_INTERNAL_SOLID);
  h_->set_field_color(c->effects[1].get(), 2, 0, QColor(50, 200, 100, 255));
  EffectPtr fx = h_->attach_xml_shader(c, "Chroma Key");
  h_->set_field_combo(fx.get(), 0, 0, 2);  // Mode = Original (no-op)
  QByteArray pixels = h_->render_frame(seq.get(), 0);
  Rgba mid = pixel_at(pixels, 64, 32, 32);
  QVERIFY2(
      qAbs(mid.r - 50) <= 5 && qAbs(mid.g - 200) <= 5 && qAbs(mid.b - 100) <= 5,
      qPrintable(QString("expected input unchanged (50,200,100), got (%1,%2,%3)").arg(mid.r).arg(mid.g).arg(mid.b)));
}

void TestEffectsGpu::lumakeyBands() {
  // lumakey.xml row 0=Lower, row 1=Upper. With loc=25, hic=75 on a black→white
  // vertical gradient (slope 1/90.5, offset 13.25): 5 sample y-coords hit the
  // three branches — below-loc (premultiplied black), in-band (rgb*=luma soft
  // ramp), above-hic (opaque source preserved). Predicted R 8-bit values
  // (y=4,20,32,44,60) → (0, 35, 65, 104, 208).
  auto seq = h_->make_sequence(64, 64, 30.0);
  Clip* c = h_->add_generator_clip(seq.get(), -1, 0, 30, EFFECT_INTERNAL_GRADIENT);
  Effect* gradient = c->effects[1].get();
  h_->set_field_color(gradient, 1, 0, QColor(Qt::black));
  h_->set_field_color(gradient, 2, 0, QColor(Qt::white));
  h_->set_field_double(gradient, 3, 0, 90.0);

  EffectPtr fx = h_->attach_xml_shader(c, "Luma Key");
  h_->set_field_double(fx.get(), 0, 0, 25.0);  // loc
  h_->set_field_double(fx.get(), 1, 0, 75.0);  // hic
  QByteArray pixels = h_->render_frame(seq.get(), 0);

  Rgba top = pixel_at(pixels, 64, 32, 4);
  Rgba mid1 = pixel_at(pixels, 64, 32, 20);
  Rgba mid2 = pixel_at(pixels, 64, 32, 32);
  Rgba mid3 = pixel_at(pixels, 64, 32, 44);
  Rgba bot = pixel_at(pixels, 64, 32, 60);

  QVERIFY2(top.r < 10, qPrintable(QString("expected below-loc → premultiplied black, top.r=%1").arg(top.r)));
  QVERIFY2(bot.r > 180, qPrintable(QString("expected above-hic → opaque source preserved, bot.r=%1").arg(bot.r)));
  QVERIFY2(mid1.r < mid2.r && mid2.r < mid3.r,
           qPrintable(
               QString("expected strict monotonic in-band ramp, got (%1,%2,%3)").arg(mid1.r).arg(mid2.r).arg(mid3.r)));
  QVERIFY2(mid1.r > top.r + 10 && mid3.r < bot.r - 10,
           qPrintable(QString("expected in-band between edges, top=%1 mid1=%2 mid3=%3 bot=%4")
                          .arg(top.r)
                          .arg(mid1.r)
                          .arg(mid3.r)
                          .arg(bot.r)));
}

void TestEffectsGpu::despillReducesGreen() {
  // despill.xml default channel=1 (Green), factor=100, balance=0. Shader
  // replaces g with composite(g, avg(r,b,0), 1.0) = min(g, (r+b)/2). For input
  // (50, 200, 50): avg(50,50)=50; min(200,50)=50. G drops 200→50. R and B
  // must be untouched — only the green channel branch writes — so a
  // "desaturate-all" regression that drops R and B is caught by the new
  // preservation assertion.
  auto seq = h_->make_sequence(64, 64, 30.0);
  Clip* c = h_->add_generator_clip(seq.get(), -1, 0, 30, EFFECT_INTERNAL_SOLID);
  h_->set_field_color(c->effects[1].get(), 2, 0, QColor(50, 200, 50, 255));

  h_->attach_xml_shader(c, "Despill");
  QByteArray pixels = h_->render_frame(seq.get(), 0);

  Rgba mid = pixel_at(pixels, 64, 32, 32);
  QVERIFY2(mid.g < 190, qPrintable(QString("expected G reduced, got G=%1").arg(mid.g)));
  QVERIFY2(qAbs(mid.r - 50) <= 5 && qAbs(mid.b - 50) <= 5,
           qPrintable(QString("expected R and B preserved at ~50, got (%1,%2,%3)").arg(mid.r).arg(mid.g).arg(mid.b)));
}

// ---------------------------------------------------------------------------
// Phase 3d — Effects, crop & misc (XML shader effects)
// ---------------------------------------------------------------------------

void TestEffectsGpu::flipHorizontal() {
  // flip.xml row 0=horiz (default true), row 1=vert (default false). Use a
  // left→right gradient (red→blue) so the flip is detectable.
  auto seq = h_->make_sequence(64, 64, 30.0);
  Clip* c = h_->add_generator_clip(seq.get(), -1, 0, 30, EFFECT_INTERNAL_GRADIENT);
  Effect* gradient = c->effects[1].get();
  h_->set_field_color(gradient, 1, 0, QColor(Qt::red));
  h_->set_field_color(gradient, 2, 0, QColor(Qt::blue));
  h_->set_field_double(gradient, 3, 0, 0.0);  // left→right
  QByteArray baseline = h_->render_frame(seq.get(), 0);

  h_->attach_xml_shader(c, "Flip");
  // defaults: horiz=true, vert=false
  QByteArray with_fx = h_->render_frame(seq.get(), 0);

  // Left edge of flipped output corresponds to right edge of input.
  Rgba a = pixel_at(baseline, 64, 58, 32);
  Rgba b = pixel_at(with_fx, 64, 5, 32);
  const int d = qAbs(a.r - b.r) + qAbs(a.g - b.g) + qAbs(a.b - b.b);
  QVERIFY2(d <= 30, qPrintable(QString("expected horizontal mirror: in(58)=(%1,%2,%3) out(5)=(%4,%5,%6) sum-diff=%7")
                                   .arg(a.r)
                                   .arg(a.g)
                                   .arg(a.b)
                                   .arg(b.r)
                                   .arg(b.g)
                                   .arg(b.b)
                                   .arg(d)));
}

void TestEffectsGpu::flipVertical() {
  auto seq = h_->make_sequence(64, 64, 30.0);
  Clip* c = h_->add_generator_clip(seq.get(), -1, 0, 30, EFFECT_INTERNAL_GRADIENT);
  Effect* gradient = c->effects[1].get();
  h_->set_field_color(gradient, 1, 0, QColor(Qt::red));
  h_->set_field_color(gradient, 2, 0, QColor(Qt::blue));
  h_->set_field_double(gradient, 3, 0, 90.0);  // top→bottom
  QByteArray baseline = h_->render_frame(seq.get(), 0);

  EffectPtr fx = h_->attach_xml_shader(c, "Flip");
  h_->set_field_bool(fx.get(), 0, 0, false);  // horiz=false
  h_->set_field_bool(fx.get(), 1, 0, true);   // vert=true
  QByteArray with_fx = h_->render_frame(seq.get(), 0);

  Rgba a = pixel_at(baseline, 64, 32, 58);
  Rgba b = pixel_at(with_fx, 64, 32, 5);
  const int d = qAbs(a.r - b.r) + qAbs(a.g - b.g) + qAbs(a.b - b.b);
  QVERIFY2(d <= 30, qPrintable(QString("expected vertical mirror: in(58)=(%1,%2,%3) out(5)=(%4,%5,%6) sum-diff=%7")
                                   .arg(a.r)
                                   .arg(a.g)
                                   .arg(a.b)
                                   .arg(b.r)
                                   .arg(b.g)
                                   .arg(b.b)
                                   .arg(d)));
}

void TestEffectsGpu::cropTransparentBorders() {
  // crop.xml rows 0..3 = Left/Top/Right/Bottom percent. With 20% margins,
  // pixels outside [0.2, 0.8] get alpha=0 and rgb premultiplied → after the
  // compositor's alpha-force they read as (0,0,0,255). Phase 2 lesson: never
  // assert alpha — assert colour distance instead.
  auto seq = h_->make_sequence(64, 64, 30.0);
  Clip* c = h_->add_generator_clip(seq.get(), -1, 0, 30, EFFECT_INTERNAL_SOLID);
  h_->set_field_color(c->effects[1].get(), 2, 0, QColor(200, 100, 50, 255));

  EffectPtr fx = h_->attach_xml_shader(c, "Crop");
  h_->set_field_double(fx.get(), 0, 0, 20.0);
  h_->set_field_double(fx.get(), 1, 0, 20.0);
  h_->set_field_double(fx.get(), 2, 0, 20.0);
  h_->set_field_double(fx.get(), 3, 0, 20.0);
  QByteArray pixels = h_->render_frame(seq.get(), 0);

  Rgba corner = pixel_at(pixels, 64, 1, 1);
  const int corner_d = qAbs(corner.r - 200) + qAbs(corner.g - 100) + qAbs(corner.b - 50);
  QVERIFY2(corner_d > 100, qPrintable(QString("expected corner cropped, got (%1,%2,%3) input-diff=%4")
                                          .arg(corner.r)
                                          .arg(corner.g)
                                          .arg(corner.b)
                                          .arg(corner_d)));

  Rgba mid = pixel_at(pixels, 64, 32, 32);
  QVERIFY2(qAbs(mid.r - 200) <= 10 && qAbs(mid.g - 100) <= 10 && qAbs(mid.b - 50) <= 10,
           qPrintable(QString("expected centre preserved, got (%1,%2,%3)").arg(mid.r).arg(mid.g).arg(mid.b)));
}

void TestEffectsGpu::cropKeepsSemiTransparentColour() {
  // No margins: Crop must leave a 50% opacity clip as it is. Premultiplied input multiplied by alpha again darkened it
  // to a quarter. The readback forces alpha to 255 and shows the premultiplied rgb: (200,100,50) * 128/255.
  auto seq = h_->make_sequence(64, 64, 30.0);
  Clip* c = h_->add_generator_clip(seq.get(), -1, 0, 30, EFFECT_INTERNAL_SOLID);
  h_->set_field_double(c->effects[1].get(), 1, 0, 50.0);  // Solid opacity 50% → alpha 128
  h_->set_field_color(c->effects[1].get(), 2, 0, QColor(200, 100, 50));
  h_->attach_xml_shader(c, "Crop");
  QByteArray pixels = h_->render_frame(seq.get(), 0);
  Rgba mid = pixel_at(pixels, 64, 32, 32);
  QVERIFY2(qAbs(mid.r - 100) <= 4 && qAbs(mid.g - 50) <= 4 && qAbs(mid.b - 25) <= 4,
           qPrintable(QString("expected ~(100,50,25), got (%1,%2,%3)").arg(mid.r).arg(mid.g).arg(mid.b)));
}

void TestEffectsGpu::embossOutputDiffers() {
  auto seq = h_->make_sequence(64, 64, 30.0);
  Clip* c = h_->add_generator_clip(seq.get(), -1, 0, 30, EFFECT_INTERNAL_GRADIENT);
  Effect* gradient = c->effects[1].get();
  h_->set_field_color(gradient, 1, 0, QColor(Qt::black));
  h_->set_field_color(gradient, 2, 0, QColor(Qt::white));
  h_->set_field_double(gradient, 3, 0, 90.0);
  QByteArray baseline = h_->render_frame(seq.get(), 0);

  h_->attach_xml_shader(c, "Emboss");
  QByteArray with_fx = h_->render_frame(seq.get(), 0);

  const int diff = buf_diff(baseline, with_fx);
  QVERIFY2(diff > 1000, qPrintable(QString("expected emboss to change output, diff=%1").arg(diff)));
}

void TestEffectsGpu::toonifyOutputDiffers() {
  auto seq = h_->make_sequence(64, 64, 30.0);
  Clip* c = h_->add_generator_clip(seq.get(), -1, 0, 30, EFFECT_INTERNAL_GRADIENT);
  Effect* gradient = c->effects[1].get();
  h_->set_field_color(gradient, 1, 0, QColor(Qt::red));
  h_->set_field_color(gradient, 2, 0, QColor(Qt::blue));
  h_->set_field_double(gradient, 3, 0, 90.0);
  QByteArray baseline = h_->render_frame(seq.get(), 0);

  h_->attach_xml_shader(c, "Toonify");
  QByteArray with_fx = h_->render_frame(seq.get(), 0);

  const int diff = buf_diff(baseline, with_fx);
  QVERIFY2(diff > 1000, qPrintable(QString("expected toonify to change output, diff=%1").arg(diff)));
}

void TestEffectsGpu::noiseShaderOutputDiffers() {
  // The XML shader "Noise" has internal == -1; attach_xml_shader's filter
  // skips the audio Noise (internal == EFFECT_INTERNAL_NOISE) so we get the
  // right one.
  auto seq = h_->make_sequence(64, 64, 30.0);
  Clip* c = h_->add_generator_clip(seq.get(), -1, 0, 30, EFFECT_INTERNAL_SOLID);
  h_->set_field_color(c->effects[1].get(), 2, 0, QColor(128, 128, 128, 255));
  QByteArray baseline = h_->render_frame(seq.get(), 0);

  h_->attach_xml_shader(c, "Noise");
  QByteArray with_fx = h_->render_frame(seq.get(), 0);

  const int diff = buf_diff(baseline, with_fx);
  QVERIFY2(diff > 1000, qPrintable(QString("expected shader noise to change output, diff=%1").arg(diff)));
}

void TestEffectsGpu::vignetteDarkensCorners() {
  // vignette.xml defaults: lensRadiusX=45, lensRadiusY=38 → noticeable vignette.
  auto seq = h_->make_sequence(64, 64, 30.0);
  Clip* c = h_->add_generator_clip(seq.get(), -1, 0, 30, EFFECT_INTERNAL_SOLID);
  h_->set_field_color(c->effects[1].get(), 2, 0, QColor(200, 200, 200, 255));

  h_->attach_xml_shader(c, "Vignette");
  QByteArray pixels = h_->render_frame(seq.get(), 0);

  Rgba corner = pixel_at(pixels, 64, 2, 2);
  Rgba mid = pixel_at(pixels, 64, 32, 32);
  const int corner_sum = corner.r + corner.g + corner.b;
  const int mid_sum = mid.r + mid.g + mid.b;
  QVERIFY2(
      mid_sum > corner_sum + 30,
      qPrintable(QString("expected corner darker than centre, corner_sum=%1 mid_sum=%2").arg(corner_sum).arg(mid_sum)));
}

void TestEffectsGpu::pixelateBlocks() {
  // pixelate.xml row 0 = Horizontal Pixels (count of blocks across; default 16).
  // With Horizontal Pixels=8 on a 64-px canvas → 8 blocks of 8 px each.
  // Pixels (1, 4) and (6, 4) are in the same block; pixel (12, 4) is in the
  // next block. Gradient supplies the inter-block colour difference.
  auto seq = h_->make_sequence(64, 64, 30.0);
  Clip* c = h_->add_generator_clip(seq.get(), -1, 0, 30, EFFECT_INTERNAL_GRADIENT);
  Effect* gradient = c->effects[1].get();
  h_->set_field_color(gradient, 1, 0, QColor(Qt::black));
  h_->set_field_color(gradient, 2, 0, QColor(Qt::white));
  h_->set_field_double(gradient, 3, 0, 0.0);  // left→right

  EffectPtr fx = h_->attach_xml_shader(c, "Pixelate");
  h_->set_field_double(fx.get(), 0, 0, 8.0);  // Horizontal Pixels (blocks across)
  h_->set_field_double(fx.get(), 1, 0, 8.0);  // Vertical Pixels (blocks down)
  QByteArray pixels = h_->render_frame(seq.get(), 0);

  Rgba in_block_a = pixel_at(pixels, 64, 1, 4);
  Rgba in_block_b = pixel_at(pixels, 64, 6, 4);
  Rgba next_block = pixel_at(pixels, 64, 12, 4);

  const int same_d =
      qAbs(in_block_a.r - in_block_b.r) + qAbs(in_block_a.g - in_block_b.g) + qAbs(in_block_a.b - in_block_b.b);
  const int diff_d =
      qAbs(in_block_a.r - next_block.r) + qAbs(in_block_a.g - next_block.g) + qAbs(in_block_a.b - next_block.b);
  QVERIFY2(same_d <= 12, qPrintable(QString("expected same-block pixels equal, sum-diff=%1").arg(same_d)));
  QVERIFY2(diff_d > 4, qPrintable(QString("expected next-block pixel different, sum-diff=%1").arg(diff_d)));
}

void TestEffectsGpu::crossstitchOutputDiffers() {
  auto seq = h_->make_sequence(64, 64, 30.0);
  Clip* c = h_->add_generator_clip(seq.get(), -1, 0, 30, EFFECT_INTERNAL_GRADIENT);
  Effect* gradient = c->effects[1].get();
  h_->set_field_color(gradient, 1, 0, QColor(Qt::red));
  h_->set_field_color(gradient, 2, 0, QColor(Qt::blue));
  h_->set_field_double(gradient, 3, 0, 0.0);
  QByteArray baseline = h_->render_frame(seq.get(), 0);

  h_->attach_xml_shader(c, "Cross Stitch");
  QByteArray with_fx = h_->render_frame(seq.get(), 0);

  const int diff = buf_diff(baseline, with_fx);
  QVERIFY2(diff > 1000, qPrintable(QString("expected cross stitch to change output, diff=%1").arg(diff)));
}

void TestEffectsGpu::volumetricLightOutputDiffers() {
  auto seq = h_->make_sequence(64, 64, 30.0);
  Clip* c = h_->add_generator_clip(seq.get(), -1, 0, 30, EFFECT_INTERNAL_GRADIENT);
  Effect* gradient = c->effects[1].get();
  h_->set_field_color(gradient, 1, 0, QColor(Qt::black));
  h_->set_field_color(gradient, 2, 0, QColor(Qt::white));
  h_->set_field_double(gradient, 3, 0, 90.0);
  QByteArray baseline = h_->render_frame(seq.get(), 0);

  h_->attach_xml_shader(c, "Volumetric Light");
  QByteArray with_fx = h_->render_frame(seq.get(), 0);

  const int diff = buf_diff(baseline, with_fx);
  QVERIFY2(diff > 1000, qPrintable(QString("expected volumetric light to change output, diff=%1").arg(diff)));
}

// ---------------------------------------------------------------------------
// F-1 batch 1 — Levels, Lift/Gamma/Gain, Tint, Threshold, Lens Correction
// ---------------------------------------------------------------------------

void TestEffectsGpu::levelsNeutralIdentity() {
  // levels.xml defaults (0, 100, gamma 100, 0, 100, RGB): c = (c-0)/1, pow(c, 1), mix(0, 1, c) = c.
  auto seq = h_->make_sequence(64, 64, 30.0);
  Clip* c = h_->add_generator_clip(seq.get(), -1, 0, 30, EFFECT_INTERNAL_GRADIENT);
  Effect* gradient = c->effects[1].get();
  h_->set_field_color(gradient, 1, 0, QColor(Qt::black));
  h_->set_field_color(gradient, 2, 0, QColor(Qt::white));
  h_->set_field_double(gradient, 3, 0, 90.0);
  QByteArray baseline = h_->render_frame(seq.get(), 0);

  h_->attach_xml_shader(c, "Levels");
  QByteArray with_fx = h_->render_frame(seq.get(), 0);

  QVERIFY2(buffers_within(baseline, with_fx, /*tolerance=*/4),
           qPrintable(QString("expected neutral Levels passthrough, diff=%1").arg(buf_diff(baseline, with_fx))));
}

void TestEffectsGpu::levelsInputWhite() {
  // Row 1 = Input White. 50 → c = 64/255 / 0.5 = 0.502 → 128.
  auto seq = h_->make_sequence(64, 64, 30.0);
  Clip* c = h_->add_generator_clip(seq.get(), -1, 0, 30, EFFECT_INTERNAL_SOLID);
  h_->set_field_color(c->effects[1].get(), 2, 0, QColor(64, 64, 64));
  EffectPtr fx = h_->attach_xml_shader(c, "Levels");
  h_->set_field_double(fx.get(), 1, 0, 50.0);
  QByteArray pixels = h_->render_frame(seq.get(), 0);
  Rgba mid = pixel_at(pixels, 64, 32, 32);
  QVERIFY2(qAbs(mid.r - 128) <= 4 && qAbs(mid.g - 128) <= 4 && qAbs(mid.b - 128) <= 4,
           qPrintable(QString("expected ~(128,128,128), got (%1,%2,%3)").arg(mid.r).arg(mid.g).arg(mid.b)));
}

void TestEffectsGpu::levelsGamma() {
  // Row 2 = Gamma. 200 → c = pow(128/255, 100/200) = pow(0.502, 0.5) = 0.7085 → 181.
  auto seq = h_->make_sequence(64, 64, 30.0);
  Clip* c = h_->add_generator_clip(seq.get(), -1, 0, 30, EFFECT_INTERNAL_SOLID);
  h_->set_field_color(c->effects[1].get(), 2, 0, QColor(128, 128, 128));
  EffectPtr fx = h_->attach_xml_shader(c, "Levels");
  h_->set_field_double(fx.get(), 2, 0, 200.0);
  QByteArray pixels = h_->render_frame(seq.get(), 0);
  Rgba mid = pixel_at(pixels, 64, 32, 32);
  QVERIFY2(qAbs(mid.r - 181) <= 4 && qAbs(mid.g - 181) <= 4 && qAbs(mid.b - 181) <= 4,
           qPrintable(QString("expected ~(181,181,181), got (%1,%2,%3)").arg(mid.r).arg(mid.g).arg(mid.b)));
}

void TestEffectsGpu::levelsChannelRed() {
  // Row 5 = Channel (0 RGB, 1 Red, 2 Green, 3 Blue). Red + Input White 50 on (64,64,64): R → 128, G/B untouched.
  auto seq = h_->make_sequence(64, 64, 30.0);
  Clip* c = h_->add_generator_clip(seq.get(), -1, 0, 30, EFFECT_INTERNAL_SOLID);
  h_->set_field_color(c->effects[1].get(), 2, 0, QColor(64, 64, 64));
  EffectPtr fx = h_->attach_xml_shader(c, "Levels");
  h_->set_field_double(fx.get(), 1, 0, 50.0);
  h_->set_field_combo(fx.get(), 5, 0, 1);
  QByteArray pixels = h_->render_frame(seq.get(), 0);
  Rgba mid = pixel_at(pixels, 64, 32, 32);
  QVERIFY2(qAbs(mid.r - 128) <= 4 && qAbs(mid.g - 64) <= 4 && qAbs(mid.b - 64) <= 4,
           qPrintable(QString("expected ~(128,64,64), got (%1,%2,%3)").arg(mid.r).arg(mid.g).arg(mid.b)));
}

void TestEffectsGpu::liftGammaGainNeutralIdentity() {
  // liftgammagain.xml defaults (lift 0, gamma 100, gain 100): c = 1 * (c + 0), pow(c, 1) = c.
  auto seq = h_->make_sequence(64, 64, 30.0);
  Clip* c = h_->add_generator_clip(seq.get(), -1, 0, 30, EFFECT_INTERNAL_GRADIENT);
  Effect* gradient = c->effects[1].get();
  h_->set_field_color(gradient, 1, 0, QColor(Qt::red));
  h_->set_field_color(gradient, 2, 0, QColor(Qt::blue));
  h_->set_field_double(gradient, 3, 0, 90.0);
  QByteArray baseline = h_->render_frame(seq.get(), 0);

  h_->attach_xml_shader(c, "Lift/Gamma/Gain");
  QByteArray with_fx = h_->render_frame(seq.get(), 0);

  QVERIFY2(
      buffers_within(baseline, with_fx, /*tolerance=*/4),
      qPrintable(QString("expected neutral Lift/Gamma/Gain passthrough, diff=%1").arg(buf_diff(baseline, with_fx))));
}

void TestEffectsGpu::liftGammaGainGainRed() {
  // Row 2 = Gain (R, G, B). Gain R 50 on (200,100,50): R = 0.5 * 200 = 100, G/B untouched → (100,100,50).
  auto seq = h_->make_sequence(64, 64, 30.0);
  Clip* c = h_->add_generator_clip(seq.get(), -1, 0, 30, EFFECT_INTERNAL_SOLID);
  h_->set_field_color(c->effects[1].get(), 2, 0, QColor(200, 100, 50));
  EffectPtr fx = h_->attach_xml_shader(c, "Lift/Gamma/Gain");
  h_->set_field_double(fx.get(), 2, 0, 50.0);
  QByteArray pixels = h_->render_frame(seq.get(), 0);
  Rgba mid = pixel_at(pixels, 64, 32, 32);
  QVERIFY2(qAbs(mid.r - 100) <= 4 && qAbs(mid.g - 100) <= 4 && qAbs(mid.b - 50) <= 4,
           qPrintable(QString("expected ~(100,100,50), got (%1,%2,%3)").arg(mid.r).arg(mid.g).arg(mid.b)));
}

void TestEffectsGpu::liftGammaGainLiftWhite() {
  // Row 0 = Lift (R, G, B). Lift 100 everywhere: c + 1 * (1 - c) = 1 → white whatever the input.
  auto seq = h_->make_sequence(64, 64, 30.0);
  Clip* c = h_->add_generator_clip(seq.get(), -1, 0, 30, EFFECT_INTERNAL_SOLID);
  h_->set_field_color(c->effects[1].get(), 2, 0, QColor(50, 100, 150));
  EffectPtr fx = h_->attach_xml_shader(c, "Lift/Gamma/Gain");
  for (int f = 0; f < 3; f++) h_->set_field_double(fx.get(), 0, f, 100.0);
  QByteArray pixels = h_->render_frame(seq.get(), 0);
  Rgba mid = pixel_at(pixels, 64, 32, 32);
  QVERIFY2(mid.r >= 251 && mid.g >= 251 && mid.b >= 251,
           qPrintable(QString("expected white, got (%1,%2,%3)").arg(mid.r).arg(mid.g).arg(mid.b)));
}

void TestEffectsGpu::tintAmountZero() {
  // Row 2 = Amount. 0 → mix(rgb, tint, 0) = rgb.
  auto seq = h_->make_sequence(64, 64, 30.0);
  Clip* c = h_->add_generator_clip(seq.get(), -1, 0, 30, EFFECT_INTERNAL_GRADIENT);
  Effect* gradient = c->effects[1].get();
  h_->set_field_color(gradient, 1, 0, QColor(Qt::red));
  h_->set_field_color(gradient, 2, 0, QColor(Qt::blue));
  h_->set_field_double(gradient, 3, 0, 90.0);
  QByteArray baseline = h_->render_frame(seq.get(), 0);

  EffectPtr fx = h_->attach_xml_shader(c, "Tint");
  h_->set_field_double(fx.get(), 2, 0, 0.0);
  QByteArray with_fx = h_->render_frame(seq.get(), 0);

  QVERIFY2(buffers_within(baseline, with_fx, /*tolerance=*/4),
           qPrintable(QString("expected Tint amount 0 passthrough, diff=%1").arg(buf_diff(baseline, with_fx))));
}

void TestEffectsGpu::tintShadowsHighlights() {
  // Row 0 = Shadows (set to (20,40,80)), row 1 = Highlights (default sepia (255,224,178)), amount 100.
  // Black: luma 0 → shadows. White: luma 1 → highlights.
  struct Case {
    QColor in, expected;
  };
  const Case cases[] = {{QColor(0, 0, 0), QColor(20, 40, 80)}, {QColor(255, 255, 255), QColor(255, 224, 178)}};
  for (const Case& k : cases) {
    auto seq = h_->make_sequence(64, 64, 30.0);
    Clip* c = h_->add_generator_clip(seq.get(), -1, 0, 30, EFFECT_INTERNAL_SOLID);
    h_->set_field_color(c->effects[1].get(), 2, 0, k.in);
    EffectPtr fx = h_->attach_xml_shader(c, "Tint");
    h_->set_field_color(fx.get(), 0, 0, QColor(20, 40, 80));
    QByteArray pixels = h_->render_frame(seq.get(), 0);
    Rgba mid = pixel_at(pixels, 64, 32, 32);
    QVERIFY2(qAbs(mid.r - k.expected.red()) <= 4 && qAbs(mid.g - k.expected.green()) <= 4 &&
                 qAbs(mid.b - k.expected.blue()) <= 4,
             qPrintable(QString("input %1: expected %2, got (%3,%4,%5)")
                            .arg(k.in.name(), k.expected.name())
                            .arg(mid.r)
                            .arg(mid.g)
                            .arg(mid.b)));
  }
}

void TestEffectsGpu::thresholdLevel() {
  // Row 0 = Level (default 50). Grey 100: luma 0.392 < 0.5 → black. Grey 200: luma 0.784 → white.
  const int greys[] = {100, 200};
  for (int grey : greys) {
    auto seq = h_->make_sequence(64, 64, 30.0);
    Clip* c = h_->add_generator_clip(seq.get(), -1, 0, 30, EFFECT_INTERNAL_SOLID);
    h_->set_field_color(c->effects[1].get(), 2, 0, QColor(grey, grey, grey));
    h_->attach_xml_shader(c, "Threshold");
    QByteArray pixels = h_->render_frame(seq.get(), 0);
    Rgba mid = pixel_at(pixels, 64, 32, 32);
    const int expected = grey < 128 ? 0 : 255;
    QVERIFY2(
        qAbs(mid.r - expected) <= 4 && qAbs(mid.g - expected) <= 4 && qAbs(mid.b - expected) <= 4,
        qPrintable(
            QString("grey %1: expected %2, got (%3,%4,%5)").arg(grey).arg(expected).arg(mid.r).arg(mid.g).arg(mid.b)));
  }
}

void TestEffectsGpu::thresholdInvert() {
  // Row 1 = Invert: grey 100 → white, grey 200 → black.
  const int greys[] = {100, 200};
  for (int grey : greys) {
    auto seq = h_->make_sequence(64, 64, 30.0);
    Clip* c = h_->add_generator_clip(seq.get(), -1, 0, 30, EFFECT_INTERNAL_SOLID);
    h_->set_field_color(c->effects[1].get(), 2, 0, QColor(grey, grey, grey));
    EffectPtr fx = h_->attach_xml_shader(c, "Threshold");
    h_->set_field_bool(fx.get(), 1, 0, true);
    QByteArray pixels = h_->render_frame(seq.get(), 0);
    Rgba mid = pixel_at(pixels, 64, 32, 32);
    const int expected = grey < 128 ? 255 : 0;
    QVERIFY2(qAbs(mid.r - expected) <= 4 && qAbs(mid.g - expected) <= 4 && qAbs(mid.b - expected) <= 4,
             qPrintable(QString("grey %1 inverted: expected %2, got (%3,%4,%5)")
                            .arg(grey)
                            .arg(expected)
                            .arg(mid.r)
                            .arg(mid.g)
                            .arg(mid.b)));
  }
}

void TestEffectsGpu::lensCorrectionNeutralIdentity() {
  // lenscorrection.xml defaults (center 0,0, K1 0, K2 0, zoom 100): src = c + p * 1 * 1 = uv.
  auto seq = h_->make_sequence(64, 64, 30.0);
  Clip* c = h_->add_generator_clip(seq.get(), -1, 0, 30, EFFECT_INTERNAL_GRADIENT);
  Effect* gradient = c->effects[1].get();
  h_->set_field_color(gradient, 1, 0, QColor(Qt::black));
  h_->set_field_color(gradient, 2, 0, QColor(Qt::white));
  h_->set_field_double(gradient, 3, 0, 90.0);
  QByteArray baseline = h_->render_frame(seq.get(), 0);

  h_->attach_xml_shader(c, "Lens Correction");
  QByteArray with_fx = h_->render_frame(seq.get(), 0);

  QVERIFY2(
      buffers_within(baseline, with_fx, /*tolerance=*/4),
      qPrintable(QString("expected neutral Lens Correction passthrough, diff=%1").arg(buf_diff(baseline, with_fx))));
}

void TestEffectsGpu::lensCorrectionK1Distorts() {
  // Row 1 = K1. 50 → src = c + p * (1 + 0.5 r²): rows away from the centre sample further out (and the corners fall
  // outside the frame → transparent), the centre (r ≈ 0) stays put.
  auto seq = h_->make_sequence(64, 64, 30.0);
  Clip* c = h_->add_generator_clip(seq.get(), -1, 0, 30, EFFECT_INTERNAL_GRADIENT);
  Effect* gradient = c->effects[1].get();
  h_->set_field_color(gradient, 1, 0, QColor(Qt::black));
  h_->set_field_color(gradient, 2, 0, QColor(Qt::white));
  h_->set_field_double(gradient, 3, 0, 90.0);
  QByteArray baseline = h_->render_frame(seq.get(), 0);

  EffectPtr fx = h_->attach_xml_shader(c, "Lens Correction");
  h_->set_field_double(fx.get(), 1, 0, 50.0);
  QByteArray with_fx = h_->render_frame(seq.get(), 0);

  const int diff = buf_diff(baseline, with_fx);
  QVERIFY2(diff > 1000, qPrintable(QString("expected K1 to change output, diff=%1").arg(diff)));
  Rgba base_mid = pixel_at(baseline, 64, 32, 32);
  Rgba fx_mid = pixel_at(with_fx, 64, 32, 32);
  QVERIFY2(qAbs(base_mid.r - fx_mid.r) <= 4,
           qPrintable(QString("expected the centre to stay put, %1 vs %2").arg(base_mid.r).arg(fx_mid.r)));
}

void TestEffectsGpu::liftGammaGainGamma() {
  // Row 1 = Gamma (R, G, B). 200 everywhere on grey 128: pow(128/255, 100/200) = pow(0.502, 0.5) = 0.7085 → 181.
  auto seq = h_->make_sequence(64, 64, 30.0);
  Clip* c = h_->add_generator_clip(seq.get(), -1, 0, 30, EFFECT_INTERNAL_SOLID);
  h_->set_field_color(c->effects[1].get(), 2, 0, QColor(128, 128, 128));
  EffectPtr fx = h_->attach_xml_shader(c, "Lift/Gamma/Gain");
  for (int f = 0; f < 3; f++) h_->set_field_double(fx.get(), 1, f, 200.0);
  QByteArray pixels = h_->render_frame(seq.get(), 0);
  Rgba mid = pixel_at(pixels, 64, 32, 32);
  QVERIFY2(qAbs(mid.r - 181) <= 4 && qAbs(mid.g - 181) <= 4 && qAbs(mid.b - 181) <= 4,
           qPrintable(QString("expected ~(181,181,181), got (%1,%2,%3)").arg(mid.r).arg(mid.g).arg(mid.b)));
}

void TestEffectsGpu::thresholdCustomLevel() {
  // Row 0 = Level 30 on grey 100: luma 0.392 >= 0.3 → white (black at the default level 50, see thresholdLevel).
  auto seq = h_->make_sequence(64, 64, 30.0);
  Clip* c = h_->add_generator_clip(seq.get(), -1, 0, 30, EFFECT_INTERNAL_SOLID);
  h_->set_field_color(c->effects[1].get(), 2, 0, QColor(100, 100, 100));
  EffectPtr fx = h_->attach_xml_shader(c, "Threshold");
  h_->set_field_double(fx.get(), 0, 0, 30.0);
  QByteArray pixels = h_->render_frame(seq.get(), 0);
  Rgba mid = pixel_at(pixels, 64, 32, 32);
  QVERIFY2(mid.r >= 251 && mid.g >= 251 && mid.b >= 251,
           qPrintable(QString("expected white, got (%1,%2,%3)").arg(mid.r).arg(mid.g).arg(mid.b)));
}

void TestEffectsGpu::lensCorrectionNonSquareAspect() {
  // 96x48 (aspect 2), K1 50: the output pixel must equal the input sampled where the shader's model says, computed
  // here on the CPU: p = (uv - 0.5) * (aspect, 1) / half_diag, half_diag = |(0.5 * aspect, 0.5)| = 1.118,
  // src = 0.5 + (uv - 0.5) * (1 + 0.5 r²). One pixel on the horizontal axis over a left→right gradient, one on the
  // vertical axis over a top→bottom gradient. A swapped aspect or a half_diag without the aspect moves the sample by
  // several pixels on at least one of them.
  //   (84,24): r² = 0.4635, f = 1.2317, src.x = 0.9683 → x' = 92.46 (swapped aspect: 86.1)
  //   (48,42): r² = 0.1189, f = 1.0595, src.y = 0.9083 → y' = 43.10 (swapped aspect: 46.4)
  // The expected value is a bilinear read of the unaffected render at (x', y'); the mapping is point-symmetric about
  // the centre, so it holds whichever way the readback rows run.
  const int W = 96, H = 48;
  const double k1 = 0.5;
  struct Case {
    double angle;  // gradient angle: 0 = left→right, 90 = top→bottom
    int x, y;
  };
  const Case cases[] = {{0.0, 84, 24}, {90.0, 48, 42}};
  for (const Case& k : cases) {
    auto seq = h_->make_sequence(W, H, 30.0);
    Clip* c = h_->add_generator_clip(seq.get(), -1, 0, 30, EFFECT_INTERNAL_GRADIENT);
    Effect* gradient = c->effects[1].get();
    h_->set_field_color(gradient, 1, 0, QColor(Qt::black));
    h_->set_field_color(gradient, 2, 0, QColor(Qt::white));
    h_->set_field_double(gradient, 3, 0, k.angle);
    QByteArray baseline = h_->render_frame(seq.get(), 0);

    EffectPtr fx = h_->attach_xml_shader(c, "Lens Correction");
    h_->set_field_double(fx.get(), 1, 0, k1 * 100.0);
    QByteArray with_fx = h_->render_frame(seq.get(), 0);

    const double aspect = double(W) / H;
    const double half_diag = std::hypot(0.5 * aspect, 0.5);
    const double dx = (k.x + 0.5) / W - 0.5, dy = (k.y + 0.5) / H - 0.5;
    const double r2 = (dx * aspect * dx * aspect + dy * dy) / (half_diag * half_diag);
    const double f = 1.0 + k1 * r2;
    const double sx = (0.5 + dx * f) * W - 0.5, sy = (0.5 + dy * f) * H - 0.5;  // source, in pixel indices
    const int x0 = int(std::floor(sx)), y0 = int(std::floor(sy));
    QVERIFY2(x0 >= 0 && x0 + 1 < W && y0 >= 0 && y0 + 1 < H, "expected source inside the frame");
    const double tx = sx - x0, ty = sy - y0;
    auto r_at = [&](int x, int y) { return double(pixel_at(baseline, W, x, y).r); };
    const double expected = (r_at(x0, y0) * (1 - tx) + r_at(x0 + 1, y0) * tx) * (1 - ty) +
                            (r_at(x0, y0 + 1) * (1 - tx) + r_at(x0 + 1, y0 + 1) * tx) * ty;
    // the gradient must run along the tested axis (pixel vs its mirror about the centre), else the check proves nothing
    const int near = pixel_at(baseline, W, k.x, k.y).r, mirror = pixel_at(baseline, W, W - 1 - k.x, H - 1 - k.y).r;
    QVERIFY2(
        qAbs(near - mirror) > 50,
        qPrintable(
            QString("angle %1: reference gradient not along the axis (%2 vs %3)").arg(k.angle).arg(near).arg(mirror)));
    const int got = pixel_at(with_fx, W, k.x, k.y).r;
    QVERIFY2(qAbs(got - expected) <= 4,
             qPrintable(QString("angle %1, pixel (%2,%3): expected ~%4 (source %5,%6), got %7")
                            .arg(k.angle)
                            .arg(k.x)
                            .arg(k.y)
                            .arg(expected, 0, 'f', 1)
                            .arg(sx, 0, 'f', 2)
                            .arg(sy, 0, 'f', 2)
                            .arg(got)));
  }
}

// ---------------------------------------------------------------------------
// F-1 batch 2 — Find Edges, Colorize, Alpha Adjust
// ---------------------------------------------------------------------------

namespace {

// 64x64 Solid checkerboard, 16 px cells, (0,0) cell opaque black, the others `color`: hard edges inside one clip
// texture (a Transform-shifted clip wouldn't do, effects see the clip texture before its Transform).
Clip* add_checkerboard(TestRenderHarness* h, Sequence* seq, QColor color) {
  Clip* c = h->add_generator_clip(seq, -1, 0, 30, EFFECT_INTERNAL_SOLID);
  Effect* checker = c->effects[1].get();
  h->set_field_combo(checker, 0, 0, 2);  // Type = SOLID_TYPE_CHECKERBOARD
  h->set_field_color(checker, 2, 0, color);
  h->set_field_double(checker, 3, 0, 16.0);
  return c;
}

}  // namespace

void TestEffectsGpu::findEdgesFlatSolid() {
  // Flat colour: every Sobel tap is equal → magnitude 0 → black (Luma mode, default), white with Invert (row 2).
  const bool inverts[] = {false, true};
  for (bool invert : inverts) {
    auto seq = h_->make_sequence(64, 64, 30.0);
    Clip* c = h_->add_generator_clip(seq.get(), -1, 0, 30, EFFECT_INTERNAL_SOLID);
    h_->set_field_color(c->effects[1].get(), 2, 0, QColor(200, 100, 50));
    EffectPtr fx = h_->attach_xml_shader(c, "Find Edges");
    h_->set_field_bool(fx.get(), 2, 0, invert);
    QByteArray pixels = h_->render_frame(seq.get(), 0);
    const int expected = invert ? 255 : 0;
    Rgba mid = pixel_at(pixels, 64, 32, 32);
    QVERIFY2(qAbs(mid.r - expected) <= 4 && qAbs(mid.g - expected) <= 4 && qAbs(mid.b - expected) <= 4,
             qPrintable(QString("invert %1: expected %2, got (%3,%4,%5)")
                            .arg(invert)
                            .arg(expected)
                            .arg(mid.r)
                            .arg(mid.g)
                            .arg(mid.b)));
  }
}

void TestEffectsGpu::findEdgesHardEdge() {
  // Black/white checkerboard, edges at x = 16 and y = 16. (15,8) and (16,8) straddle the vertical edge: one Sobel
  // column is white, the other black → |gx| = 1 + 2 + 1 = 4 → clamp(4 * 1) = 1 → 255. Same for (8,15) across the
  // horizontal edge. (8,8) and (24,8) are cell interiors (black and white) → 0.
  auto seq = h_->make_sequence(64, 64, 30.0);
  Clip* c = add_checkerboard(h_.get(), seq.get(), QColor(255, 255, 255));
  h_->attach_xml_shader(c, "Find Edges");
  QByteArray pixels = h_->render_frame(seq.get(), 0);
  struct Probe {
    int x, y, expected;
  };
  const Probe probes[] = {{15, 8, 255}, {16, 8, 255}, {8, 15, 255}, {8, 8, 0}, {24, 8, 0}};
  for (const Probe& p : probes) {
    Rgba px = pixel_at(pixels, 64, p.x, p.y);
    QVERIFY2(qAbs(px.r - p.expected) <= 4 && qAbs(px.g - p.expected) <= 4 && qAbs(px.b - p.expected) <= 4,
             qPrintable(QString("(%1,%2): expected %3, got (%4,%5,%6)")
                            .arg(p.x)
                            .arg(p.y)
                            .arg(p.expected)
                            .arg(px.r)
                            .arg(px.g)
                            .arg(px.b)));
  }
}

void TestEffectsGpu::findEdgesStrengthZero() {
  // Row 0 = Strength. 0 → e = clamp(mag * 0) = 0 everywhere, edges included.
  auto seq = h_->make_sequence(64, 64, 30.0);
  Clip* c = add_checkerboard(h_.get(), seq.get(), QColor(255, 255, 255));
  EffectPtr fx = h_->attach_xml_shader(c, "Find Edges");
  h_->set_field_double(fx.get(), 0, 0, 0.0);
  QByteArray pixels = h_->render_frame(seq.get(), 0);
  int brightest = 0;
  for (int i = 0; i + 3 < pixels.size(); i += 4) {
    for (int ch = 0; ch < 3; ch++) brightest = qMax(brightest, int(static_cast<uchar>(pixels[i + ch])));
  }
  QVERIFY2(brightest <= 4, qPrintable(QString("expected black everywhere, brightest channel %1").arg(brightest)));
}

void TestEffectsGpu::findEdgesColorMode() {
  // Black/red checkerboard, pixel (16,8) on the vertical edge. Luma mode (0): luma step 0.2126 → |gx| = 0.8504 → grey
  // 217. Color mode (row 1 = 1): per channel, R step 1 → 1, G/B 0 → (255,0,0).
  struct Case {
    int mode;
    QColor expected;
  };
  const Case cases[] = {{0, QColor(217, 217, 217)}, {1, QColor(255, 0, 0)}};
  for (const Case& k : cases) {
    auto seq = h_->make_sequence(64, 64, 30.0);
    Clip* c = add_checkerboard(h_.get(), seq.get(), QColor(255, 0, 0));
    EffectPtr fx = h_->attach_xml_shader(c, "Find Edges");
    h_->set_field_combo(fx.get(), 1, 0, k.mode);
    QByteArray pixels = h_->render_frame(seq.get(), 0);
    Rgba px = pixel_at(pixels, 64, 16, 8);
    QVERIFY2(qAbs(px.r - k.expected.red()) <= 4 && qAbs(px.g - k.expected.green()) <= 4 &&
                 qAbs(px.b - k.expected.blue()) <= 4,
             qPrintable(QString("mode %1: expected %2, got (%3,%4,%5)")
                            .arg(k.mode)
                            .arg(k.expected.name())
                            .arg(px.r)
                            .arg(px.g)
                            .arg(px.b)));
  }
}

void TestEffectsGpu::colorizeSaturationZero() {
  // Row 1 = Saturation 0: HSL with S = 0 is grey L. (200,100,50): L = 0.2126*0.7843 + 0.7152*0.3922 + 0.0722*0.1961
  // = 0.4614 → 118.
  auto seq = h_->make_sequence(64, 64, 30.0);
  Clip* c = h_->add_generator_clip(seq.get(), -1, 0, 30, EFFECT_INTERNAL_SOLID);
  h_->set_field_color(c->effects[1].get(), 2, 0, QColor(200, 100, 50));
  EffectPtr fx = h_->attach_xml_shader(c, "Colorize");
  h_->set_field_double(fx.get(), 1, 0, 0.0);
  QByteArray pixels = h_->render_frame(seq.get(), 0);
  Rgba mid = pixel_at(pixels, 64, 32, 32);
  QVERIFY2(qAbs(mid.r - 118) <= 4 && qAbs(mid.g - 118) <= 4 && qAbs(mid.b - 118) <= 4,
           qPrintable(QString("expected ~(118,118,118), got (%1,%2,%3)").arg(mid.r).arg(mid.g).arg(mid.b)));
}

void TestEffectsGpu::colorizeRedHue() {
  // Hue 0, saturation 100, lightness 0 on grey 128: L = 0.502, C = (1 - |2L - 1|) * S = 0.996, pure hue 0 = (1,0,0)
  // → L + C * ((1,0,0) - 0.5) = (1.000, 0.004, 0.004) → (255,1,1).
  auto seq = h_->make_sequence(64, 64, 30.0);
  Clip* c = h_->add_generator_clip(seq.get(), -1, 0, 30, EFFECT_INTERNAL_SOLID);
  h_->set_field_color(c->effects[1].get(), 2, 0, QColor(128, 128, 128));
  EffectPtr fx = h_->attach_xml_shader(c, "Colorize");
  h_->set_field_double(fx.get(), 0, 0, 0.0);
  h_->set_field_double(fx.get(), 1, 0, 100.0);
  QByteArray pixels = h_->render_frame(seq.get(), 0);
  Rgba mid = pixel_at(pixels, 64, 32, 32);
  QVERIFY2(qAbs(mid.r - 255) <= 4 && qAbs(mid.g - 1) <= 4 && qAbs(mid.b - 1) <= 4,
           qPrintable(QString("expected ~(255,1,1), got (%1,%2,%3)").arg(mid.r).arg(mid.g).arg(mid.b)));
}

void TestEffectsGpu::colorizeLightnessWhite() {
  // Row 2 = Lightness 100: L' = L + (1 - L) * 1 = 1 → C = 0 → white, whatever the hue/saturation (defaults 30/50).
  auto seq = h_->make_sequence(64, 64, 30.0);
  Clip* c = h_->add_generator_clip(seq.get(), -1, 0, 30, EFFECT_INTERNAL_SOLID);
  h_->set_field_color(c->effects[1].get(), 2, 0, QColor(50, 100, 150));
  EffectPtr fx = h_->attach_xml_shader(c, "Colorize");
  h_->set_field_double(fx.get(), 2, 0, 100.0);
  QByteArray pixels = h_->render_frame(seq.get(), 0);
  Rgba mid = pixel_at(pixels, 64, 32, 32);
  QVERIFY2(mid.r >= 251 && mid.g >= 251 && mid.b >= 251,
           qPrintable(QString("expected white, got (%1,%2,%3)").arg(mid.r).arg(mid.g).arg(mid.b)));
}

namespace {

// Opaque (50,100,150) Solid on track -1, a (200,100,50) Solid at `opacity` % on track -2 carrying Alpha Adjust in
// `mode` (row 0) with Level `level` (row 1). Returns the composite at the centre. Alpha can't be read back (the
// readback forces 255), the srcover composite over the track below shows it: top * a' + bottom * (1 - a').
Rgba alpha_adjust_composite(TestRenderHarness* h, double opacity, int mode, double level) {
  auto seq = h->make_sequence(64, 64, 30.0);
  Clip* bottom = h->add_generator_clip(seq.get(), -1, 0, 30, EFFECT_INTERNAL_SOLID);
  h->set_field_color(bottom->effects[1].get(), 2, 0, QColor(50, 100, 150));
  Clip* top = h->add_generator_clip(seq.get(), -2, 0, 30, EFFECT_INTERNAL_SOLID);
  h->set_field_double(top->effects[1].get(), 1, 0, opacity);  // Solid opacity → alpha round(opacity * 2.55)
  h->set_field_color(top->effects[1].get(), 2, 0, QColor(200, 100, 50));
  EffectPtr fx = h->attach_xml_shader(top, "Alpha Adjust");
  h->set_field_combo(fx.get(), 0, 0, mode);
  h->set_field_double(fx.get(), 1, 0, level);
  QByteArray pixels = h->render_frame(seq.get(), 0);
  return pixel_at(pixels, 64, 32, 32);
}

}  // namespace

void TestEffectsGpu::alphaAdjustInvert() {
  // Mode 0 = Invert. Top at 25% → alpha 64/255 = 0.251 → a' = 0.749. Composite: R 200*0.749 + 50*0.251 = 162,
  // G 100, B 50*0.749 + 150*0.251 = 75. Without the effect it would be (88,100,125).
  Rgba mid = alpha_adjust_composite(h_.get(), 25.0, 0, 50.0);
  QVERIFY2(qAbs(mid.r - 162) <= 4 && qAbs(mid.g - 100) <= 4 && qAbs(mid.b - 75) <= 4,
           qPrintable(QString("expected ~(162,100,75), got (%1,%2,%3)").arg(mid.r).arg(mid.g).arg(mid.b)));
}

void TestEffectsGpu::alphaAdjustThreshold() {
  // Mode 1 = Threshold. Top at 60% → alpha 0.6. Level 50: 0.6 >= 0.5 → a' = 1 → the top colour (200,100,50).
  // Level 70: 0.6 < 0.7 → a' = 0 → the track below (50,100,150).
  struct Case {
    double level;
    QColor expected;
  };
  const Case cases[] = {{50.0, QColor(200, 100, 50)}, {70.0, QColor(50, 100, 150)}};
  for (const Case& k : cases) {
    Rgba mid = alpha_adjust_composite(h_.get(), 60.0, 1, k.level);
    QVERIFY2(qAbs(mid.r - k.expected.red()) <= 4 && qAbs(mid.g - k.expected.green()) <= 4 &&
                 qAbs(mid.b - k.expected.blue()) <= 4,
             qPrintable(QString("level %1: expected %2, got (%3,%4,%5)")
                            .arg(k.level)
                            .arg(k.expected.name())
                            .arg(mid.r)
                            .arg(mid.g)
                            .arg(mid.b)));
  }
}

void TestEffectsGpu::alphaAdjustShowAlpha() {
  // Mode 2 = Show Alpha: vec4(vec3(a), 1), opaque so it hides the track below. Opaque top → white; top at 25% →
  // grey 64.
  struct Case {
    double opacity;
    int expected;
  };
  const Case cases[] = {{100.0, 255}, {25.0, 64}};
  for (const Case& k : cases) {
    Rgba mid = alpha_adjust_composite(h_.get(), k.opacity, 2, 50.0);
    QVERIFY2(qAbs(mid.r - k.expected) <= 4 && qAbs(mid.g - k.expected) <= 4 && qAbs(mid.b - k.expected) <= 4,
             qPrintable(QString("opacity %1: expected %2, got (%3,%4,%5)")
                            .arg(k.opacity)
                            .arg(k.expected)
                            .arg(mid.r)
                            .arg(mid.g)
                            .arg(mid.b)));
  }
}

void TestEffectsGpu::colorShadersKeepTransparentClipTransparent() {
  // Premultiplied input: a fully transparent clip is (0,0,0,0). An offset op applied to premultiplied rgb (output
  // black, lift, shadows tint, inverted threshold/edges, colorize lightness) would give rgb > 0 with a = 0, which the
  // srcover blend ADDS onto the track below. Each effect, with non-neutral parameters, sits on a 0% opacity Solid over
  // an opaque (50,100,150) track: the track below must come out unchanged.
  struct Case {
    const char* effect;
    std::function<void(Effect*)> configure;
  };
  const Case cases[] = {
      {"Levels", [this](Effect* e) { h_->set_field_double(e, 3, 0, 50.0); }},  // output black 50
      {"Lift/Gamma/Gain",
       [this](Effect* e) {
         for (int f = 0; f < 3; f++) h_->set_field_double(e, 0, f, 50.0);  // lift 50
       }},
      {"Tint", [this](Effect* e) { h_->set_field_color(e, 0, 0, QColor(200, 100, 50)); }},  // shadows non-black
      {"Threshold", [this](Effect* e) { h_->set_field_bool(e, 1, 0, true); }},              // invert: black → white
      {"Find Edges", [this](Effect* e) { h_->set_field_bool(e, 2, 0, true); }},             // invert: no edge → white
      {"Colorize", [this](Effect* e) { h_->set_field_double(e, 2, 0, 50.0); }},             // lightness 50
      {"Glow", [](Effect*) {}},  // defaults: bright pass of zeros is zero, a = max(0, max(rgb)) = 0
      {"Denoise", [this](Effect* e) { h_->set_field_double(e, 1, 0, 100.0); }},  // strength 100
  };

  auto make_bottom = [this](Sequence* seq) {
    Clip* bottom = h_->add_generator_clip(seq, -1, 0, 30, EFFECT_INTERNAL_SOLID);
    h_->set_field_color(bottom->effects[1].get(), 2, 0, QColor(50, 100, 150));
  };
  auto ref_seq = h_->make_sequence(64, 64, 30.0);
  make_bottom(ref_seq.get());
  QByteArray reference = h_->render_frame(ref_seq.get(), 0);
  QVERIFY(!reference.isEmpty());

  for (const Case& k : cases) {
    auto seq = h_->make_sequence(64, 64, 30.0);
    make_bottom(seq.get());
    Clip* top = h_->add_generator_clip(seq.get(), -2, 0, 30, EFFECT_INTERNAL_SOLID);
    h_->set_field_double(top->effects[1].get(), 1, 0, 0.0);  // Solid opacity 0% → (0,0,0,0)
    h_->set_field_color(top->effects[1].get(), 2, 0, QColor(200, 100, 50));
    EffectPtr fx = h_->attach_xml_shader(top, k.effect);
    k.configure(fx.get());
    QByteArray pixels = h_->render_frame(seq.get(), 0);
    Rgba mid = pixel_at(pixels, 64, 32, 32);
    QVERIFY2(buffers_within(reference, pixels, /*tolerance=*/4),
             qPrintable(QString("%1 on a transparent clip changed the track below: got (%2,%3,%4), diff=%5")
                            .arg(k.effect)
                            .arg(mid.r)
                            .arg(mid.g)
                            .arg(mid.b)
                            .arg(buf_diff(reference, pixels))));
  }
}

// ---------------------------------------------------------------------------
// F-1 batch 3 — original input for multipass shaders (original="1"), Glow
// ---------------------------------------------------------------------------

namespace {

// Glow rows: 0 Threshold, 1 Radius, 2 Intensity, 3 Blend (0 Add, 1 Screen)
EffectPtr attach_glow(TestRenderHarness* h, Clip* c, double threshold, double radius, double intensity, int blend) {
  EffectPtr fx = h->attach_xml_shader(c, "Glow");
  h->set_field_double(fx.get(), 0, 0, threshold);
  h->set_field_double(fx.get(), 1, 0, radius);
  h->set_field_double(fx.get(), 2, 0, intensity);
  h->set_field_combo(fx.get(), 3, 0, blend);
  return fx;
}

// Probes of the white/black checkerboard (cell (0,0) black, x 0..15) under Glow threshold 60, radius 4, intensity
// 100, Add. sigma = 4/3, one tap per texel over x = -4..4, w(x) = exp(-x^2 * 9/32): w0 = 1, w1 = 0.75484,
// w2 = 0.32465, w3 = 0.07956, w4 = 0.01111, sum = 3.34032. White taps are bright 1 ((1 - 0.6) / 0.4), black 0.
// (14,8): white texels 16..18 = x 2..4 → 0.41532 / 3.34032 = 0.12434 → 32 after the RGBA8 pass, the vertical pass
// stays inside the cell (rows 4..12), so 0 + 32 = 32. (8,8): taps 4..12 all black → 0. (24,8) white: 1 + g → 255.
struct GlowProbe {
  int x, y, expected;
};
const GlowProbe kGlowCheckerProbes[] = {{14, 8, 32}, {8, 8, 0}, {24, 8, 255}};

void check_glow_probes(const QByteArray& pixels, const char* label) {
  for (const GlowProbe& p : kGlowCheckerProbes) {
    Rgba px = pixel_at(pixels, 64, p.x, p.y);
    QVERIFY2(qAbs(px.r - p.expected) <= 4 && qAbs(px.g - p.expected) <= 4 && qAbs(px.b - p.expected) <= 4,
             qPrintable(QString("%1 (%2,%3): expected %4, got (%5,%6,%7)")
                            .arg(label)
                            .arg(p.x)
                            .arg(p.y)
                            .arg(p.expected)
                            .arg(px.r)
                            .arg(px.g)
                            .arg(px.b)));
  }
}

}  // namespace

void TestEffectsGpu::glowIntensityZeroIdentity() {
  // Intensity 0 → g = 0, rgb = o.rgb, a = max(o.a, max(o.rgb)) = o.a (premultiplied): the input exactly.
  auto seq = h_->make_sequence(64, 64, 30.0);
  Clip* c = h_->add_generator_clip(seq.get(), -1, 0, 30, EFFECT_INTERNAL_GRADIENT);
  Effect* gradient = c->effects[1].get();
  h_->set_field_color(gradient, 1, 0, QColor(Qt::black));
  h_->set_field_color(gradient, 2, 0, QColor(Qt::white));
  h_->set_field_double(gradient, 3, 0, 90.0);
  QByteArray baseline = h_->render_frame(seq.get(), 0);
  attach_glow(h_.get(), c, 0.0, 20.0, 0.0, 0);
  QByteArray with_fx = h_->render_frame(seq.get(), 0);
  QVERIFY2(buffers_within(baseline, with_fx, /*tolerance=*/2),
           qPrintable(QString("expected identity, diff=%1").arg(buf_diff(baseline, with_fx))));
}

void TestEffectsGpu::glowThresholdFullIdentity() {
  // Threshold 100 → k = max(luma - 1, 0) / 1e-4 = 0 for every pixel: no halo, the input comes out unchanged.
  auto seq = h_->make_sequence(64, 64, 30.0);
  Clip* c = h_->add_generator_clip(seq.get(), -1, 0, 30, EFFECT_INTERNAL_GRADIENT);
  Effect* gradient = c->effects[1].get();
  h_->set_field_color(gradient, 1, 0, QColor(Qt::black));
  h_->set_field_color(gradient, 2, 0, QColor(Qt::white));
  h_->set_field_double(gradient, 3, 0, 90.0);
  QByteArray baseline = h_->render_frame(seq.get(), 0);
  attach_glow(h_.get(), c, 100.0, 20.0, 400.0, 0);
  QByteArray with_fx = h_->render_frame(seq.get(), 0);
  QVERIFY2(buffers_within(baseline, with_fx, /*tolerance=*/2),
           qPrintable(QString("expected identity, diff=%1").arg(buf_diff(baseline, with_fx))));
}

void TestEffectsGpu::glowCheckerboardHalo() {
  // The Solid's own texture is the Glow input here (not a ping-pong texture): bound directly, no snapshot.
  auto seq = h_->make_sequence(64, 64, 30.0);
  Clip* c = add_checkerboard(h_.get(), seq.get(), QColor(255, 255, 255));
  attach_glow(h_.get(), c, 60.0, 4.0, 100.0, 0);
  check_glow_probes(h_->render_frame(seq.get(), 0), "direct input");
}

void TestEffectsGpu::glowScreenMode() {
  // Flat (204,204,204) = 0.8: k = (0.8 - 0.6) / 0.4 = 0.5 → g = 0.4 everywhere (a flat blur stays flat).
  // Add: 0.8 + 0.4 = 1.2 → 255. Screen: 1 - (1 - 0.8)(1 - 0.4) = 0.88 → 224.
  const int modes[] = {0, 1};
  for (int mode : modes) {
    auto seq = h_->make_sequence(64, 64, 30.0);
    Clip* c = h_->add_generator_clip(seq.get(), -1, 0, 30, EFFECT_INTERNAL_SOLID);
    h_->set_field_color(c->effects[1].get(), 2, 0, QColor(204, 204, 204));
    attach_glow(h_.get(), c, 60.0, 4.0, 100.0, mode);
    QByteArray pixels = h_->render_frame(seq.get(), 0);
    const int expected = mode == 0 ? 255 : 224;
    Rgba mid = pixel_at(pixels, 64, 32, 32);
    QVERIFY2(
        qAbs(mid.r - expected) <= 4 && qAbs(mid.g - expected) <= 4 && qAbs(mid.b - expected) <= 4,
        qPrintable(
            QString("blend %1: expected %2, got (%3,%4,%5)").arg(mode).arg(expected).arg(mid.r).arg(mid.g).arg(mid.b)));
  }
}

void TestEffectsGpu::glowSnapshotPingPongInput() {
  // Invert at amount 0 changes nothing but leaves the Glow input in one of the clip's ping-pong textures, which the
  // Glow passes overwrite: binding 3 must read the copy in ClipRhiResources::orig. Without the snapshot, pass 2
  // reads pass 1's output as the "original" (halo added twice: (14,8) ≈ 64 instead of 32).
  auto ref_seq = h_->make_sequence(64, 64, 30.0);
  Clip* ref = add_checkerboard(h_.get(), ref_seq.get(), QColor(255, 255, 255));
  attach_glow(h_.get(), ref, 60.0, 4.0, 100.0, 0);
  QByteArray reference = h_->render_frame(ref_seq.get(), 0);

  auto seq = h_->make_sequence(64, 64, 30.0);
  Clip* c = add_checkerboard(h_.get(), seq.get(), QColor(255, 255, 255));
  EffectPtr invert = h_->attach_xml_shader(c, "Invert");
  h_->set_field_double(invert.get(), 0, 0, 0.0);
  attach_glow(h_.get(), c, 60.0, 4.0, 100.0, 0);
  QByteArray pixels = h_->render_frame(seq.get(), 0);
  check_glow_probes(pixels, "ping-pong input");
  QVERIFY2(buffers_within(reference, pixels, /*tolerance=*/2),
           qPrintable(QString("ping-pong input differs from direct input, diff=%1").arg(buf_diff(reference, pixels))));
}

// ---------------------------------------------------------------------------
// F-1 batch 4 — Denoise (spatial bilateral)
// ---------------------------------------------------------------------------

namespace {

// Denoise rows: 0 Radius (render-target px, rounded down), 1 Strength, 2 Detail
EffectPtr attach_denoise(TestRenderHarness* h, Clip* c, double radius, double strength, double detail) {
  EffectPtr fx = h->attach_xml_shader(c, "Denoise");
  h->set_field_double(fx.get(), 0, 0, radius);
  h->set_field_double(fx.get(), 1, 0, strength);
  h->set_field_double(fx.get(), 2, 0, detail);
  return fx;
}

// Sum over the 32x32 centre block of |r - mean r|: how grainy the picture is.
long block_abs_deviation(const QByteArray& pixels) {
  long sum = 0;
  for (int y = 16; y < 48; y++) {
    for (int x = 16; x < 48; x++) sum += pixel_at(pixels, 64, x, y).r;
  }
  const double mean = sum / 1024.0;
  double dev = 0.0;
  for (int y = 16; y < 48; y++) {
    for (int x = 16; x < 48; x++) dev += std::abs(pixel_at(pixels, 64, x, y).r - mean);
  }
  return long(dev);
}

}  // namespace

void TestEffectsGpu::denoiseStrengthZeroIdentity() {
  // Strength 0 skips the filter and outputs the input texel unchanged, even at the widest radius.
  auto seq = h_->make_sequence(64, 64, 30.0);
  Clip* c = h_->add_generator_clip(seq.get(), -1, 0, 30, EFFECT_INTERNAL_GRADIENT);
  Effect* gradient = c->effects[1].get();
  h_->set_field_color(gradient, 1, 0, QColor(Qt::black));
  h_->set_field_color(gradient, 2, 0, QColor(Qt::white));
  h_->set_field_double(gradient, 3, 0, 90.0);
  QByteArray baseline = h_->render_frame(seq.get(), 0);
  attach_denoise(h_.get(), c, 6.0, 0.0, 0.0);
  QByteArray with_fx = h_->render_frame(seq.get(), 0);
  QVERIFY2(buffers_within(baseline, with_fx, /*tolerance=*/2),
           qPrintable(QString("expected identity, diff=%1").arg(buf_diff(baseline, with_fx))));
}

void TestEffectsGpu::denoiseFlatSolid() {
  // Flat colour: every tap equals the centre, so the weighted mean is the centre whatever the weights.
  // Opaque (200,100,50) stays (200,100,50). At 50% opacity (alpha 128/255 = 0.502) over an opaque (50,100,150) track,
  // the straight colour is filtered then premultiplied again: R 200 * 0.502 + 50 * 0.498 = 125, G 100,
  // B 50 * 0.502 + 150 * 0.498 = 100. Without the re-multiply the straight colour would be added: R 200 + 25 = 225.
  struct Case {
    double opacity, strength;
    QColor expected;
  };
  const Case cases[] = {{100.0, 30.0, QColor(200, 100, 50)},
                        {100.0, 100.0, QColor(200, 100, 50)},
                        {50.0, 30.0, QColor(125, 100, 100)},
                        {50.0, 100.0, QColor(125, 100, 100)}};
  for (const Case& k : cases) {
    auto seq = h_->make_sequence(64, 64, 30.0);
    Clip* bottom = h_->add_generator_clip(seq.get(), -1, 0, 30, EFFECT_INTERNAL_SOLID);
    h_->set_field_color(bottom->effects[1].get(), 2, 0, QColor(50, 100, 150));
    Clip* top = h_->add_generator_clip(seq.get(), -2, 0, 30, EFFECT_INTERNAL_SOLID);
    h_->set_field_double(top->effects[1].get(), 1, 0, k.opacity);
    h_->set_field_color(top->effects[1].get(), 2, 0, QColor(200, 100, 50));
    attach_denoise(h_.get(), top, 6.0, k.strength, 0.0);
    QByteArray pixels = h_->render_frame(seq.get(), 0);
    Rgba mid = pixel_at(pixels, 64, 32, 32);
    QVERIFY2(qAbs(mid.r - k.expected.red()) <= 4 && qAbs(mid.g - k.expected.green()) <= 4 &&
                 qAbs(mid.b - k.expected.blue()) <= 4,
             qPrintable(QString("opacity %1, strength %2: expected (%3,%4,%5), got (%6,%7,%8)")
                            .arg(k.opacity)
                            .arg(k.strength)
                            .arg(k.expected.red())
                            .arg(k.expected.green())
                            .arg(k.expected.blue())
                            .arg(mid.r)
                            .arg(mid.g)
                            .arg(mid.b)));
  }
}

void TestEffectsGpu::denoiseReducesNoise() {
  // Grey 128 Solid with the Noise shader at its default amount 20: grey grain uniform in +-0.2, mean |dev| ~25 per
  // pixel. Denoise radius 2 (sigma_s 1), strength 100 (sigma_r 0.3 per channel), detail 0. Noise amount 10, not 20:
  // at 20 with detail 50 a CPU model gives 0.48..0.50x, too close to the 0.5 bar for drivers whose tan() differs
  // (the Noise shader hashes with fract(tan(x))); amount 10 / detail 0 models at 0.27..0.31x.
  auto seq = h_->make_sequence(64, 64, 30.0);
  Clip* c = h_->add_generator_clip(seq.get(), -1, 0, 30, EFFECT_INTERNAL_SOLID);
  h_->set_field_color(c->effects[1].get(), 2, 0, QColor(128, 128, 128));
  EffectPtr noise = h_->attach_xml_shader(c, "Noise");
  h_->set_field_double(noise.get(), 0, 0, 10.0);
  const long noisy = block_abs_deviation(h_->render_frame(seq.get(), 0));
  QVERIFY2(noisy > 1024 * 5, qPrintable(QString("Noise shader barely changed the solid, deviation %1").arg(noisy)));
  attach_denoise(h_.get(), c, 2.0, 100.0, 0.0);
  const long denoised = block_abs_deviation(h_->render_frame(seq.get(), 0));
  QVERIFY2(
      denoised * 2 <= noisy,
      qPrintable(QString("expected the deviation at least halved: noise %1, denoised %2").arg(noisy).arg(denoised)));
}

void TestEffectsGpu::denoiseKeepsHardEdge() {
  // Black/white checkerboard, vertical edge between x = 15 (black) and 16 (white). Denoise radius 4 (sigma_s 2),
  // strength 30 (sigma_r = 0.02 + 0.28 * 0.3 = 0.104): a tap across the edge has |d|^2 / 3 = 1, range
  // weight exp(-1 / (2 * 0.104^2)) = exp(-46) ~ 0, so both sides keep their value (0 and 255). A Gaussian Blur of the
  // same sigma 2 mixes the cells: (15,8) ~104, (16,8) ~154.
  struct Probe {
    int x, y;
  };
  const Probe probes[] = {{15, 8}, {16, 8}};

  auto base_seq = h_->make_sequence(64, 64, 30.0);
  add_checkerboard(h_.get(), base_seq.get(), QColor(255, 255, 255));
  QByteArray baseline = h_->render_frame(base_seq.get(), 0);

  auto den_seq = h_->make_sequence(64, 64, 30.0);
  Clip* dc = add_checkerboard(h_.get(), den_seq.get(), QColor(255, 255, 255));
  attach_denoise(h_.get(), dc, 4.0, 30.0, 50.0);
  QByteArray denoised = h_->render_frame(den_seq.get(), 0);

  auto blur_seq = h_->make_sequence(64, 64, 30.0);
  Clip* bc = add_checkerboard(h_.get(), blur_seq.get(), QColor(255, 255, 255));
  EffectPtr blur = h_->attach_xml_shader(bc, "Gaussian Blur");
  h_->set_field_double(blur.get(), 0, 0, 2.0);  // sigma
  QByteArray blurred = h_->render_frame(blur_seq.get(), 0);

  for (const Probe& p : probes) {
    const int base = pixel_at(baseline, 64, p.x, p.y).r;
    const int den = pixel_at(denoised, 64, p.x, p.y).r;
    const int gauss = pixel_at(blurred, 64, p.x, p.y).r;
    QVERIFY2(qAbs(den - base) <= 10 && qAbs(gauss - base) >= 50,
             qPrintable(QString("(%1,%2): no effect %3, Denoise %4 (want within 10), Gaussian Blur %5 (want 50+ away)")
                            .arg(p.x)
                            .arg(p.y)
                            .arg(base)
                            .arg(den)
                            .arg(gauss)));
  }
}

void TestEffectsGpu::denoiseBilateralWeights() {
  // Black / grey g = 128/255 = 0.50196 checkerboard, edge between x = 15 and 16, radius 2 (sigma_s 1), strength 100
  // (sigma_r 0.3 per channel), detail 0. At y = 8 all 5 rows are in the same cell; per column dx the spatial weights
  // sum to exp(-dx^2 / 2) * S, S = 1 + 2 * 0.60653 + 2 * 0.13534 = 2.48373. Same side: columns 0, -1, -2 (or 0, 1, 2)
  // → S * 1.74187 = 4.32633. Other side: S * 0.74187 = 1.84260, times the range weight exp(-(3 g^2 / 3) / 0.18) =
  // 0.24665 → 0.45447. (15,8): 0.45447 * g / 4.78080 = 0.04772 → 12. (16,8): 4.32633 * g / 4.78080 = 0.45425 → 116.
  // Plain gaussian (no range weight): 38 and 90.
  auto seq = h_->make_sequence(64, 64, 30.0);
  Clip* c = add_checkerboard(h_.get(), seq.get(), QColor(128, 128, 128));
  attach_denoise(h_.get(), c, 2.0, 100.0, 0.0);
  QByteArray pixels = h_->render_frame(seq.get(), 0);
  struct Probe {
    int x, y, expected;
  };
  const Probe probes[] = {{15, 8, 12}, {16, 8, 116}};
  for (const Probe& p : probes) {
    Rgba px = pixel_at(pixels, 64, p.x, p.y);
    QVERIFY2(qAbs(px.r - p.expected) <= 4 && qAbs(px.g - p.expected) <= 4 && qAbs(px.b - p.expected) <= 4,
             qPrintable(QString("(%1,%2): expected %3, got (%4,%5,%6)")
                            .arg(p.x)
                            .arg(p.y)
                            .arg(p.expected)
                            .arg(px.r)
                            .arg(px.g)
                            .arg(px.b)));
  }
}

void TestEffectsGpu::denoiseIgnoresTransparentTaps() {
  // Grey (60,60,60) Solid, Crop left 25%: texels x <= 15 become (0,0,0,0), x = 16 is the first opaque column.
  // Denoise radius 4, strength 100, detail 0 at (16,32): the transparent taps have weight 0, every remaining tap is
  // the same grey → stays 60. Were they counted as black (range weight exp(-0.2353^2 / 0.18) = 0.735 on columns
  // dx -1..-4 = 9.546 of spatial weight, vs 14.444 on the grey side) the pixel would drop to ~40.
  auto seq = h_->make_sequence(64, 64, 30.0);
  Clip* c = h_->add_generator_clip(seq.get(), -1, 0, 30, EFFECT_INTERNAL_SOLID);
  h_->set_field_color(c->effects[1].get(), 2, 0, QColor(60, 60, 60));
  EffectPtr crop = h_->attach_xml_shader(c, "Crop");
  h_->set_field_double(crop.get(), 0, 0, 25.0);
  attach_denoise(h_.get(), c, 4.0, 100.0, 0.0);
  QByteArray pixels = h_->render_frame(seq.get(), 0);
  Rgba px = pixel_at(pixels, 64, 16, 32);
  QVERIFY2(qAbs(px.r - 60) <= 4 && qAbs(px.g - 60) <= 4 && qAbs(px.b - 60) <= 4,
           qPrintable(QString("expected ~(60,60,60), got (%1,%2,%3)").arg(px.r).arg(px.g).arg(px.b)));
}

// Phase 4 — only test that exercises the YUV decode path (yuv2rgb.frag.qsb).
// All other slots use generator clips (RGBA-direct, bypass YUV). This slot
// loads a committed 1-frame H.264 yuv420p MP4 containing an 8x8 checkerboard,
// decodes it via the Cacher, and asserts the readback recovers the pattern.
void TestEffectsGpu::yuvDecodeRoundtrip() {
  if (!avcodec_find_decoder(AV_CODEC_ID_H264)) {
    QSKIP("H.264 decoder unavailable in this build of libavcodec");
  }

  auto seq = h_->make_sequence(64, 64, 30.0);
  Media* m = h_->import_video_media(SOURCE_DIR "/tests/fixtures/t1_yuv_input.mp4",
                                    /*w=*/64, /*h=*/64, /*fps=*/30.0);
  QVERIFY(m != nullptr);
  h_->add_video_clip(seq.get(), 0, 30, m);

  QByteArray out = h_->render_frame(seq.get(), 0);
  QVERIFY(!out.isEmpty());

  // Sum-of-luma canary: any decode that produced white blocks yields ~522 000
  // (4096 pixels, half white at luma ≈ 255). A black/empty frame yields 0.
  // Catches "Cacher returned nothing" before the per-block loop emits 32+
  // individual failures.
  long luma_sum = 0;
  for (int i = 0; i + 3 < out.size(); i += 4) {
    luma_sum += (static_cast<uchar>(out[i + 0]) + static_cast<uchar>(out[i + 1]) + static_cast<uchar>(out[i + 2])) / 3;
  }
  QVERIFY2(luma_sum > 100000, qPrintable(QString("yuv decode produced near-empty frame, luma_sum=%1").arg(luma_sum)));

  // 8x8 checkerboard: 8 rows x 8 cols of 8-pixel blocks. (bx+by)%2 == 0 => white.
  for (int by = 0; by < 8; ++by) {
    for (int bx = 0; bx < 8; ++bx) {
      const bool expected_white = ((bx + by) % 2 == 0);
      // Sample the centre of the block to dodge YUV 4:2:0 chroma-edge artifacts.
      const int px = bx * 8 + 4;
      const int py = by * 8 + 4;
      const int off = (py * 64 + px) * 4;
      const uchar r = static_cast<uchar>(out[off + 0]);
      const uchar g = static_cast<uchar>(out[off + 1]);
      const uchar b = static_cast<uchar>(out[off + 2]);
      const int luma = (int(r) + int(g) + int(b)) / 3;
      if (expected_white) {
        QVERIFY2(luma > 200, qPrintable(QString("block (%1,%2) expected white, luma=%3 rgb=(%4,%5,%6)")
                                            .arg(bx)
                                            .arg(by)
                                            .arg(luma)
                                            .arg(r)
                                            .arg(g)
                                            .arg(b)));
      } else {
        QVERIFY2(luma < 55, qPrintable(QString("block (%1,%2) expected black, luma=%3 rgb=(%4,%5,%6)")
                                           .arg(bx)
                                           .arg(by)
                                           .arg(luma)
                                           .arg(r)
                                           .arg(g)
                                           .arg(b)));
      }
    }
  }
}

void TestEffectsGpu::yuvDecodeNV12() {
  // Exercises the NV12 branch in engine/clip.cpp:756-763 (interleaved CbCr
  // plane, distinct from the YUV420P path covered by yuvDecodeRoundtrip).
  // Fixture is a rawvideo NV12 .mkv with the same 8x8 checkerboard pattern.
  if (!avcodec_find_decoder(AV_CODEC_ID_RAWVIDEO)) {
    QSKIP("rawvideo decoder unavailable in this build of libavcodec");
  }

  auto seq = h_->make_sequence(64, 64, 30.0);
  Media* m = h_->import_video_media(SOURCE_DIR "/tests/fixtures/t2_nv12_input.mkv",
                                    /*w=*/64, /*h=*/64, /*fps=*/30.0);
  QVERIFY(m != nullptr);
  h_->add_video_clip(seq.get(), 0, 30, m);

  QByteArray out = h_->render_frame(seq.get(), 0);
  QVERIFY(!out.isEmpty());

  // Sum-of-luma canary: see yuvDecodeRoundtrip for rationale.
  long luma_sum = 0;
  for (int i = 0; i + 3 < out.size(); i += 4) {
    luma_sum += (static_cast<uchar>(out[i + 0]) + static_cast<uchar>(out[i + 1]) + static_cast<uchar>(out[i + 2])) / 3;
  }
  QVERIFY2(luma_sum > 100000, qPrintable(QString("nv12 decode produced near-empty frame, luma_sum=%1").arg(luma_sum)));

  // 8x8 checkerboard: 8 rows x 8 cols of 8-pixel blocks. (bx+by)%2 == 0 => white.
  for (int by = 0; by < 8; ++by) {
    for (int bx = 0; bx < 8; ++bx) {
      const bool expected_white = ((bx + by) % 2 == 0);
      const int px = bx * 8 + 4;
      const int py = by * 8 + 4;
      const int off = (py * 64 + px) * 4;
      const uchar r = static_cast<uchar>(out[off + 0]);
      const uchar g = static_cast<uchar>(out[off + 1]);
      const uchar b = static_cast<uchar>(out[off + 2]);
      const int luma = (int(r) + int(g) + int(b)) / 3;
      if (expected_white) {
        QVERIFY2(luma > 200, qPrintable(QString("block (%1,%2) expected white, luma=%3 rgb=(%4,%5,%6)")
                                            .arg(bx)
                                            .arg(by)
                                            .arg(luma)
                                            .arg(r)
                                            .arg(g)
                                            .arg(b)));
      } else {
        QVERIFY2(luma < 55, qPrintable(QString("block (%1,%2) expected black, luma=%3 rgb=(%4,%5,%6)")
                                           .arg(bx)
                                           .arg(by)
                                           .arg(luma)
                                           .arg(r)
                                           .arg(g)
                                           .arg(b)));
      }
    }
  }
}

int main(int argc, char* argv[]) {
  // Offscreen QPA can't create a QVulkanInstance: AMBER_TEST_RHI=vulkan needs a real platform (xcb under xvfb-run)
  if (qEnvironmentVariable("AMBER_TEST_RHI") != QLatin1String("vulkan")) qputenv("QT_QPA_PLATFORM", "offscreen");

  // Force OpenGL 3.2 core profile so the QRhi(OpenGLES2) backend's offscreen
  // context exposes a GLSL 150 binding. Without this, Qt's offscreen platform
  // hands out a GL 2.1 / GLSL 1.20 context and QRhi rejects every .qsb shader
  // (which is baked at GLSL 150 in src/CMakeLists.txt:467).
  QSurfaceFormat fmt;
  fmt.setVersion(3, 2);
  fmt.setProfile(QSurfaceFormat::CoreProfile);
  fmt.setRenderableType(QSurfaceFormat::OpenGL);
  QSurfaceFormat::setDefaultFormat(fmt);

  QGuiApplication app(argc, argv);
  TestEffectsGpu test;
  return QTest::qExec(&test, argc, argv);
}

#include "test_effects_gpu.moc"
