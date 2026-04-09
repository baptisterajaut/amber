#include <QtTest>

class TestRendering : public QObject {
  Q_OBJECT
private slots:
  void initTestCase() {
    QSKIP("Rendering integration test not yet wired up — needs Clip/Sequence compilation in test harness");
  }

  void solidClipComposition() {
    // 1. Create Sequence(320x240, 30fps)
    // 2. Create Clip with solid-red media
    // 3. Set up ComposeSequenceParams with test QRhi (QRhiGles2InitParams + QOffscreenSurface)
    // 4. Call compose_sequence()
    // 5. Readback pixels
    // 6. Assert all pixels are #FF0000FF
  }

  void twoClipComposition() {
    // Red on track -1 (back), green on track -2 (front)
    // Verify front pixels are green
  }

  void temporalCorrectness() {
    // Clip with speed=2.0, verify playhead_to_clip_frame mapping
  }
};

QTEST_MAIN(TestRendering)
#include "test_rendering.moc"
