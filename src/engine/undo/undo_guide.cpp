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

#include "undo_guide.h"

#include "engine/sequence.h"

AddGuideAction::AddGuideAction(Sequence* seq, const Guide& guide) : seq_(seq), guide_(guide) {}

void AddGuideAction::doUndo() {
  if (!seq_) { qWarning() << "AddGuideAction::doUndo: seq is null"; return; }
  seq_->guides.removeLast();
}

void AddGuideAction::doRedo() {
  if (!seq_) { qWarning() << "AddGuideAction::doRedo: seq is null"; return; }
  seq_->guides.append(guide_);
}

DeleteGuideAction::DeleteGuideAction(Sequence* seq, int index)
    : seq_(seq), index_(index), guide_(seq->guides.at(index)) {}

void DeleteGuideAction::doUndo() {
  if (!seq_) { qWarning() << "DeleteGuideAction::doUndo: seq is null"; return; }
  seq_->guides.insert(index_, guide_);
}

void DeleteGuideAction::doRedo() {
  if (!seq_) { qWarning() << "DeleteGuideAction::doRedo: seq is null"; return; }
  seq_->guides.removeAt(index_);
}

MoveGuideAction::MoveGuideAction(Sequence* seq, int index, int old_position, int new_position)
    : seq_(seq), index_(index), old_position_(old_position), new_position_(new_position) {}

void MoveGuideAction::doUndo() {
  if (!seq_) { qWarning() << "MoveGuideAction::doUndo: seq is null"; return; }
  seq_->guides[index_].position = old_position_;
}

void MoveGuideAction::doRedo() {
  if (!seq_) { qWarning() << "MoveGuideAction::doRedo: seq is null"; return; }
  seq_->guides[index_].position = new_position_;
}

SetGuideMirrorAction::SetGuideMirrorAction(Sequence* seq, int index, bool mirror)
    : seq_(seq), index_(index), mirror_(mirror) {}

void SetGuideMirrorAction::doUndo() {
  if (!seq_) { qWarning() << "SetGuideMirrorAction::doUndo: seq is null"; return; }
  seq_->guides[index_].mirror = !mirror_;
}

void SetGuideMirrorAction::doRedo() {
  if (!seq_) { qWarning() << "SetGuideMirrorAction::doRedo: seq is null"; return; }
  seq_->guides[index_].mirror = mirror_;
}
