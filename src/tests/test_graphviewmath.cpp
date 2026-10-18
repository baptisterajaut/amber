#include <QtTest>

#include <cmath>

#include "core/graphmath.h"
#include "effects/effect.h"

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

  void dragFromOutsideTheRangeIsNotPulledIn() {
    // Transform scale (min 0) at -50 from a viewer gizmo: a time-only drag keeps -50, moving further out is refused
    QCOMPARE(ClampDragValue(-50.0, -50.0, 0.0, qSNaN()), -50.0);
    QCOMPARE(ClampDragValue(-40.0, -50.0, 0.0, qSNaN()), -40.0);
    QCOMPARE(ClampDragValue(-60.0, -50.0, 0.0, qSNaN()), -50.0);
    QCOMPARE(ClampDragValue(130.0, 120.0, 0.0, 100.0), 120.0);
    // Started inside: the plain range; no start: plain clamp; NaN bounds stay unbounded
    QCOMPARE(ClampDragValue(-5.0, 10.0, 0.0, 100.0), 0.0);
    QCOMPARE(ClampDragValue(-5.0, qQNaN(), 0.0, 100.0), 0.0);
    QCOMPARE(ClampDragValue(-500.0, -50.0, qSNaN(), qSNaN()), -500.0);
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

  void onlyBezierKeysShowHandles() {
    // Painting and hit-testing share this test: a Linear or Hold key keeps default +-40 handles it never draws
    QVERIFY(keyframe_shows_handles(EFFECT_KEYFRAME_BEZIER));
    QVERIFY(!keyframe_shows_handles(EFFECT_KEYFRAME_LINEAR));
    QVERIFY(!keyframe_shows_handles(EFFECT_KEYFRAME_HOLD));
  }
};

QTEST_GUILESS_MAIN(TestGraphViewMath)
#include "test_graphviewmath.moc"
