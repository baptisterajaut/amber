#include <QApplication>
#include <QTest>

#include <memory>

#include "effects/effect.h"
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
  QTest::addColumn<QString>("effect_name");
  QTest::addColumn<int>("internal_id");

  // `effects` is the global QVector<EffectMeta> from effect.h:60 (no namespace).
  for (const EffectMeta& meta : effects) {
    // Skip transitions (they need a different setup with two clips).
    if (meta.type != EFFECT_TYPE_EFFECT) continue;
    // Skip audio-subtype effects — sweep is video-only. Audio effects are
    // exercised in Phase 2 once render_audio() exists.
    if (meta.subtype == EFFECT_TYPE_AUDIO) continue;
    // Skip deprecated / GUI-only / disabled internals.
    if (meta.internal == EFFECT_INTERNAL_FREI0R) continue;
    if (meta.internal == EFFECT_INTERNAL_VST) continue;
    if (meta.internal == EFFECT_INTERNAL_MASK) continue;  // disabled
    QTest::newRow(qPrintable(meta.name)) << meta.name << meta.internal;
  }
}

void TestEffectsGpu::effectPassthroughSweep() {
  QFETCH(QString, effect_name);
  QFETCH(int, internal_id);

  auto seq = h_->make_sequence(64, 64, 30.0);
  Clip* c = h_->add_generator_clip(seq.get(), -1, 0, 30, EFFECT_INTERNAL_SOLID);
  // Set Solid to a non-black colour so we can detect "all-zero output" as a failure mode.
  Effect* solid = c->effects[1].get();
  h_->set_field_color(solid, 2, 0, QColor(128, 128, 128, 255));

  // Attach the effect under test on top of the Solid generator.
  // Skip if it's the same internal as the generator (Solid already there).
  if (internal_id >= 0 && internal_id < EFFECT_INTERNAL_COUNT) {
    if (static_cast<EffectInternal>(internal_id) != EFFECT_INTERNAL_TRANSFORM &&
        static_cast<EffectInternal>(internal_id) != EFFECT_INTERNAL_SOLID) {
      h_->attach_internal(c, static_cast<EffectInternal>(internal_id), EFFECT_TYPE_EFFECT);
    }
  } else {
    h_->attach_xml_effect(c, effect_name);
  }

  QByteArray pixels = h_->render_frame(seq.get(), 0);
  QVERIFY2(!pixels.isEmpty(), qPrintable("readback empty for " + effect_name));
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
  QVERIFY2(any_non_zero, qPrintable("output all zeros for " + effect_name));
}

int main(int argc, char* argv[]) {
  qputenv("QT_QPA_PLATFORM", "offscreen");
  QApplication app(argc, argv);
  TestEffectsGpu test;
  return QTest::qExec(&test, argc, argv);
}

#include "test_effects_gpu.moc"
