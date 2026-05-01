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
}
#endif
