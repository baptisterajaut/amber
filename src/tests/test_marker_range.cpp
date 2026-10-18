#include <QVector>
#include <QtTest>

#include "timeline/marker_range.h"

namespace {
Marker marker(long frame, long duration) {
  Marker m;
  m.frame = frame;
  m.duration = duration;
  return m;
}

// 2 px per frame, scrolled 10 px: frame 0 sits at x = -10
int to_screen(long frame) { return int(frame * 2) - 10; }
}  // namespace

class TestMarkerRange : public QObject {
  Q_OBJECT
 private slots:
  void endTickHitWithinThreePixels() {
    using amber::marker_range::end_tick_at;
    const QVector<Marker> markers = {marker(10, 20)};  // ends at frame 30, x = 50
    QCOMPARE(end_tick_at(markers, 50, to_screen), 0);
    QCOMPARE(end_tick_at(markers, 47, to_screen), 0);
    QCOMPARE(end_tick_at(markers, 53, to_screen), 0);
    QCOMPARE(end_tick_at(markers, 46, to_screen), -1);
    QCOMPARE(end_tick_at(markers, 54, to_screen), -1);
  }
  void pointMarkerHasNoEndTick() {
    using amber::marker_range::end_tick_at;
    const QVector<Marker> markers = {marker(30, 0)};  // head at x = 50
    QCOMPARE(end_tick_at(markers, 50, to_screen), -1);
  }
  void endTickFindsTheRightMarker() {
    using amber::marker_range::end_tick_at;
    const QVector<Marker> markers = {marker(0, 0), marker(5, 5), marker(40, 60)};  // ends at x = 10 and x = 190
    QCOMPARE(end_tick_at(markers, 190, to_screen), 2);
    QCOMPARE(end_tick_at(markers, 11, to_screen), 1);
    QCOMPARE(end_tick_at(markers, 100, to_screen), -1);
  }
  void durationFollowsTheEnd() {
    QCOMPARE(amber::marker_range::duration_to(10, 55), 45L);
    QCOMPARE(amber::marker_range::duration_to(10, 11), 1L);
  }
  void durationClampsAtZero() {
    QCOMPARE(amber::marker_range::duration_to(10, 10), 0L);
    QCOMPARE(amber::marker_range::duration_to(10, 3), 0L);
  }
  // Alt-drag from a point marker's head: the end starts on the head (duration 0) and follows the mouse
  void altDragFromPointHead() {
    const long frame = 30, original_duration = 0, press_frame = 30;
    auto drag_to = [&](long mouse_frame) {
      return amber::marker_range::duration_to(frame, frame + original_duration + (mouse_frame - press_frame));
    };
    QCOMPARE(drag_to(30), 0L);
    QCOMPARE(drag_to(42), 12L);
    QCOMPARE(drag_to(20), 0L);
  }
};

QTEST_GUILESS_MAIN(TestMarkerRange)
#include "test_marker_range.moc"
