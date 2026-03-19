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

#include "undo_timeline.h"

#include "global/global.h"
#include "project/footage.h"
#include "project/media.h"
#include "engine/clip.h"
#include "engine/sequence.h"

RippleAction::RippleAction(Sequence *is, long ipoint, long ilength, const QVector<int> &iignore) :
  s(is),
  point(ipoint),
  length(ilength),
  ignore(iignore)
{
}

void RippleAction::doUndo() {
  if (ca != nullptr) {
    ca->undo();
    delete ca;
    ca = nullptr;
  }
}

void RippleAction::doRedo() {
  if (!s) {
    qWarning() << "RippleAction::doRedo: sequence is null";
    return;
  }
  ca = new ComboAction();
  for (int i=0;i<s->clips.size();i++) {
    if (!ignore.contains(i)) {
      ClipPtr c = s->clips.at(i);
      if (c != nullptr) {
        if (c->timeline_in() >= point) {
          c->move(ca, length, length, 0, 0, true, true);
        }
      }
    }
  }
  ca->redo();
}

ChangeSequenceAction::ChangeSequenceAction(SequencePtr s) {
  new_sequence = std::move(s);
}

void ChangeSequenceAction::doUndo() {
  if (!amber::Global) {
    qWarning() << "ChangeSequenceAction::doUndo: Global is null";
    return;
  }
  amber::Global->set_sequence(old_sequence);
}

void ChangeSequenceAction::doRedo() {
  if (!amber::Global) {
    qWarning() << "ChangeSequenceAction::doRedo: Global is null";
    return;
  }
  old_sequence = amber::ActiveSequence;
  amber::Global->set_sequence(new_sequence);
}

SetTimelineInOutCommand::SetTimelineInOutCommand(Sequence* s, bool enabled, long in, long out) {
  seq = s;
  new_enabled = enabled;
  new_in = in;
  new_out = out;
}

void SetTimelineInOutCommand::doUndo() {
  if (!seq) {
    qWarning() << "SetTimelineInOutCommand::doUndo: seq is null";
    return;
  }
  seq->using_workarea = old_enabled;
  seq->workarea_in = old_in;
  seq->workarea_out = old_out;

  // footage viewer functions
  if (seq->wrapper_sequence && !seq->clips.isEmpty()
      && seq->clips.at(0) != nullptr && seq->clips.at(0)->media() != nullptr) {
    Footage* m = seq->clips.at(0)->media()->to_footage();
    if (m) {
      m->using_inout = old_enabled;
      m->in = old_in;
      m->out = old_out;
    }
  }
}

void SetTimelineInOutCommand::doRedo() {
  if (!seq) {
    qWarning() << "SetTimelineInOutCommand::doRedo: seq is null";
    return;
  }
  old_enabled = seq->using_workarea;
  old_in = seq->workarea_in;
  old_out = seq->workarea_out;

  seq->using_workarea = new_enabled;
  seq->workarea_in = new_in;
  seq->workarea_out = new_out;

  // footage viewer functions
  if (seq->wrapper_sequence && !seq->clips.isEmpty()
      && seq->clips.at(0) != nullptr && seq->clips.at(0)->media() != nullptr) {
    Footage* m = seq->clips.at(0)->media()->to_footage();
    if (m) {
      m->using_inout = new_enabled;
      m->in = new_in;
      m->out = new_out;
    }
  }
}

SetSelectionsCommand::SetSelectionsCommand(Sequence *s) {
  seq = s;
  done = true;
}

void SetSelectionsCommand::doUndo() {
  if (!seq) {
    qWarning() << "SetSelectionsCommand::doUndo: seq is null";
    return;
  }
  seq->selections = old_data;
  done = false;
}

void SetSelectionsCommand::doRedo() {
  if (!seq) {
    qWarning() << "SetSelectionsCommand::doRedo: seq is null";
    return;
  }
  if (!done) {
    seq->selections = new_data;
    done = true;
  }
}

EditSequenceCommand::EditSequenceCommand(Media* i, const SequencePtr& s) {
  item = i;
  seq = s;
  old_name = s->name;
  old_width = s->width;
  old_height = s->height;
  old_frame_rate = s->frame_rate;
  old_audio_frequency = s->audio_frequency;
  old_audio_layout = s->audio_layout;
}

void EditSequenceCommand::doUndo() {
  seq->name = old_name;
  seq->width = old_width;
  seq->height = old_height;
  seq->frame_rate = old_frame_rate;
  seq->audio_frequency = old_audio_frequency;
  seq->audio_layout = old_audio_layout;
  update();
}

void EditSequenceCommand::doRedo() {
  seq->name = name;
  seq->width = width;
  seq->height = height;
  seq->frame_rate = frame_rate;
  seq->audio_frequency = audio_frequency;
  seq->audio_layout = audio_layout;
  update();
}

void EditSequenceCommand::update() {
  if (!item) {
    qWarning() << "EditSequenceCommand::update: item is null";
    return;
  }
  // Update sequence's tooltip
  item->update_tooltip();

  for (const auto & clip : seq->clips) {
    if (clip != nullptr) {
      clip->refresh();
    }
  }

  if (amber::ActiveSequence == seq && amber::Global) {
    amber::Global->set_sequence(seq);
  }
}

AddMarkerAction::AddMarkerAction(QVector<Marker>* m, long t, QString n) {
  active_array = m;
  time = t;
  name = n;
}

void AddMarkerAction::doUndo() {
  if (!active_array) {
    qWarning() << "AddMarkerAction::doUndo: active_array is null";
    return;
  }
  if (index == -1) {
    active_array->removeLast();
  } else {
    (*active_array)[index].name = old_name;
  }
}

void AddMarkerAction::doRedo() {
  if (!active_array) {
    qWarning() << "AddMarkerAction::doRedo: active_array is null";
    return;
  }
  index = -1;

  for (int i=0;i<active_array->size();i++) {
    if (active_array->at(i).frame == time) {
      index = i;
      break;
    }
  }

  if (index == -1) {
    Marker m;
    m.frame = time;
    m.name = name;
    active_array->append(m);
  } else {
    old_name = active_array->at(index).name;
    (*active_array)[index].name = name;
  }
}

MoveMarkerAction::MoveMarkerAction(Marker* m, long o, long n) {
  marker = m;
  old_time = o;
  new_time = n;
}

void MoveMarkerAction::doUndo() {
  if (!marker) {
    qWarning() << "MoveMarkerAction::doUndo: marker is null";
    return;
  }
  marker->frame = old_time;
}

void MoveMarkerAction::doRedo() {
  if (!marker) {
    qWarning() << "MoveMarkerAction::doRedo: marker is null";
    return;
  }
  marker->frame = new_time;
}

DeleteMarkerAction::DeleteMarkerAction(QVector<Marker> *m) {
  active_array = m;
  sorted = false;
}

void DeleteMarkerAction::doUndo() {
  if (!active_array) {
    qWarning() << "DeleteMarkerAction::doUndo: active_array is null";
    return;
  }
  for (int i=markers.size()-1;i>=0;i--) {
    active_array->insert(markers.at(i), copies.at(i));
  }
}

void DeleteMarkerAction::doRedo() {
  if (!active_array) {
    qWarning() << "DeleteMarkerAction::doRedo: active_array is null";
    return;
  }
  for (int i=0;i<markers.size();i++) {
    // correct future removals
    if (!sorted) {
      copies.append(active_array->at(markers.at(i)));
      for (int j=i+1;j<markers.size();j++) {
        if (markers.at(j) > markers.at(i)) {
          markers[j]--;
        }
      }
    }
    active_array->removeAt(markers.at(i));
  }
  sorted = true;
}
