#include <QtTest>

#include "core/fpsmeter.h"

using amber::FpsMeter;

class TestFpsMeter : public QObject {
  Q_OBJECT

  // n frames from t0 at the given rate, timestamps rounded to the millisecond like QElapsedTimer::elapsed()
  static void feed(FpsMeter& m, int n, double fps, qint64 t0 = 0) {
    for (int i = 0; i < n; i++) m.add_frame(t0 + qRound64(i * 1000.0 / fps));
  }

 private slots:
  void evenFramesOverOneSecond() {
    FpsMeter m;
    feed(m, 30, 30);
    double fps = 0;
    QVERIFY(m.rate(&fps));
    QVERIFY(qAbs(fps - 30.0) <= 0.1);
  }
  void oldFramesDropOut() {
    FpsMeter m;
    feed(m, 30, 10);         // 3 s at 10 fps
    feed(m, 120, 60, 3000);  // then 2 s at 60 fps: the 10 fps frames are out of the window
    double fps = 0;
    QVERIFY(m.rate(&fps));
    QVERIFY(qAbs(fps - 60.0) <= 0.1);
  }
  void stallLowersRate() {
    FpsMeter m;
    feed(m, 15, 30);    // 0..467 ms at 30 fps
    m.add_frame(1000);  // then nothing until 1000 ms: 15 intervals over 1 s
    double fps = 0;
    QVERIFY(m.rate(&fps));
    QVERIFY(qAbs(fps - 15.0) <= 0.1);
    QVERIFY(amber::below_target(fps, 30));
  }
  void fewerThanTwoFramesHasNoValue() {
    FpsMeter m;
    double fps = -1;
    QVERIFY(!m.rate(&fps));
    m.add_frame(100);
    QVERIFY(!m.rate(&fps));
    m.add_frame(100);  // same millisecond: no span to divide by
    QVERIFY(!m.rate(&fps));
    QCOMPARE(fps, -1.0);
    m.add_frame(2000);  // a frame after a long stall leaves it alone in the window
    QVERIFY(!m.rate(&fps));
  }
  void resetClears() {
    FpsMeter m;
    feed(m, 60, 30);
    QVERIFY(m.window_full());
    m.reset();
    double fps = 0;
    QVERIFY(!m.rate(&fps));
    QVERIFY(!m.window_full());
    feed(m, 5, 30, 10000);  // the next play starts over: a few frames don't make a full window
    QVERIFY(m.rate(&fps));
    QVERIFY(!m.window_full());
  }
  void windowFullAfterOneSecond() {
    FpsMeter m;
    feed(m, 30, 30);  // last frame at 967 ms
    QVERIFY(!m.window_full());
    m.add_frame(1000);
    QVERIFY(m.window_full());
  }
  void belowTargetIs95Percent() {
    QVERIFY(!amber::below_target(30, 30));
    QVERIFY(!amber::below_target(28.5, 30));  // exactly 95%
    QVERIFY(amber::below_target(28.4, 30));
    QVERIFY(!amber::below_target(23.976, 23.976));
    QVERIFY(amber::below_target(22.7, 23.976));  // 94.7%
    QVERIFY(!amber::below_target(0, 0));         // no sequence rate: never red
  }
};

QTEST_GUILESS_MAIN(TestFpsMeter)
#include "test_fps_meter.moc"
