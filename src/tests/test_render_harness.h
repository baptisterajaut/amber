#ifndef TEST_RENDER_HARNESS_H
#define TEST_RENDER_HARNESS_H

#include <memory>

#include <rhi/qrhi.h>
#include <rhi/qshader.h>
#include <QByteArray>
#include <QColor>
#include <QString>

#include "effects/effect.h"
#include "engine/sequence.h"
#include "tests/test_appcontext_stub.h"

class Clip;
class QOffscreenSurface;

// TestRenderHarness — encapsulates QRhi(OpenGLES2) setup, sequence/clip/effect
// construction, and compose_sequence() wiring for headless effect integration tests.
//
// Lifetime: constructed once per QTest fixture in initTestCase(), reused across all
// slots, destroyed in cleanupTestCase(). Initialisation can fail silently if
// QRhi(OpenGLES2) is unavailable — call initialized() and QSKIP() in that case.
class TestRenderHarness {
 public:
  TestRenderHarness();
  ~TestRenderHarness();

  // Returns true if QRhi(OpenGLES2) initialisation succeeded.
  // If false, tests should QSKIP() — the host can't run GL.
  bool initialized() const { return rhi_ != nullptr; }

  // Build a 1-clip-track sequence, no media. Caller owns the SequencePtr.
  SequencePtr make_sequence(int w, int h, double fps);

  // Add a generator clip to the sequence: no media, attaches default Transform +
  // the requested generator effect. Returns a non-owning Clip*.
  // OLIVE QUIRK: `track` uses NEGATIVE indices for VIDEO (-1 = topmost video),
  // POSITIVE for audio. See clip.cpp:203 and renderfunctions.cpp.
  // Defaults to -1 (top video track) — pass a different value only for tests
  // that need multiple tracks.
  Clip* add_generator_clip(Sequence* seq, int track, long in, long out, EffectInternal generator);

  // Attach an effect (XML or internal) to an existing clip. Returns the EffectPtr.
  EffectPtr attach_effect(Clip* clip, const EffectMeta* meta);
  EffectPtr attach_internal(Clip* clip, EffectInternal id, int type);
  EffectPtr attach_xml_effect(Clip* clip, const QString& effect_name);

  // Field setters — all go through SetValueAt(0.0, ...).
  // When the field is non-keyframable (default), SetValueAt writes persistent_data_
  // which is what GetValueAt returns at every timecode. This matches what
  // production code does (e.g. solideffect.cpp:54).
  //
  // DO NOT use SetDefaultData() — it only writes default_data_, which the render
  // path never reads (only the UI's "Reset to Defaults" command does).
  void set_field_double(Effect* e, int row, int field, double value);
  void set_field_color(Effect* e, int row, int field, QColor value);
  void set_field_bool(Effect* e, int row, int field, bool value);
  void set_field_string(Effect* e, int row, int field, const QString& value);
  void set_field_combo(Effect* e, int row, int field, int index);

  // Run compose_sequence() at `playhead` (sequence frame). Reads back the main
  // texture. Returns RGBA8 bytes, row-major, size = w*h*4. Reports failure via
  // QFAIL on RHI errors (caller's QTest slot reports failure).
  QByteArray render_frame(Sequence* seq, long playhead);

  // Pixel assertions. `pixels` is the QByteArray from render_frame.
  // `w` is the sequence width (needed to compute the row offset for (x, y)).
  void assert_pixel(const QByteArray& pixels, int w, int x, int y, QColor expected, int tolerance = 2);
  void assert_solid_color(const QByteArray& pixels, int w, int h, QColor expected, int tolerance = 2);

 private:
  void load_core_shaders();
  void ensure_buffers(int w, int h);  // allocate or re-allocate if size changed
  void release_buffers();

  std::unique_ptr<QOffscreenSurface> fallback_surface_;
  std::unique_ptr<QRhi> rhi_;
  QShader passthrough_vert_, passthrough_frag_, blending_frag_, premultiply_frag_, yuv_frag_;
  QRhiSampler* sampler_ = nullptr;

  int buf_w_ = 0, buf_h_ = 0;
  QRhiTexture* main_tex_ = nullptr;
  QRhiTextureRenderTarget* main_target_ = nullptr;        // PreserveColorContents
  QRhiTextureRenderTarget* main_target_clear_ = nullptr;  // non-preserve alias (for initial clear)
  QRhiRenderPassDescriptor* main_rpd_ = nullptr;
  QRhiTexture* backend_tex_ = nullptr;
  QRhiTextureRenderTarget* backend_target_ = nullptr;
  QRhiRenderPassDescriptor* backend_rpd_ = nullptr;

  TestAppContext app_ctx_;
};

#endif  // TEST_RENDER_HARNESS_H
