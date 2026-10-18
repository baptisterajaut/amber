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

#ifndef UNDO_TIMELINE_H
#define UNDO_TIMELINE_H

#include "undoactions.h"

#include <QMap>

#include "engine/trackstate.h"

class RippleAction : public AmberAction {
public:
  RippleAction(Sequence* is, long ipoint, long ilength, const QVector<int>& iignore);
  ~RippleAction() override;
  void doUndo() override;
  void doRedo() override;
private:
  Sequence* s;
  long point;
  long length;
  QVector<int> ignore;
  ComboAction* ca = nullptr;
};

// Swaps a sequence's whole track_states map: one entry covers a Shift+click applied to every track of a kind
class SetTrackStateCommand : public AmberAction {
public:
  SetTrackStateCommand(Sequence* s, QMap<int, TrackState> old_states, QMap<int, TrackState> new_states);
  void doUndo() override;
  void doRedo() override;
private:
  void apply(const QMap<int, TrackState>& states);
  Sequence* seq;
  QMap<int, TrackState> old_states_;
  QMap<int, TrackState> new_states_;
};

class ChangeSequenceAction : public AmberAction {
public:
  explicit ChangeSequenceAction(SequencePtr s);
  void doUndo() override;
  void doRedo() override;
private:
  SequencePtr old_sequence;
  SequencePtr new_sequence;
};

class SetTimelineInOutCommand : public AmberAction {
public:
  SetTimelineInOutCommand(Sequence* s, bool enabled, long in, long out);
  void doUndo() override;
  void doRedo() override;
private:
  Sequence* seq;

  bool old_enabled;
  long old_in;
  long old_out;

  bool new_enabled;
  long new_in;
  long new_out;
};

class SetSelectionsCommand : public AmberAction {
public:
  explicit SetSelectionsCommand(Sequence* s);
  void doUndo() override;
  void doRedo() override;
  QVector<Selection> old_data;
  QVector<Selection> new_data;
private:
  Sequence* seq;
  bool done;
};

class EditSequenceCommand : public AmberAction {
public:
  EditSequenceCommand(Media *i, const SequencePtr& s);
  void doUndo() override;
  void doRedo() override;
  void update();

  QString name;
  int width;
  int height;
  double frame_rate;
  int audio_frequency;
  int audio_layout;
private:
  Media* item;
  SequencePtr seq;

  QString old_name;
  int old_width;
  int old_height;
  double old_frame_rate;
  int old_audio_frequency;
  int old_audio_layout;
};

class AddMarkerAction : public AmberAction {
public:
  AddMarkerAction(QVector<Marker>* m, long t, QString n);
  void doUndo() override;
  void doRedo() override;
private:
  QVector<Marker>* active_array;
  long time;
  QString name;
  QString old_name;
  int index;
};

class MoveMarkerAction : public AmberAction {
public:
  MoveMarkerAction(QVector<Marker>* m, int i, long o, long n);
  void doUndo() override;
  void doRedo() override;
private:
  QVector<Marker>* active_array;
  int index;
  long old_time;
  long new_time;
};

class ResizeMarkerAction : public AmberAction {
public:
  ResizeMarkerAction(QVector<Marker>* m, int i, long old_duration, long new_duration);
  void doUndo() override;
  void doRedo() override;
private:
  QVector<Marker>* active_array;
  int index;
  long old_duration;
  long new_duration;
};

// Marker Properties: replaces every field of one marker. Index-addressed like MoveMarkerAction, so it
// survives the array reallocating between redo and undo (an appended marker, later undone).
class EditMarkerAction : public AmberAction {
public:
  EditMarkerAction(QVector<Marker>* m, int i, const Marker& old_marker, const Marker& new_marker);
  void doUndo() override;
  void doRedo() override;
private:
  QVector<Marker>* active_array;
  int index;
  Marker old_marker;
  Marker new_marker;
};

class DeleteMarkerAction : public AmberAction {
public:
  explicit DeleteMarkerAction(QVector<Marker>* m);
  void doUndo() override;
  void doRedo() override;
  QVector<int> markers;
private:
  QVector<Marker>* active_array;
  QVector<Marker> copies;
  bool sorted;
};

#endif // UNDO_TIMELINE_H
