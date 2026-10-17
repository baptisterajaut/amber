#ifndef TIMELINE_LAYOUT_H
#define TIMELINE_LAYOUT_H
#include <QVector>
namespace amber::timeline_layout {
struct TrackHeights {
  QVector<int> video;  // video[0] = height(-1), video[1] = height(-2), ...
  QVector<int> audio;  // audio[0] = height(0), audio[1] = height(1), ...
};
int seam_y(const TrackHeights& h);
int track_top_y(const TrackHeights& h, int track);
int track_at_y(const TrackHeights& h, int y);

// Move drag track arithmetic. Video tracks are negative (V1 = -1, V2 = -2), audio tracks start at 0 (A1 = 0, A2 = 1).
// A ghost on the dragged side (same sign as drag_track_start) moves by track_diff, a ghost on the other side by the
// mirrored amount, so a linked V+A pair moves away from (or toward) the V/A seam together.
int ghost_track(int old_track, int drag_track_start, int track_diff);
// Shrinks track_diff until no ghost, on either side, would cross the V/A seam. Keeps the ghosts' relative layout:
// a ghost that can't move blocks the vertical move of all of them.
int clamp_track_diff(const QVector<int>& old_tracks, int drag_track_start, int track_diff);
}
#endif
