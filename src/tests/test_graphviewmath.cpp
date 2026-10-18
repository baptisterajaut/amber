#include <QtTest>

#include <cmath>

#include "core/graphmath.h"
#include "effects/effect.h"
#include "effects/keyframe.h"

using namespace amber::graph;

// Pure math of the Graph Editor (core/graphmath), no window
class TestGraphViewMath : public QObject {
  Q_OBJECT

 private slots:
  void dragValueIsNotRounded() {
    // A 0-1 field: the value follows the mouse, it no longer jumps to 0 or 1
    QCOMPARE(ClampToRange(0.37, 0.0, 1.0), 0.37);
    QCOMPARE(ClampToRange(12.25, qSNaN(), qSNaN()), 12.25);
  }

  void dragValueIsClampedToTheFieldRange() {
    QCOMPARE(ClampToRange(1.4, 0.0, 1.0), 1.0);
    QCOMPARE(ClampToRange(-0.2, 0.0, 1.0), 0.0);
    // A NaN bound is no bound (DoubleField's default)
    QCOMPARE(ClampToRange(-500.0, qSNaN(), 100.0), -500.0);
    QCOMPARE(ClampToRange(500.0, 0.0, qSNaN()), 500.0);
    QCOMPARE(ClampToRange(-1.0, 0.0, qSNaN()), 0.0);
  }

  void fitNegativeOnlyCurve() {
    // All values below zero: the range is the keys' own, not stretched up to 0 (the old DBL_MIN start)
    const FitRect r = FitRange(0, 100, -50.0, -10.0);
    QCOMPARE(r.min_value, -54.0);
    QCOMPARE(r.max_value, -6.0);
    QCOMPARE(r.min_time, -10.0);
    QCOMPARE(r.max_time, 110.0);
  }

  void fitMixedSigns() {
    const FitRect r = FitRange(10, 30, -1.0, 1.0);
    QCOMPARE(r.min_value, -1.2);
    QCOMPARE(r.max_value, 1.2);
    QCOMPARE(r.min_time, 8.0);
    QCOMPARE(r.max_time, 32.0);
  }

  void fitSmallRangeIsNotADot() {
    // A 0-1 curve fills the view: 10 % padding, not +10 units
    const FitRect r = FitRange(0, 60, 0.0, 1.0);
    QVERIFY(qAbs(r.min_value - -0.1) < 1e-12);
    QVERIFY(qAbs(r.max_value - 1.1) < 1e-12);
  }

  void fitFlatCurve() {
    const FitRect r = FitRange(0, 60, 0.5, 0.5);
    QCOMPARE(r.min_value, -0.1);  // span widened to 1 around 0.5, then padded
    QCOMPARE(r.max_value, 1.1);
  }

  void fitSingleKey() {
    const FitRect r = FitRange(40, 40, 100.0, 100.0);
    QCOMPARE(r.min_value, 99.4);
    QCOMPARE(r.max_value, 100.6);
    QCOMPARE(r.min_time, 34.0);  // 10 frames around the key, then padded
    QCOMPARE(r.max_time, 46.0);
    QVERIFY(r.max_time > r.min_time && r.max_value > r.min_value);
  }

  void keysMayPassEachOther() {
    // Key at 0 dragged to 45, past the fixed key at 30: allowed
    QCOMPARE(AvoidKeyCollisions(45, 44, {0}, {{30, 60}}), 45L);
  }

  void keyNeverLandsOnAFixedKey() {
    // Moving right (previous offset smaller): stepped back left
    QCOMPARE(AvoidKeyCollisions(30, 29, {0}, {{30, 60}}), 29L);
    // Moving left onto 30 from 60: stepped back right
    QCOMPARE(AvoidKeyCollisions(-30, -29, {60}, {{0, 30}}), -29L);
    // Two fixed keys side by side: steps past both
    QCOMPARE(AvoidKeyCollisions(30, 20, {0}, {{29, 30}}), 28L);
  }

  void collisionsAreCheckedPerField() {
    // Key of field 0 at 0 and key of field 1 at 10, moved together by 20: field 1 has a fixed key at 30
    QCOMPARE(AvoidKeyCollisions(20, 19, {0, 10}, {{40}, {30}}), 19L);
    // The same frame in another field is not a collision
    QCOMPARE(AvoidKeyCollisions(30, 29, {0, 10}, {{}, {50}}), 30L);
  }

  void snapToNearestTargetInRange() {
    long target = -1;
    long diff = 8;  // key at 0 dragged to 8, playhead at 10, range 3 frames
    QVERIFY(SnapKeyDrag(diff, {0}, {{10}}, 3.0, &target));
    QCOMPARE(diff, 10L);
    QCOMPARE(target, 10L);

    diff = 5;  // 5 frames away: out of range
    QVERIFY(!SnapKeyDrag(diff, {0}, {{10}}, 3.0, &target));
    QCOMPARE(diff, 5L);
  }

  void snapPicksTheClosestMovingKey() {
    // Keys at 0 and 20 moved by 8: 8 is 2 from 10, 28 is 1 from 29 -> the drag goes to 9
    long target = -1;
    long diff = 8;
    QVERIFY(SnapKeyDrag(diff, {0, 20}, {{10}, {29}}, 3.0, &target));
    QCOMPARE(diff, 9L);
    QCOMPARE(target, 29L);
  }

  void snapRangeFollowsZoom() {
    // 10 px: 1 frame at 10 px/frame, 5 frames at 2 px/frame
    long diff = 7;
    QVERIFY(!SnapKeyDrag(diff, {0}, {{10}}, kSnapRangePx / 10.0, nullptr));
    QVERIFY(SnapKeyDrag(diff, {0}, {{10}}, kSnapRangePx / 2.0, nullptr));
    QCOMPARE(diff, 10L);
  }

  void valueGridSteps() {
    // 1, 2 or 5 x 10^n, the smallest at least 40 px apart
    QCOMPARE(ValueGridStep(1.0), 50.0);   // 1 px per unit: 40 -> 50
    QCOMPARE(ValueGridStep(0.4), 100.0);  // exactly 40 px
    QCOMPARE(ValueGridStep(3.0), 20.0);   // 13.3 -> 20
    QCOMPARE(ValueGridStep(300.0), 0.2);  // a 0-1 field over 300 px
    QCOMPARE(ValueGridStep(1000.0), 0.05);
    QCOMPARE(ValueGridStep(0.001), 50000.0);
    for (double px : {0.0013, 0.37, 1.0, 7.7, 123.0, 4567.0}) {
      const double step = ValueGridStep(px);
      QVERIFY(step * px >= 40.0 * (1 - 1e-9));
      QVERIFY(step * px < 100.0);
      const double mantissa = step / std::pow(10.0, std::floor(std::log10(step) + 1e-9));
      QVERIFY(qFuzzyCompare(mantissa, 1.0) || qFuzzyCompare(mantissa, 2.0) || qFuzzyCompare(mantissa, 5.0));
    }
  }

  void valueLabelDecimals() {
    QCOMPARE(StepDecimals(50.0), 0);
    QCOMPARE(StepDecimals(1.0), 0);
    QCOMPARE(StepDecimals(0.5), 1);
    QCOMPARE(StepDecimals(0.2), 1);
    QCOMPARE(StepDecimals(0.1), 1);
    QCOMPARE(StepDecimals(0.05), 2);
    QCOMPARE(StepDecimals(0.001), 3);
  }

  void timeGridSteps_data() {
    QTest::addColumn<double>("fps");
    QTest::addColumn<double>("px_per_frame");
    QTest::addColumn<long>("step");
    QTest::newRow("24 fps, 100 px/frame") << 24.0 << 100.0 << 1L;
    QTest::newRow("24 fps, 30 px/frame") << 24.0 << 30.0 << 5L;
    QTest::newRow("24 fps, 5 px/frame") << 24.0 << 5.0 << 20L;
    QTest::newRow("24 fps, 3 px/frame") << 24.0 << 3.0 << 48L;     // 20 frames is 60 px: 1 s too (72 px): 2 s
    QTest::newRow("24 fps, 0.5 px/frame") << 24.0 << 0.5 << 240L;  // 10 s
    QTest::newRow("25 fps, 4 px/frame") << 25.0 << 4.0 << 20L;
    QTest::newRow("25 fps, 3.5 px/frame") << 25.0 << 3.5 << 25L;    // 1 s = 25 frames, 87.5 px
    QTest::newRow("29.97 fps, 3 px/frame") << 29.97 << 3.0 << 30L;  // 1 s = 30 whole frames
    QTest::newRow("29.97 fps, 1 px/frame") << 29.97 << 1.0 << 150L;
    QTest::newRow("29.97 fps, tiny zoom") << 29.97 << 1e-4 << 108000L * 8;  // past an hour: doublings
  }

  void timeGridSteps() {
    QFETCH(double, fps);
    QFETCH(double, px_per_frame);
    QFETCH(long, step);
    QCOMPARE(TimeGridStep(px_per_frame, fps), step);
  }

  void easyEaseThirdOfTheWay() {
    // Key at 30 between keys at 0 and 90: handles flat, a third of the way to each neighbour
    const Handles h = EasyEase({-40, 12, 40, -7}, 30, 0L, 90L, EaseSide::kBoth);
    QCOMPARE(h.pre_x, -10.0);
    QCOMPARE(h.pre_y, 0.0);
    QCOMPARE(h.post_x, 20.0);
    QCOMPARE(h.post_y, 0.0);
  }

  void easyEaseFirstAndLastKeys() {
    // No neighbour on a side: that x is kept, the handle still flattened
    const Handles first = EasyEase({-40, 5, 40, 5}, 0, std::nullopt, 60L, EaseSide::kBoth);
    QCOMPARE(first.pre_x, -40.0);
    QCOMPARE(first.pre_y, 0.0);
    QCOMPARE(first.post_x, 20.0);
    const Handles last = EasyEase({-40, 5, 40, 5}, 60, 0L, std::nullopt, EaseSide::kBoth);
    QCOMPARE(last.pre_x, -20.0);
    QCOMPARE(last.post_x, 40.0);
    QCOMPARE(last.post_y, 0.0);
  }

  void easyEaseInAndOutOnly() {
    const Handles in = EasyEase({-40, 5, 40, 6}, 30, 0L, 90L, EaseSide::kIn);
    QCOMPARE(in.pre_x, -10.0);
    QCOMPARE(in.pre_y, 0.0);
    QCOMPARE(in.post_x, 40.0);  // the Out side is untouched
    QCOMPARE(in.post_y, 6.0);
    const Handles out = EasyEase({-40, 5, 40, 6}, 30, 0L, 90L, EaseSide::kOut);
    QCOMPARE(out.pre_x, -40.0);
    QCOMPARE(out.pre_y, 5.0);
    QCOMPARE(out.post_x, 20.0);
    QCOMPARE(out.post_y, 0.0);
  }

  void onlyBezierKeysShowHandles() {
    // Painting and hit-testing share this test: a Linear or Hold key keeps default +-40 handles it never draws
    QVERIFY(keyframe_shows_handles(EFFECT_KEYFRAME_BEZIER));
    QVERIFY(!keyframe_shows_handles(EFFECT_KEYFRAME_LINEAR));
    QVERIFY(!keyframe_shows_handles(EFFECT_KEYFRAME_HOLD));
  }
};

QTEST_GUILESS_MAIN(TestGraphViewMath)
#include "test_graphviewmath.moc"
