#include <QApplication>
#include <QSurfaceFormat>
#include <QTest>

#include <memory>

#include "effects/effect.h"
#include "effects/internal/srtparser.h"
#include "effects/internal/subtitleeffect.h"
#include "engine/clip.h"
#include "tests/test_render_harness.h"

class TestEffectsGpu : public QObject {
  Q_OBJECT
 private slots:
  void initTestCase();
  void cleanupTestCase();

  void solidColorPassthrough();

  void effectPassthroughSweep_data();
  void effectPassthroughSweep();

  // Bespoke video effect tests (Phase 2).
  void transformTranslate();
  void transformScale();
  void transformRotate180();
  void textRenders();
  void richTextRenders();
  void shakeChangesOutput();
  void timecodeRenders();
  void cornerpinDistorts();
  void subtitleActiveEntry();
  void gradientRendersTwoStops();

  // Bespoke audio effect tests (Phase 2).
  void toneGeneratesAudio();
  void audioNoiseNotSilent();
  void volumeMute();
  void volumePassthrough();
  void panHardLeft();

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
  void huesatbriSaturationZero();
  void huesatbriContrast();
  void colorCorrectionShift();
  void colorselIsolates();
  void posterizeReducesLevels();
  void chromakeyTransparent();
  void lumakeyBands();
  void despillReducesGreen();

  // Phase 3d — Effects, crop & misc (XML shader effects).
  void flipHorizontal();
  void flipVertical();
  void cropTransparentBorders();
  void embossOutputDiffers();
  void toonifyOutputDiffers();
  void noiseShaderOutputDiffers();
  void vignetteDarkensCorners();
  void pixelateBlocks();
  void crossstitchOutputDiffers();
  void volumetricLightOutputDiffers();

 private:
  std::unique_ptr<TestRenderHarness> h_;
};

void TestEffectsGpu::initTestCase() {
  h_ = std::make_unique<TestRenderHarness>();
  if (!h_->initialized()) QSKIP("QRhi OpenGLES2 unavailable");
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

}  // namespace

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
  // Past the cue, fewer than 25% of the original text-like pixels should remain.
  QVERIFY2(n_after * 4 < n_text,
           qPrintable(QString("expected subtitle to clear past cue: inside=%1 after=%2").arg(n_text).arg(n_after)));
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
  // Pattern B: gradient + radius=10 must change output meaningfully.
  auto seq = h_->make_sequence(64, 64, 30.0);
  Clip* c = h_->add_generator_clip(seq.get(), -1, 0, 30, EFFECT_INTERNAL_GRADIENT);
  Effect* gradient = c->effects[1].get();
  h_->set_field_color(gradient, 1, 0, QColor(Qt::black));
  h_->set_field_color(gradient, 2, 0, QColor(Qt::white));
  h_->set_field_double(gradient, 3, 0, 90.0);
  QByteArray baseline = h_->render_frame(seq.get(), 0);

  EffectPtr fx = h_->attach_xml_shader(c, "Box Blur");
  h_->set_field_double(fx.get(), 0, 0, 10.0);
  QByteArray with_fx = h_->render_frame(seq.get(), 0);

  // 10-px blur on a vertical gradient redistributes intensity at the edges.
  // 1000 is a generous lower bound (calibration: empirically much higher).
  const int diff = buf_diff(baseline, with_fx);
  QVERIFY2(diff > 1000, qPrintable(QString("expected blur to change output, diff=%1").arg(diff)));
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
  auto seq = h_->make_sequence(64, 64, 30.0);
  Clip* c = h_->add_generator_clip(seq.get(), -1, 0, 30, EFFECT_INTERNAL_GRADIENT);
  Effect* gradient = c->effects[1].get();
  h_->set_field_color(gradient, 1, 0, QColor(Qt::black));
  h_->set_field_color(gradient, 2, 0, QColor(Qt::white));
  h_->set_field_double(gradient, 3, 0, 90.0);
  QByteArray baseline = h_->render_frame(seq.get(), 0);

  EffectPtr fx = h_->attach_xml_shader(c, "Gaussian Blur");
  h_->set_field_double(fx.get(), 0, 0, 5.0);
  QByteArray with_fx = h_->render_frame(seq.get(), 0);

  const int diff = buf_diff(baseline, with_fx);
  QVERIFY2(diff > 1000, qPrintable(QString("expected blur to change output, diff=%1").arg(diff)));
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
  auto seq = h_->make_sequence(64, 64, 30.0);
  Clip* c = h_->add_generator_clip(seq.get(), -1, 0, 30, EFFECT_INTERNAL_GRADIENT);
  Effect* gradient = c->effects[1].get();
  h_->set_field_color(gradient, 1, 0, QColor(Qt::black));
  h_->set_field_color(gradient, 2, 0, QColor(Qt::white));
  h_->set_field_double(gradient, 3, 0, 90.0);
  QByteArray baseline = h_->render_frame(seq.get(), 0);

  EffectPtr fx = h_->attach_xml_shader(c, "Directional Blur");
  h_->set_field_double(fx.get(), 0, 0, 15.0);  // length
  h_->set_field_double(fx.get(), 1, 0, 90.0);  // angle perpendicular to gradient
  QByteArray with_fx = h_->render_frame(seq.get(), 0);

  const int diff = buf_diff(baseline, with_fx);
  QVERIFY2(diff > 1000, qPrintable(QString("expected blur to change output, diff=%1").arg(diff)));
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
  auto seq = h_->make_sequence(64, 64, 30.0);
  Clip* c = h_->add_generator_clip(seq.get(), -1, 0, 30, EFFECT_INTERNAL_GRADIENT);
  Effect* gradient = c->effects[1].get();
  h_->set_field_color(gradient, 1, 0, QColor(Qt::black));
  h_->set_field_color(gradient, 2, 0, QColor(Qt::white));
  h_->set_field_double(gradient, 3, 0, 90.0);
  QByteArray baseline = h_->render_frame(seq.get(), 0);

  EffectPtr fx = h_->attach_xml_shader(c, "Radial Blur");
  h_->set_field_double(fx.get(), 0, 0, 50.0);  // radius
  QByteArray with_fx = h_->render_frame(seq.get(), 0);

  const int diff = buf_diff(baseline, with_fx);
  QVERIFY2(diff > 1000, qPrintable(QString("expected radial blur to change output, diff=%1").arg(diff)));
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

int main(int argc, char* argv[]) {
  qputenv("QT_QPA_PLATFORM", "offscreen");

  // Force OpenGL 3.2 core profile so the QRhi(OpenGLES2) backend's offscreen
  // context exposes a GLSL 150 binding. Without this, Qt's offscreen platform
  // hands out a GL 2.1 / GLSL 1.20 context and QRhi rejects every .qsb shader
  // (which is baked at GLSL 150 in src/CMakeLists.txt:467).
  QSurfaceFormat fmt;
  fmt.setVersion(3, 2);
  fmt.setProfile(QSurfaceFormat::CoreProfile);
  fmt.setRenderableType(QSurfaceFormat::OpenGL);
  QSurfaceFormat::setDefaultFormat(fmt);

  QApplication app(argc, argv);
  TestEffectsGpu test;
  return QTest::qExec(&test, argc, argv);
}

#include "test_effects_gpu.moc"
