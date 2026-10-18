#include <QVector>
#include <QtTest>

#include <memory>

#include "engine/undo/undo_timeline.h"
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
  // A range marker a few pixels long: its end tick would cover the head, so a press there moves the marker
  void shortRangeMarkerKeepsItsHead() {
    using amber::marker_range::end_tick_at;
    const QVector<Marker> short_marker = {marker(30, 3)};  // head x = 50, end x = 56
    QCOMPARE(end_tick_at(short_marker, 53, to_screen), -1);
    QCOMPARE(end_tick_at(short_marker, 56, to_screen), -1);
    const QVector<Marker> long_enough = {marker(30, 4)};  // end x = 58: 8 px, clear of the head's band (47..53)
    QCOMPARE(end_tick_at(long_enough, 58, to_screen), 0);
    QCOMPARE(end_tick_at(long_enough, 55, to_screen), 0);
    QCOMPARE(end_tick_at(long_enough, 53, to_screen), -1);
  }
  void durationFollowsTheEnd() {
    QCOMPARE(amber::marker_range::duration_to(10, 55), 45L);
    QCOMPARE(amber::marker_range::duration_to(10, 11), 1L);
  }
  void durationClampsAtZero() {
    QCOMPARE(amber::marker_range::duration_to(10, 10), 0L);
    QCOMPARE(amber::marker_range::duration_to(10, 3), 0L);
  }
  // What TimelineHeader::mousePressEvent grabs (press_hit). Marker offset 0: presses at y <= 0 are on the timecode.
  void pressEndTickWinsOverHeads() {
    using amber::marker_range::press_hit;
    // range 0 ends at frame 30 (x = 50), where point marker 1's head is too
    const QVector<Marker> markers = {marker(10, 20), marker(30, 0)};
    QVector<int> selected;
    auto hit = press_hit(selected, markers, 50, 5, 0, -100, false, false, to_screen);
    QCOMPARE(hit.end, 0);
    QCOMPARE(hit.head, -1);
    QCOMPARE(selected, QVector<int>{0});
    hit = press_hit(selected, markers, 10, 5, 0, -100, false, false, to_screen);  // range 0's head (x = 10)
    QCOMPARE(hit.end, -1);
    QCOMPARE(hit.head, 0);
    hit = press_hit(selected, markers, 50, 0, 0, -100, false, false, to_screen);  // on the timecode band
    QCOMPARE(hit.end, -1);
    QCOMPARE(hit.head, -1);
  }
  void pressAltGrabsOnlyAPointMarkersEnd() {
    using amber::marker_range::press_hit;
    const QVector<Marker> markers = {marker(10, 20), marker(40, 0)};  // heads at x = 10 and x = 70
    QVector<int> selected = {0};
    auto hit = press_hit(selected, markers, 70, 5, 0, -100, true, true, to_screen);  // Shift+Alt on the point head
    QCOMPARE(hit.end, 1);
    QCOMPARE(selected, QVector<int>{1});  // the resized marker alone, whatever shift did
    hit = press_hit(selected, markers, 70, 5, 0, -100, false, false, to_screen);
    QCOMPARE(hit.end, -1);
    QCOMPARE(hit.head, 1);
    hit = press_hit(selected, markers, 10, 5, 0, -100, false, true, to_screen);  // Alt on a range head: a move
    QCOMPARE(hit.end, -1);
    QCOMPARE(hit.head, 0);
  }
  void pressHeadSelectionAndPlayhead() {
    using amber::marker_range::press_hit;
    const QVector<Marker> markers = {marker(10, 0), marker(40, 0)};  // heads at x = 10 and x = 70
    QVector<int> selected;
    QCOMPARE(press_hit(selected, markers, 10, 5, 0, -100, false, false, to_screen).head, 0);
    QCOMPARE(press_hit(selected, markers, 70, 5, 0, -100, true, false, to_screen).head, 1);
    QCOMPARE(selected, (QVector<int>{0, 1}));
    QCOMPARE(press_hit(selected, markers, 10, 5, 0, -100, true, false, to_screen).head, 0);  // shift toggles off
    QCOMPARE(selected, QVector<int>{1});
    // the playhead's handle sits over head 1: the press goes to the playhead
    QCOMPARE(press_hit(selected, markers, 70, 5, 0, 72, false, false, to_screen).head, -1);
  }
  // Alt-drag from a point marker's head: the end starts on the head (duration 0) and follows the mouse; the release
  // pushes one Resize Marker step, none when the duration came back to where it was
  void altDragFromPointHead() {
    using namespace amber::marker_range;
    QVector<Marker> markers = {marker(30, 0)};  // head at x = 50
    QVector<int> selected;
    const PressHit hit = press_hit(selected, markers, 50, 5, 0, -100, false, true, to_screen);
    QCOMPARE(hit.end, 0);
    const long press_frame = 30, original = markers.at(0).duration;
    auto drag_to = [&](long mouse_frame) {
      markers[0].duration =
          duration_to(markers.at(0).frame, dragged_end_frame(markers.at(0).frame, original, press_frame, mouse_frame));
      return markers.at(0).duration;
    };
    QCOMPARE(drag_to(20), 0L);
    QCOMPARE(drag_to(42), 12L);

    std::unique_ptr<ResizeMarkerAction> step(resize_release_action(&markers, hit.end, original));
    QVERIFY(step != nullptr);
    step->undo();
    QCOMPARE(markers.at(0).duration, 0L);
    step->redo();
    QCOMPARE(markers.at(0).duration, 12L);

    drag_to(30);
    QVERIFY(resize_release_action(&markers, hit.end, original) == nullptr);  // back to a point marker: no step
    QVERIFY(resize_release_action(&markers, 1, original) == nullptr);        // stale index
  }
  // Resizing an existing range: the end moves by what the mouse travelled, not to the mouse
  void dragEndKeepsThePressOffset() {
    using amber::marker_range::dragged_end_frame;
    QCOMPARE(dragged_end_frame(10, 20, 29, 35), 36L);  // grabbed one frame left of the end at 30
    QCOMPARE(dragged_end_frame(10, 20, 29, 29), 30L);
  }
};

QTEST_GUILESS_MAIN(TestMarkerRange)
#include "test_marker_range.moc"
