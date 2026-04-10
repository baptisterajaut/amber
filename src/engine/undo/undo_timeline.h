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

class RippleAction : public AmberAction {
public:
  RippleAction(Sequence* is, long ipoint, long ilength, const QVector<int>& iignore);
  void doUndo() override;
  void doRedo() override;
private:
  Sequence* s;
  long point;
  long length;
  QVector<int> ignore;
  ComboAction* ca = nullptr;
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
  MoveMarkerAction(Marker* m, long o, long n);
  void doUndo() override;
  void doRedo() override;
private:
  Marker* marker;
  long old_time;
  long new_time;
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
