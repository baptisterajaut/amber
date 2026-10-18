#ifndef TRACKSTATE_H
#define TRACKSTATE_H

#include <QString>

// Per-track header toggles of a sequence (Sequence::track_states). Tracks are implicit ints: V1 = -1, V2 = -2...,
// A1 = 0, A2 = 1... hidden applies to video tracks only, muted and solo to audio tracks only, locked to both; a flag
// that doesn't apply to a track's kind is never set by the UI and is ignored by the Sequence getters.
struct TrackState {
  bool hidden{false};
  bool muted{false};
  bool solo{false};
  bool locked{false};

  bool is_default() const { return !hidden && !muted && !solo && !locked; }
  bool operator==(const TrackState& o) const {
    return hidden == o.hidden && muted == o.muted && solo == o.solo && locked == o.locked;
  }
  bool operator!=(const TrackState& o) const { return !(*this == o); }
};

namespace amber {
// Track indexes a state is kept for: |index| < 100, as the subtitle import's track search. A corrupt .ove can't make
// the header and the timeline walk billions of tracks.
constexpr int kTrackStateIndexLimit = 100;
inline bool track_state_index_valid(int track) {
  return track > -kTrackStateIndexLimit && track < kTrackStateIndexLimit;
}

// "V1", "V2"... for video tracks, "A1", "A2"... for audio tracks
inline QString track_label(int track) {
  return (track < 0) ? QStringLiteral("V%1").arg(-track) : QStringLiteral("A%1").arg(track + 1);
}
}  // namespace amber

#endif  // TRACKSTATE_H
