#include <QtTest>

#include <cmath>

#include "core/graphmath.h"

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
};

QTEST_GUILESS_MAIN(TestGraphViewMath)
#include "test_graphviewmath.moc"
