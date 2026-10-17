#include <QtTest>

#include "core/scrubresolution.h"

using amber::ScrubHalfRes;
using amber::ScrubResolution;

class TestScrubResolution : public QObject {
  Q_OBJECT

 private slots:
  void nonScrubAlwaysBase() {
    ScrubResolution r;
    QCOMPARE(r.divider(1, ScrubHalfRes::Always, false, 1080), 1);
    QCOMPARE(r.divider(2, ScrubHalfRes::Always, false, 1080), 2);
  }
  void modes() {
    ScrubResolution r;
    QCOMPARE(r.divider(1, ScrubHalfRes::Always, true, 1080), 2);
    QCOMPARE(r.divider(1, ScrubHalfRes::Never, true, 1080), 1);
    QCOMPARE(r.divider(1, ScrubHalfRes::Auto, true, 1080), 1);  // not latched yet
  }
  void capAndFloor() {
    ScrubResolution r;
    QCOMPARE(r.divider(4, ScrubHalfRes::Always, true, 2160), 8);
    QCOMPARE(r.divider(8, ScrubHalfRes::Always, true, 2160), 8);  // already at the cap
    QCOMPARE(r.divider(1, ScrubHalfRes::Always, true, 100), 1);   // 100 / 2 < 64
    QCOMPARE(r.divider(2, ScrubHalfRes::Always, true, 300), 4);   // 300 / 4 = 75
    QCOMPARE(r.divider(2, ScrubHalfRes::Always, true, 200), 2);   // 200 / 4 = 50
  }
  void autoLatchesAfterTwoSlowFrames() {
    ScrubResolution r;
    r.record_frame(1, 50, 1, 1, 30);  // budget 33.3 ms
    QVERIFY(!r.reduced());
    r.record_frame(2, 50, 1, 1, 30);
    QVERIFY(r.reduced());
    QCOMPARE(r.divider(1, ScrubHalfRes::Auto, true, 1080), 2);
  }
  void fastFrameResetsCount() {
    ScrubResolution r;
    r.record_frame(1, 50, 1, 1, 30);
    r.record_frame(2, 10, 1, 1, 30);
    r.record_frame(3, 50, 1, 1, 30);
    QVERIFY(!r.reduced());
  }
  void sameSerialCountsOnce() {
    ScrubResolution r;
    r.record_frame(7, 50, 1, 1, 30);
    r.record_frame(7, 50, 1, 1, 30);
    QVERIFY(!r.reduced());
  }
  void reducedFramesDontCountOrUnlatch() {
    ScrubResolution r;
    r.record_frame(1, 50, 2, 1, 30);  // rendered reduced: ignored
    r.record_frame(2, 50, 2, 1, 30);
    QVERIFY(!r.reduced());
    r.record_frame(3, 50, 1, 1, 30);
    r.record_frame(4, 50, 1, 1, 30);
    QVERIFY(r.reduced());
    r.record_frame(5, 1, 2, 1, 30);  // fast reduced frame does not unlatch
    QVERIFY(r.reduced());
  }
  void skippedSerialIsNotCounted() {
    ScrubResolution r;
    r.skip_serial(5);
    r.record_frame(5, 50, 1, 1, 30);  // pre-gesture frame: ignored
    r.record_frame(6, 50, 1, 1, 30);
    QVERIFY(!r.reduced());  // only one slow scrub frame so far
    r.record_frame(7, 50, 1, 1, 30);
    QVERIFY(r.reduced());
  }
  void endGestureResets() {
    ScrubResolution r;
    r.record_frame(1, 50, 1, 1, 30);
    r.record_frame(2, 50, 1, 1, 30);
    r.end_gesture();
    QVERIFY(!r.reduced());
    r.record_frame(3, 50, 1, 1, 30);
    QVERIFY(!r.reduced());  // count restarted
  }
  void budgetFollowsFrameRate() {
    ScrubResolution a;  // 24 fps: budget 41.7 ms
    a.record_frame(1, 40, 1, 1, 24);
    a.record_frame(2, 40, 1, 1, 24);
    QVERIFY(!a.reduced());
    ScrubResolution b;  // 60 fps: budget 16.7 ms
    b.record_frame(1, 20, 1, 1, 60);
    b.record_frame(2, 20, 1, 1, 60);
    QVERIFY(b.reduced());
  }
  void baseDividerFramesCount() {
    ScrubResolution r;  // preview at 1/2: frames rendered at 2 are full-divider frames
    r.record_frame(1, 50, 2, 2, 30);
    r.record_frame(2, 50, 2, 2, 30);
    QVERIFY(r.reduced());
    ScrubResolution s;  // rendered at 4 with base 2: reduced frames, ignored
    s.record_frame(1, 50, 4, 2, 30);
    s.record_frame(2, 50, 4, 2, 30);
    QVERIFY(!s.reduced());
  }
  void latchedRespectsModeAndScrub() {
    ScrubResolution r;
    r.record_frame(1, 50, 1, 1, 30);
    r.record_frame(2, 50, 1, 1, 30);
    QVERIFY(r.reduced());
    QCOMPARE(r.divider(1, ScrubHalfRes::Never, true, 1080), 1);
    QCOMPARE(r.divider(1, ScrubHalfRes::Auto, false, 1080), 1);
    QCOMPARE(r.divider(2, ScrubHalfRes::Auto, true, 1080), 4);
  }
  void floorBoundary() {
    ScrubResolution r;
    QCOMPARE(r.divider(1, ScrubHalfRes::Always, true, 128), 2);  // 128 / 2 = 64: allowed
    QCOMPARE(r.divider(1, ScrubHalfRes::Always, true, 127), 1);  // 127 / 2 = 63: too small
  }
  void frameAtBudgetIsNotSlow() {
    ScrubResolution r;  // 25 fps: budget exactly 40 ms
    r.record_frame(1, 40.0, 1, 1, 25);
    r.record_frame(2, 40.0, 1, 1, 25);
    QVERIFY(!r.reduced());
  }
  void zeroFrameRateAndSerialNeverLatch() {
    ScrubResolution a;
    a.record_frame(1, 50, 1, 1, 0);
    a.record_frame(2, 50, 1, 1, 0);
    a.record_frame(3, 50, 1, 1, -1);  // a negative budget would make every frame slow
    a.record_frame(4, 50, 1, 1, -1);
    QVERIFY(!a.reduced());
    ScrubResolution b;
    b.record_frame(0, 50, 1, 1, 30);
    b.record_frame(1, 50, 1, 1, 30);
    b.record_frame(0, 50, 1, 1, 30);  // serial 0 = nothing rendered yet, even after a real serial
    QVERIFY(!b.reduced());
  }
};

QTEST_GUILESS_MAIN(TestScrubResolution)
#include "test_scrub_resolution.moc"
