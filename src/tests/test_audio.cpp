#include <QtTest>

#include "core/audio.h"

class TestAudio : public QObject {
  Q_OBJECT
 private slots:
  void testLogVolumeBounds() {
    // log_volume(0) should be 0 (fully attenuated)
    QVERIFY(qAbs(log_volume(0.0) - 0.0) < 0.001);
    // log_volume(1) should be 1 (full volume)
    QVERIFY(qAbs(log_volume(1.0) - 1.0) < 0.001);
  }

  void testLogVolumeMonotonic() {
    // log_volume should be monotonically increasing
    QVERIFY(log_volume(0.5) > log_volume(0.3));
    QVERIFY(log_volume(0.8) > log_volume(0.5));
    QVERIFY(log_volume(1.0) > log_volume(0.9));
  }

  void testLogVolumeAboveUnity() {
    // Values above 1.0 should produce values above 1.0 (amplification)
    QVERIFY(log_volume(1.5) > 1.0);
  }

  void testLogVolumeZero() { QVERIFY(qAbs(log_volume(0.0)) < 0.001); }

  void testLogVolumeNegative() {
    // formula doesn't clamp: (exp(-1) - 1) / (e - 1) ≈ -0.368
    QVERIFY(log_volume(-1.0) < 0.0);
  }

  void testLogVolumeLargeValues() {
    QVERIFY(log_volume(2.0) > 1.0);
    QVERIFY(log_volume(3.0) > log_volume(2.0));
  }

  void testLogVolumeSmallPositive() {
    double result = log_volume(0.01);
    QVERIFY(result > 0.0);
    QVERIFY(result < 0.01);  // log curve below linear for small values
  }

  void testBufferGlobalsInit() {
    // Verify buffer globals have expected initial values
    QCOMPARE(audio_ibuffer_read.load(), qint64(0));
    QCOMPARE(audio_ibuffer_frame.load(), 0L);
    QCOMPARE(audio_scrub_id.load(), 0u);
    QCOMPARE(audio_rendering, false);
    QCOMPARE(audio_rendering_rate, 0);
  }
};

QTEST_GUILESS_MAIN(TestAudio)
#include "test_audio.moc"
