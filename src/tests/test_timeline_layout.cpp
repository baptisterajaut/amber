#include <QtTest>
#include <QVector>

namespace amber::timeline_layout {
struct TrackHeights {
  // negative-indexed video heights: video[0] = height(-1), video[1] = height(-2), ...
  QVector<int> video;
  // non-negative-indexed audio: audio[0] = height(0), audio[1] = height(1), ...
  QVector<int> audio;
};
int seam_y(const TrackHeights& h);
int track_top_y(const TrackHeights& h, int track);
int track_at_y(const TrackHeights& h, int y);  // returns INT_MIN if out of range
}

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
};

QTEST_MAIN(TestTimelineLayout)
#include "test_timeline_layout.moc"
