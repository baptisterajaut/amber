#include "timeline_layout.h"
#include <climits>

namespace amber::timeline_layout {

int seam_y(const TrackHeights& h) {
  int s = 0;
  for (int v : h.video) s += v;
  return s;
}

int track_top_y(const TrackHeights& h, int track) {
  const int seam = seam_y(h);
  if (track < 0) {
    int y = seam;
    for (int i = -1; i >= track; --i) {
      const int idx = -i - 1;  // -1 -> 0, -2 -> 1, ...
      if (idx >= h.video.size()) break;
      y -= h.video[idx];
    }
    return y;
  }
  int y = seam;
  for (int i = 0; i < track; ++i) {
    if (i >= h.audio.size()) break;
    y += h.audio[i];
  }
  return y;
}

int track_at_y(const TrackHeights& h, int y) {
  const int seam = seam_y(h);
  if (y < 0) return INT_MIN;
  if (y < seam) {
    int acc = 0;
    for (int i = 0; i < h.video.size(); ++i) {
      acc += h.video[i];
      if (seam - acc <= y) return -(i + 1);
    }
    return INT_MIN;
  }
  int acc = seam;
  for (int i = 0; i < h.audio.size(); ++i) {
    acc += h.audio[i];
    if (y < acc) return i;
  }
  return INT_MIN;
}

static bool same_side(int a, int b) { return (a < 0) == (b < 0); }

int ghost_track(int old_track, int drag_track_start, int track_diff) {
  return same_side(old_track, drag_track_start) ? old_track + track_diff : old_track - track_diff;
}

int clamp_track_diff(const QVector<int>& old_tracks, int drag_track_start, int track_diff) {
  // Every seam limit points the same way: dragging from video, they are all upper bounds on track_diff (a video
  // ghost can't go below V1, a mirrored audio ghost can't go above A1); dragging from audio, all lower bounds. 0 meets
  // them all and a violation only happens on the far side of 0, so stepping back toward 0 always ends.
  const int step = (drag_track_start < 0) ? -1 : 1;
  for (int old_track : old_tracks) {
    while (!same_side(old_track, ghost_track(old_track, drag_track_start, track_diff))) track_diff += step;
  }
  return track_diff;
}

}  // namespace amber::timeline_layout
