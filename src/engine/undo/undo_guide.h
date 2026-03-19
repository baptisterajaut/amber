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

#ifndef UNDO_GUIDE_H
#define UNDO_GUIDE_H

#include "core/guide.h"
#include "undoactions.h"

class Sequence;

class AddGuideAction : public OliveAction {
 public:
  AddGuideAction(Sequence* seq, const Guide& guide);
  void doUndo() override;
  void doRedo() override;

 private:
  Sequence* seq_;
  Guide guide_;
};

class DeleteGuideAction : public OliveAction {
 public:
  DeleteGuideAction(Sequence* seq, int index);
  void doUndo() override;
  void doRedo() override;

 private:
  Sequence* seq_;
  int index_;
  Guide guide_;
};

class MoveGuideAction : public OliveAction {
 public:
  MoveGuideAction(Sequence* seq, int index, int old_position, int new_position);
  void doUndo() override;
  void doRedo() override;

 private:
  Sequence* seq_;
  int index_;
  int old_position_;
  int new_position_;
};

class SetGuideMirrorAction : public OliveAction {
 public:
  SetGuideMirrorAction(Sequence* seq, int index, bool mirror);
  void doUndo() override;
  void doRedo() override;

 private:
  Sequence* seq_;
  int index_;
  bool mirror_;
};

#endif  // UNDO_GUIDE_H
