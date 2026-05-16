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
#include "project/media.h"
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

  // Attach an XML shader effect by its display name (e.g. "Box Blur",
  // "Hue/Saturation/Brightness"). Walks the global `effects` vector and matches
  // on EffectMeta::name exactly. Filters out internal effects (meta.internal !=
  // -1) so the audio Noise (EFFECT_INTERNAL_NOISE) doesn't shadow the XML
  // shader Noise (internal == -1). Q_ASSERTs if not found.
  //
  // Why not get_meta_from_name(): that helper splits on '/' (effect.cpp:1072),
  // which breaks names like "Hue/Saturation/Brightness".
  EffectPtr attach_xml_shader(Clip* clip, const QString& name);

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

  // Build a `Media*` of type MEDIA_TYPE_FOOTAGE backed by an on-disk video file
  // (e.g. a committed .mp4 fixture). The harness owns the MediaPtr+FootagePtr —
  // they live until the harness is destroyed (or clear_owned_media() is called).
  //
  // PreviewGenerator::AnalyzeMedia is a no-op in the test build (test_ui_stubs.cpp),
  // so we populate Footage::url + Footage::ready=true + a single FootageStream
  // directly. The cacher opens the file via FFmpeg and reads the actual streams;
  // it does not depend on prior analysis.
  Media* import_video_media(const QString& path, int width, int height, double frame_rate);

  // Add a video clip backed by a Media* (from import_video_media). Mirrors
  // add_generator_clip but without injecting Transform/generator effects — the
  // clip's source is the decoded footage, not a GPU generator. Returns a
  // non-owning Clip*.
  Clip* add_video_clip(Sequence* seq, long in, long out, Media* media);

  // Add an audio generator clip on an audio track (track >= 0). No media,
  // no Transform (video-only). Attaches the requested audio generator
  // (e.g. EFFECT_INTERNAL_TONE, EFFECT_INTERNAL_NOISE) as the first effect.
  // Subsequent effects can be attached with attach_internal().
  Clip* add_audio_generator_clip(Sequence* seq, int track, long in, long out, EffectInternal generator);

  // Synchronously evaluate the clip's audio chain over
  // [timecode_start, timecode_start + duration_ms / 1000] seconds. Walks
  // clip->effects in order and calls each effect's process_audio() in place on
  // a local stereo S16 LE interleaved buffer (4 bytes per frame). Returns the
  // mutated buffer.
  //
  // Does NOT touch the global audio_ibuffer or kick the Cacher — the production
  // audio path is asynchronous (Cacher worker writes the ring buffer) and racy
  // for tests. This bypass exercises the same per-effect contract used in
  // production minus the threading.
  QByteArray render_audio(Clip* clip, double timecode_start, int duration_ms);

  // Pixel assertions. `pixels` is the QByteArray from render_frame.
  // `w` is the sequence width (needed to compute the row offset for (x, y)).
  void assert_pixel(const QByteArray& pixels, int w, int x, int y, QColor expected, int tolerance = 2);
  void assert_solid_color(const QByteArray& pixels, int w, int h, QColor expected, int tolerance = 2);

  // Audio assertions. `samples` is stereo S16 LE interleaved (4 bytes/frame).
  // `channel`: 0 = left, 1 = right.
  void assert_audio_silence(const QByteArray& samples, int tolerance = 1);
  void assert_audio_non_silence(const QByteArray& samples, int min_abs_threshold = 1000);
  void assert_channel_silence(const QByteArray& samples, int channel, int tolerance = 1);
  void assert_channels_equal(const QByteArray& samples, int tolerance = 1);

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

  // Keeps MediaPtr / FootagePtr alive for the harness lifetime. Clip stores a
  // raw Media*, so something has to own the shared_ptr until tests tear down.
  QVector<MediaPtr> owned_media_;
};

#endif  // TEST_RENDER_HARNESS_H
