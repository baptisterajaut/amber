#ifndef TRACKSTATE_H
#define TRACKSTATE_H

#include <QString>

// Per-track header state of a sequence (Sequence::track_states). Tracks are implicit ints: V1 = -1, V2 = -2...,
// A1 = 0, A2 = 1... hidden applies to video tracks only, muted, solo, volume and pan to audio tracks only, locked to
// both; a value that doesn't apply to a track's kind is never set by the UI and is ignored by the Sequence getters.
// CBA: no track volume keyframes, mixer panel or typed values; add if asked
struct TrackState {
  bool hidden{false};
  bool muted{false};
  bool solo{false};
  bool locked{false};
  // [kTrackVolumeMinDb, kTrackVolumeMaxDb], the minimum is silence. The header rounds it to 0.1 dB, so exact
  // comparisons are stable.
  double volume_db{0.0};
  // [-100, 100], negative is left
  int pan{0};

  bool is_default() const { return !hidden && !muted && !solo && !locked && volume_db == 0.0 && pan == 0; }
  bool operator==(const TrackState& o) const {
    return hidden == o.hidden && muted == o.muted && solo == o.solo && locked == o.locked && volume_db == o.volume_db &&
           pan == o.pan;
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

// Track volume range in dB; the minimum means silence ("-inf dB")
constexpr double kTrackVolumeMinDb = -60.0;
constexpr double kTrackVolumeMaxDb = 12.0;
constexpr int kTrackPanLimit = 100;

// "V1", "V2"... for video tracks, "A1", "A2"... for audio tracks
inline QString track_label(int track) {
  return (track < 0) ? QStringLiteral("V%1").arg(-track) : QStringLiteral("A%1").arg(track + 1);
}
}  // namespace amber

#endif  // TRACKSTATE_H
