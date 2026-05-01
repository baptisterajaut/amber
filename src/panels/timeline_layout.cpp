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

}  // namespace amber::timeline_layout
