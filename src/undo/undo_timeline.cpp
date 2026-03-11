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
#include "timeline/clip.h"
#include "timeline/sequence.h"

RippleAction::RippleAction(Sequence *is, long ipoint, long ilength, const QVector<int> &iignore) :
  s(is),
  point(ipoint),
  length(ilength),
  ignore(iignore)
{
}

void RippleAction::doUndo() {
  ca->undo();
  delete ca;
}

void RippleAction::doRedo() {
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
  new_sequence = s;
}

void ChangeSequenceAction::doUndo() {
  olive::Global->set_sequence(old_sequence);
}

void ChangeSequenceAction::doRedo() {
  old_sequence = olive::ActiveSequence;
  olive::Global->set_sequence(new_sequence);
}

SetTimelineInOutCommand::SetTimelineInOutCommand(Sequence* s, bool enabled, long in, long out) {
  seq = s;
  new_enabled = enabled;
  new_in = in;
  new_out = out;
}

void SetTimelineInOutCommand::doUndo() {
  seq->using_workarea = old_enabled;
  seq->workarea_in = old_in;
  seq->workarea_out = old_out;

  // footage viewer functions
  if (seq->wrapper_sequence) {
    Footage* m = seq->clips.at(0)->media()->to_footage();
    m->using_inout = old_enabled;
    m->in = old_in;
    m->out = old_out;
  }
}

void SetTimelineInOutCommand::doRedo() {
  old_enabled = seq->using_workarea;
  old_in = seq->workarea_in;
  old_out = seq->workarea_out;

  seq->using_workarea = new_enabled;
  seq->workarea_in = new_in;
  seq->workarea_out = new_out;

  // footage viewer functions
  if (seq->wrapper_sequence) {
    Footage* m = seq->clips.at(0)->media()->to_footage();
    m->using_inout = new_enabled;
    m->in = new_in;
    m->out = new_out;
  }
}

SetSelectionsCommand::SetSelectionsCommand(Sequence *s) {
  seq = s;
  done = true;
}

void SetSelectionsCommand::doUndo() {
  seq->selections = old_data;
  done = false;
}

void SetSelectionsCommand::doRedo() {
  if (!done) {
    seq->selections = new_data;
    done = true;
  }
}

EditSequenceCommand::EditSequenceCommand(Media* i, SequencePtr s) {
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
  // Update sequence's tooltip
  item->update_tooltip();

  for (int i=0;i<seq->clips.size();i++) {
    if (seq->clips.at(i) != nullptr) {
      seq->clips.at(i)->refresh();
    }
  }

  if (olive::ActiveSequence == seq) {
    olive::Global->set_sequence(seq);
  }
}

AddMarkerAction::AddMarkerAction(QVector<Marker>* m, long t, QString n) {
  active_array = m;
  time = t;
  name = n;
}

void AddMarkerAction::doUndo() {
  if (index == -1) {
    active_array->removeLast();
  } else {
    active_array[0][index].name = old_name;
  }
}

void AddMarkerAction::doRedo() {
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
    active_array[0][index].name = name;
  }
}

MoveMarkerAction::MoveMarkerAction(Marker* m, long o, long n) {
  marker = m;
  old_time = o;
  new_time = n;
}

void MoveMarkerAction::doUndo() {
  marker->frame = old_time;

}

void MoveMarkerAction::doRedo() {
  marker->frame = new_time;
}

DeleteMarkerAction::DeleteMarkerAction(QVector<Marker> *m) {
  active_array = m;
  sorted = false;
}

void DeleteMarkerAction::doUndo() {
  for (int i=markers.size()-1;i>=0;i--) {
    active_array->insert(markers.at(i), copies.at(i));
  }

}

void DeleteMarkerAction::doRedo() {
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
