/***

    Olive - Non-Linear Video Editor
    Copyright (C) 2019  Olive Team

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.

***/

#ifndef SEQUENCE_H
#define SEQUENCE_H

#include <QMap>
#include <QVector>

#include <optional>

#include "sequence_fwd.h"

#include "clip.h"
#include "core/guide.h"
#include "core/marker.h"
#include "core/selection.h"
#include "trackstate.h"

class Sequence {
 public:
  Sequence();
  ~Sequence();
  SequencePtr copy();
  QString name;
  void getTrackLimits(int* video_tracks, int* audio_tracks);
  long getEndFrame();
  int width;
  int height;
  double frame_rate;
  int audio_frequency;
  int audio_layout;

  void RefreshClips(Media* m = nullptr);
  QVector<Clip*> SelectedClips(bool containing = true);
  QVector<int> SelectedClipIndexes();

  Effect* GetSelectedGizmo();

  bool IsClipSelected(int clip_index, bool containing = true);
  bool IsClipSelected(Clip* clip, bool containing = true);
  bool IsTransitionSelected(Transition* t);

  QVector<Selection> selections;
  long playhead{0};

  bool using_workarea{false};
  long workarea_in{0};
  long workarea_out{0};

  bool wrapper_sequence{false};

  int save_id;

  QVector<Marker> markers;
  QVector<Guide> guides;
  QVector<ClipPtr> clips;

  // Track header toggles, keyed by track index. Only non-default states are stored: a missing key is the default.
  QMap<int, TrackState> track_states;
  TrackState track_state(int track) const;
  // Erases the key when the state is the default
  void set_track_state(int track, const TrackState& state);
  // Not muted, and soloed if any audio track of this sequence is soloed
  bool track_audible(int track) const;
  // A video track that is not hidden
  bool track_visible_in_render(int track) const;
  bool track_locked(int track) const;
  // The first of these tracks that is locked, if any
  std::optional<int> first_locked_track(const QVector<int>& tracks) const;
  // Indexes of the clips on locked tracks (the ripple ignore list)
  QVector<int> locked_clip_indexes() const;
  // Drops the selections on locked tracks
  void remove_locked_selections();
};

#endif  // SEQUENCE_H
