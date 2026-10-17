#include <QtTest>
#include <QVector>

#include "panels/timeline_layout.h"

namespace {
// The tracks each ghost lands on, the way update_ghosts() does it: clamp once, then move every ghost.
QVector<int> drag(const QVector<int>& old_tracks, int drag_track_start, int track_diff) {
  using namespace amber::timeline_layout;
  const int diff = clamp_track_diff(old_tracks, drag_track_start, track_diff);
  QVector<int> tracks;
  for (int t : old_tracks) tracks.append(ghost_track(t, drag_track_start, diff));
  return tracks;
}
}  // namespace

class TestTimelineLayout : public QObject {
  Q_OBJECT
 private slots:
  void testSeamWithThreeVideoTwoAudio() {
    amber::timeline_layout::TrackHeights h{{40, 40, 40}, {40, 40}};
    QCOMPARE(amber::timeline_layout::seam_y(h), 120);
  }
  void testTrackTopYVideo() {
    amber::timeline_layout::TrackHeights h{{40, 40, 40}, {40, 40}};
    QCOMPARE(amber::timeline_layout::track_top_y(h, -1), 80);
    QCOMPARE(amber::timeline_layout::track_top_y(h, -3), 0);
  }
  void testTrackTopYAudio() {
    amber::timeline_layout::TrackHeights h{{40, 40, 40}, {40, 40}};
    QCOMPARE(amber::timeline_layout::track_top_y(h, 0), 120);
    QCOMPARE(amber::timeline_layout::track_top_y(h, 1), 160);
  }
  void testTrackAtYRoundtrips() {
    amber::timeline_layout::TrackHeights h{{40, 40, 40}, {40, 40}};
    for (int t : {-3, -2, -1, 0, 1}) {
      int y = amber::timeline_layout::track_top_y(h, t) + 1;  // mid-row
      QCOMPARE(amber::timeline_layout::track_at_y(h, y), t);
    }
  }

  // Track numbers: V1 = -1, V2 = -2, A1 = 0, A2 = 1.
  void testLinkedPairDraggedUpFromVideo() {
    // V1+A1, video dragged to V2: the audio goes down to A2
    QCOMPARE(drag({-1, 0}, -1, -1), QVector<int>({-2, 1}));
  }
  void testLinkedPairDraggedDownFromAudio() {
    // A1+V1, audio dragged to A2: the video goes up to V2
    QCOMPARE(drag({0, -1}, 0, +1), QVector<int>({1, -2}));
  }
  void testLinkedPairTowardSeam() {
    // V2+A2, video dragged to V1: the audio comes back to A1
    QCOMPARE(drag({-2, 1}, -2, +1), QVector<int>({-1, 0}));
  }
  void testOtherSideBlocksAtSeam() {
    // V2+A1, video dragged to V1: the audio can't go above A1, so the pair doesn't move vertically
    QCOMPARE(amber::timeline_layout::clamp_track_diff({-2, 0}, -2, +1), 0);
    QCOMPARE(drag({-2, 0}, -2, +1), QVector<int>({-2, 0}));
    // same from the audio side: A2+V1, audio dragged to A1
    QCOMPARE(amber::timeline_layout::clamp_track_diff({1, -1}, 1, -1), 0);
  }
  void testOtherSideClampsPartially() {
    // V3+A2, video dragged down 2 tracks (would put the audio above A1): stops after 1
    QCOMPARE(amber::timeline_layout::clamp_track_diff({-3, 1}, -3, +2), 1);
    QCOMPARE(drag({-3, 1}, -3, +2), QVector<int>({-2, 0}));
  }
  void testSameSideSeamClamp() {
    // regression of the existing clamp: V1+V2 dragged from V2 by +2 can't move, V1 would cross into audio
    QCOMPARE(amber::timeline_layout::clamp_track_diff({-1, -2}, -2, +2), 0);
    // A1+A2 dragged from A2 by -2
    QCOMPARE(amber::timeline_layout::clamp_track_diff({0, 1}, 1, -2), 0);
  }
  void testDraggedSideStillCrossesNothing() {
    // V1 alone dragged down by 1 stays on V1, A1 alone dragged up by 1 stays on A1
    QCOMPARE(amber::timeline_layout::clamp_track_diff({-1}, -1, +1), 0);
    QCOMPARE(amber::timeline_layout::clamp_track_diff({0}, 0, -1), 0);
    // moving away from the seam is never clamped
    QCOMPARE(amber::timeline_layout::clamp_track_diff({-1, 0}, -1, -5), -5);
    QCOMPARE(amber::timeline_layout::clamp_track_diff({-1, 0}, 0, +5), 5);
  }
  void testUnlinkedOtherSideGhostFollowsMirror() {
    // V1 plus an unrelated A3, video dragged to V2: the A3 clip moves down to A4
    QCOMPARE(drag({-1, 2}, -1, -1), QVector<int>({-2, 3}));
  }
  void testNoVerticalMoveKeepsTracks() {
    // slide tool and transition drags pass track_diff = 0
    QCOMPARE(drag({-2, -1, 0, 3}, -1, 0), QVector<int>({-2, -1, 0, 3}));
  }
};

QTEST_MAIN(TestTimelineLayout)
#include "test_timeline_layout.moc"
