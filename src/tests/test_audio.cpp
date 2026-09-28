#include <QRegularExpression>
#include <QtTest>

#include "core/audio.h"
#include "engine/cacher.h"
#include "rendering/audio.h"

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

  void testScrubGrainSize() {
    // 80 ms grain, rounded up to whole samples; 4 bytes per stereo S16 frame
    QCOMPARE(scrub_grain_samples(48000), 3840);
    QCOMPARE(scrub_grain_samples(44100), 3528);
    QCOMPARE(scrub_grain_samples(44101), 3529);  // 3528.08 rounds up
    QCOMPARE(scrub_grain_bytes(48000), 3840 * 4);
  }

  void testBytesToSeconds() {
    // S16 interleaved: bytes / 2 / channels / rate
    QCOMPARE(bytes_to_seconds(192000, 2, 48000), 1.0);
    QCOMPARE(bytes_to_seconds(96000, 2, 48000), 0.5);
    QCOMPARE(bytes_to_seconds(0, 2, 48000), 0.0);
  }

  void testBufferOffsetFromFrame() {
    // No audio device and not exporting: current_audio_freq() falls back to 48000 Hz; stereo S16 = 4 bytes/frame
    audio_ibuffer_frame.store(0);
    QCOMPARE(get_buffer_offset_from_frame(30.0, 0), qint64(0));
    QCOMPARE(get_buffer_offset_from_frame(30.0, 15), qint64(24000 * 4));
    QCOMPARE(get_buffer_offset_from_frame(30.0, 30), qint64(48000 * 4));

    audio_ibuffer_frame.store(10);  // offsets are relative to the buffer's first frame
    QCOMPARE(get_buffer_offset_from_frame(30.0, 40), qint64(48000 * 4));
    audio_ibuffer_frame.store(0);
  }

  void testBufferOffsetBeforeBufferStart() {
    audio_ibuffer_frame.store(100);
    QTest::ignoreMessage(QtWarningMsg, QRegularExpression("Invalid values passed to get_buffer_offset_from_frame"));
    QCOMPARE(get_buffer_offset_from_frame(30.0, 50), qint64(-1));
    audio_ibuffer_frame.store(0);
  }
};

QTEST_GUILESS_MAIN(TestAudio)
#include "test_audio.moc"
