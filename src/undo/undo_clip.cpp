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

#include "undo_clip.h"

#include "panels/panels.h"
#include "panels/project.h"
#include "panels/effectcontrols.h"
#include "panels/viewer.h"
#include "panels/timeline.h"
#include "project/clipboard.h"
#include "project/media.h"
#include "rendering/renderfunctions.h"
#include "ui/viewerwidget.h"

MoveClipAction::MoveClipAction(Clip *c, long iin, long iout, long iclip_in, int itrack, bool irelative) {
  clip = c;

  old_in = c->timeline_in();
  old_out = c->timeline_out();
  old_clip_in = c->clip_in();
  old_track = c->track();

  new_in = iin;
  new_out = iout;
  new_clip_in = iclip_in;
  new_track = itrack;

  relative = irelative;
}

void MoveClipAction::doUndo() {
  if (relative) {
    clip->set_timeline_in (clip->timeline_in() - new_in);
    clip->set_timeline_out (clip->timeline_out() - new_out);
    clip->set_clip_in (clip->clip_in() - new_clip_in);
    clip->set_track (clip->track() - new_track);
  } else {
    clip->set_timeline_in(old_in);
    clip->set_timeline_out(old_out);
    clip->set_clip_in(old_clip_in);
    clip->set_track(old_track);
  }
}

void MoveClipAction::doRedo() {
  if (relative) {
    clip->set_timeline_in(clip->timeline_in() + new_in);
    clip->set_timeline_out(clip->timeline_out() + new_out);
    clip->set_clip_in(clip->clip_in() + new_clip_in);
    clip->set_track(clip->track() + new_track);
  } else {
    clip->set_timeline_in(new_in);
    clip->set_timeline_out(new_out);
    clip->set_clip_in(new_clip_in);
    clip->set_track(new_track);
  }
}

DeleteClipAction::DeleteClipAction(Sequence *s, int clip) {
  seq = s;
  index = clip;
  opening_transition = -1;
  closing_transition = -1;
}

DeleteClipAction::~DeleteClipAction() {}

void DeleteClipAction::doUndo() {
  // restore ref to clip
  seq->clips[index] = ref;

  // restore links to this clip
  for (int i=linkClipIndex.size()-1;i>=0;i--) {
    seq->clips.at(linkClipIndex.at(i))->linked.insert(linkLinkIndex.at(i), index);
  }

  ref = nullptr;
}

void DeleteClipAction::doRedo() {
  // remove ref to clip
  ref = seq->clips.at(index);
  if (ref->IsOpen()) {
    ref->Close(true);
  }
  seq->clips[index] = nullptr;

  // delete link to this clip
  linkClipIndex.clear();
  linkLinkIndex.clear();
  for (int i=0;i<seq->clips.size();i++) {
    ClipPtr c = seq->clips.at(i);
    if (c != nullptr) {
      for (int j=0;j<c->linked.size();j++) {
        if (c->linked.at(j) == index) {
          linkClipIndex.append(i);
          linkLinkIndex.append(j);
          c->linked.removeAt(j);
          j--;
        }
      }
    }
  }
}

AddClipCommand::AddClipCommand(Sequence *s, QVector<ClipPtr>& add) {
  link_offset_ = 0;
  seq = s;
  clips = add;
}

AddClipCommand::~AddClipCommand() {}

void AddClipCommand::doUndo() {
  // clear effects panel
  panel_graph_editor->set_row(nullptr);
  panel_effect_controls->Clear(true);

  for (int i=0;i<clips.size();i++) {
    ClipPtr c = seq->clips.last();

    if (c != nullptr) {
      // un-offset all the clips
      for (int j=0;j<c->linked.size();j++) {
        c->linked[j] -= link_offset_;
      }

      // deselect the area occupied by this clip
      panel_timeline->deselect_area(c->timeline_in(), c->timeline_out(), c->track());

      // if the clip is open, close it
      if (c->IsOpen()) {
        c->Close(true);
      }
    }

    // remove it from the sequence
    seq->clips.removeLast();
  }

}

void AddClipCommand::doRedo() {
  link_offset_ = seq->clips.size();
  for (int i=0;i<clips.size();i++) {
    ClipPtr original = clips.at(i);

    if (original != nullptr) {

      // offset all links by the current clip size
      for (int j=0;j<original->linked.size();j++) {
        original->linked[j] += link_offset_;
      }

    }

    seq->clips.append(original);
  }
}

ReplaceClipMediaCommand::ReplaceClipMediaCommand(Media *a, Media *b, bool e) {
  old_media = a;
  new_media = b;
  preserve_clip_ins = e;
}

void ReplaceClipMediaCommand::replace(bool undo) {
  if (!undo) {
    old_clip_ins.clear();
  }

  for (int i=0;i<clips.size();i++) {
    ClipPtr c = clips.at(i);
    if (c->IsOpen()) {
      c->Close(true);
    }

    if (undo) {
      if (!preserve_clip_ins) {
        c->set_clip_in(old_clip_ins.at(i));
      }

      c->set_media(old_media, c->media_stream_index());
    } else {
      if (!preserve_clip_ins) {
        old_clip_ins.append(c->clip_in());
        c->set_clip_in(0);
      }

      c->set_media(new_media, c->media_stream_index());
    }

    c->replaced = true;
    c->refresh();
  }
}

void ReplaceClipMediaCommand::doUndo() {
  replace(true);
}

void ReplaceClipMediaCommand::doRedo() {
  replace(false);

  update_ui(true);
}

SetClipProperty::SetClipProperty(SetClipPropertyType type) : type_(type)
{}

void SetClipProperty::AddSetting(QVector<Clip *> clips, bool setting)
{
  for (int i=0;i<clips.size();i++) {
    AddSetting(clips.at(i), setting);
  }
}

void SetClipProperty::AddSetting(Clip* c, bool setting)
{
  clips_.append(c);
  setting_.append(setting);

  // store current setting for undoing
  bool old_setting = false;
  switch (type_) {
  case kSetClipPropertyAutoscale:
    old_setting = c->autoscaled();
    break;
  case kSetClipPropertyReversed:
    old_setting = c->reversed();
    break;
  case kSetClipPropertyMaintainAudioPitch:
    old_setting = c->speed().maintain_audio_pitch;
    break;
  case kSetClipPropertyEnabled:
    old_setting = c->enabled();
    break;
  }
  old_setting_.append(old_setting);
}

void SetClipProperty::MainLoop(bool undo)
{
  for (int i=0;i<clips_.size();i++) {

    bool setting = (undo) ? old_setting_.at(i) : setting_.at(i);

    switch (type_) {
    case kSetClipPropertyAutoscale:
      clips_.at(i)->set_autoscaled(setting);
      break;
    case kSetClipPropertyReversed:
      clips_.at(i)->set_reversed(setting);
      break;
    case kSetClipPropertyMaintainAudioPitch:
    {
      ClipSpeed s = clips_.at(i)->speed();
      s.maintain_audio_pitch = setting;
      clips_.at(i)->set_speed(s);
    }
      break;
    case kSetClipPropertyEnabled:
      clips_.at(i)->set_enabled(setting);
      break;
    }
  }
}

void SetClipProperty::doUndo() {
  MainLoop(true);
  panel_sequence_viewer->viewer_widget->frame_update();

}

void SetClipProperty::doRedo() {
  MainLoop(false);
  panel_sequence_viewer->viewer_widget->frame_update();
}

SetSpeedAction::SetSpeedAction(Clip* c, double speed) {
  clip = c;
  old_speed = c->speed().value;
  new_speed = speed;
}

void SetSpeedAction::doUndo() {
  ClipSpeed cs = clip->speed();

  cs.value = old_speed;

  clip->set_speed(cs);
}

void SetSpeedAction::doRedo() {
  ClipSpeed cs = clip->speed();

  cs.value = new_speed;

  clip->set_speed(cs);
}

RenameClipCommand::RenameClipCommand(Clip *clip, QString new_name)
{
  clip_ = clip;
  old_name_ = clip_->name();
  new_name_ = new_name;
}

void RenameClipCommand::doUndo() {
  clip_->set_name(old_name_);
}

void RenameClipCommand::doRedo() {
  clip_->set_name(new_name_);
}

RemoveClipsFromClipboard::RemoveClipsFromClipboard(int index) {
  pos = index;
  done = false;
}

RemoveClipsFromClipboard::~RemoveClipsFromClipboard() {}

void RemoveClipsFromClipboard::doUndo() {
  clipboard.insert(pos, clip);
  done = false;
}

void RemoveClipsFromClipboard::doRedo() {
  clip = std::static_pointer_cast<Clip>(clipboard.at(pos));
  clipboard.removeAt(pos);
  done = true;
}

RefreshClips::RefreshClips(Media *m) {
  media = m;
}

void RefreshClips::doUndo() {
  redo();
}

void RefreshClips::doRedo() {
  // close any clips currently using this media
  QVector<Media*> all_sequences = panel_project->list_all_project_sequences();
  for (int i=0;i<all_sequences.size();i++) {
    Sequence* s = all_sequences.at(i)->to_sequence().get();
    for (int j=0;j<s->clips.size();j++) {
      Clip* c = s->clips.at(j).get();
      if (c != nullptr && c->media() == media) {
        c->replaced = true;
        c->refresh();
      }
    }
  }
}

LinkCommand::LinkCommand() {
  link = true;
}

void LinkCommand::doUndo() {
  for (int i=0;i<clips.size();i++) {
    ClipPtr c = s->clips.at(clips.at(i));
    if (link) {
      c->linked.clear();
    } else {
      c->linked = old_links.at(i);
    }
  }

}

void LinkCommand::doRedo() {
  old_links.clear();
  for (int i=0;i<clips.size();i++) {
    ClipPtr c = s->clips.at(clips.at(i));
    if (link) {
      for (int j=0;j<clips.size();j++) {
        if (i != j) {
          c->linked.append(clips.at(j));
        }
      }
    } else {
      old_links.append(c->linked);
      c->linked.clear();
    }
  }
}
