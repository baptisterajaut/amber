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
