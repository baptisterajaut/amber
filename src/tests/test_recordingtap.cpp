#include <QtTest>
#include <QBuffer>
#include "rendering/recordingtap.h"

class TestRecordingTap : public QObject {
  Q_OBJECT
 private slots:
  void testPeakSilence() {
    // 16-bit signed PCM stereo, 4 frames of silence
    QByteArray silence(16, '\0');
    QVector<double> peaks = recording_tap_compute_peaks(silence, 2, 16);
    QCOMPARE(peaks.size(), 2);
    QVERIFY(qAbs(peaks[0]) < 1e-9);
    QVERIFY(qAbs(peaks[1]) < 1e-9);
  }

  void testPeakFullScale() {
    // 16-bit, two channels, sample 0 = +max in L, sample 1 = -max in R
    QByteArray data;
    data.resize(4);
    int16_t* s = reinterpret_cast<int16_t*>(data.data());
    s[0] = 32767;
    s[1] = -32768;
    QVector<double> peaks = recording_tap_compute_peaks(data, 2, 16);
    QVERIFY(peaks[0] > 0.99);
    QVERIFY(peaks[1] > 0.99);
  }

  void testTapForwardsBytes() {
    QBuffer underlying;
    underlying.open(QIODevice::WriteOnly);
    RecordingTap tap(&underlying, 2, 16);
    tap.open(QIODevice::WriteOnly);
    QByteArray sample(8, 'A');
    tap.write(sample);
    QCOMPARE(underlying.data(), sample);
  }
};

QTEST_MAIN(TestRecordingTap)
#include "test_recordingtap.moc"
