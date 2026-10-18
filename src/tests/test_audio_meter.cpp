#include <QtTest>

#include "core/audiometer.h"

using amber::AudioMeterState;
using amber::kMeterFloorDb;

class TestAudioMeter : public QObject {
  Q_OBJECT

  static double lin(double db) { return std::pow(10.0, db / 20.0); }
  static bool near(double a, double b, double tol = 0.5) { return qAbs(a - b) <= tol; }

 private slots:
  void instantRise() {
    AudioMeterState m;
    m.push({0.0, 0.0}, 0);
    m.push({lin(-12), lin(-3)}, 10);
    QVERIFY(near(m.level_db(0), -12, 1e-9));
    QVERIFY(near(m.level_db(1), -3, 1e-9));
    QVERIFY(near(m.peak_hold_db(1), -3, 1e-9));
  }
  void fallsAt20DbPerSecond() {
    AudioMeterState m;
    m.push({lin(-6)}, 0);
    m.push({0.0}, 0);
    m.advance(500);
    QVERIFY2(near(m.level_db(0), -16), qPrintable(QString::number(m.level_db(0))));
  }
  void fallStopsAtLastPush() {
    AudioMeterState m;
    m.push({lin(-6)}, 0);
    m.push({lin(-10)}, 100);
    m.advance(2000);
    QVERIFY(near(m.level_db(0), -10, 1e-9));  // never below the last pushed value
    m.push({lin(-30)}, 2000);
    m.advance(2500);
    QVERIFY(near(m.level_db(0), -20));  // a newer, lower push lets it fall again
  }
  void holdStaysThenFalls() {
    AudioMeterState m;
    m.push({lin(-6)}, 0);
    m.push({0.0}, 0);
    m.advance(1000);
    QVERIFY(near(m.peak_hold_db(0), -6, 1e-9));
    m.advance(1500);
    QVERIFY(near(m.peak_hold_db(0), -6, 1e-9));
    m.advance(2000);
    QVERIFY(near(m.peak_hold_db(0), -16));
  }
  void holdRestartsWhenReached() {
    AudioMeterState m;
    m.push({lin(-6)}, 0);
    m.push({lin(-6)}, 1000);  // reached again: 1.5 s from here
    m.push({0.0}, 1000);
    m.advance(2400);
    QVERIFY(near(m.peak_hold_db(0), -6, 1e-9));
    m.advance(3000);
    QVERIFY(near(m.peak_hold_db(0), -16));
  }
  void holdNeverBelowLevel() {
    AudioMeterState m;
    m.push({lin(-6)}, 0);
    m.push({lin(-20)}, 0);  // the level falls toward -20, the hold sits at -6 then falls
    for (qint64 t = 0; t <= 6000; t += 50) {
      m.advance(t);
      QVERIFY(m.peak_hold_db(0) >= m.level_db(0));
      if (t == 3000) m.push({lin(-8)}, t);  // a new peak, above the falling hold
    }
    QVERIFY(near(m.level_db(0), -8, 1e-9));
    QVERIFY(near(m.peak_hold_db(0), -8, 1e-9));
  }
  void clipLatch() {
    AudioMeterState m;
    m.push({0.999, 32767.0 / 32768.0, 1.0}, 0);
    QVERIFY(!m.clipped(0));
    QVERIFY(m.clipped(1));  // a full-scale +32767 sample is 0.99997
    QVERIFY(m.clipped(2));
    m.push({0.0, 0.0, 0.0}, 100);
    m.advance(10000);
    QVERIFY(m.clipped(1));
    m.clear(10000);
    QVERIFY(m.clipped(1));
    m.reset_clip();
    QVERIFY(!m.clipped(1));
    QVERIFY(!m.clipped(5));
  }
  void floorForSilence() {
    AudioMeterState m;
    m.push({0.0, 1e-9}, 0);
    QCOMPARE(m.level_db(0), kMeterFloorDb);
    QCOMPARE(m.level_db(1), kMeterFloorDb);
    QCOMPARE(m.level_db(7), kMeterFloorDb);
    QCOMPARE(amber::meter_db(0.0), kMeterFloorDb);
    QVERIFY(near(amber::meter_db(0.5), -6.02, 0.01));
  }
  void channelCountChangeResets() {
    AudioMeterState m;
    m.push({1.0, 1.0}, 0);
    QCOMPARE(m.channel_count(), 2);
    m.push({0.0}, 100);
    QCOMPARE(m.channel_count(), 1);
    QCOMPARE(m.level_db(0), kMeterFloorDb);
    QCOMPARE(m.peak_hold_db(0), kMeterFloorDb);
    QVERIFY(!m.clipped(0));
  }
  void clearFallsSmoothly() {
    AudioMeterState m;
    m.push({lin(-6), lin(-6)}, 0);
    m.clear(0);
    QCOMPARE(m.channel_count(), 2);
    QVERIFY(near(m.level_db(0), -6, 1e-9));  // no snap to the floor
    m.advance(500);
    QVERIFY(near(m.level_db(1), -16));
  }
  void stopsAnimatingOnTheFloor() {
    AudioMeterState m;
    QVERIFY(!m.animating());
    m.push({lin(-6), 1.0}, 0);
    m.clear(0);
    QVERIFY(m.animating());
    m.advance(3000);  // levels down 60 dB: on the floor, holds still up
    QCOMPARE(m.level_db(0), kMeterFloorDb);
    QVERIFY(m.animating());
    m.advance(6000);  // holds: 1.5 s + 60 dB at 20 dB/s
    QVERIFY(!m.animating());
    QVERIFY(m.clipped(1));  // the latch alone doesn't animate
  }
};

QTEST_GUILESS_MAIN(TestAudioMeter)
#include "test_audio_meter.moc"
