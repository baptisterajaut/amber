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

#ifndef UNDO_CLIP_H
#define UNDO_CLIP_H

#include "undoactions.h"

class MoveClipAction : public OliveAction {
public:
  MoveClipAction(Clip* c, long iin, long iout, long iclip_in, int itrack, bool irelative);
  virtual void doUndo() override;
  virtual void doRedo() override;
private:
  Clip* clip;

  long old_in;
  long old_out;
  long old_clip_in;
  int old_track;

  long new_in;
  long new_out;
  long new_clip_in;
  int new_track;

  bool relative;
};

class DeleteClipAction : public OliveAction {
public:
  DeleteClipAction(Sequence* s, int clip);
  virtual ~DeleteClipAction() override;
  virtual void doUndo() override;
  virtual void doRedo() override;
private:
  Sequence* seq;
  ClipPtr ref;
  int index;

  int opening_transition;
  int closing_transition;

  QVector<int> linkClipIndex;
  QVector<int> linkLinkIndex;
};

class AddClipCommand : public OliveAction {
public:
  AddClipCommand(Sequence* s, QVector<ClipPtr>& add);
  virtual ~AddClipCommand() override;
  virtual void doUndo() override;
  virtual void doRedo() override;
private:
  Sequence* seq;
  QVector<ClipPtr> clips;
  int link_offset_;
};

class ReplaceClipMediaCommand : public OliveAction {
public:
  ReplaceClipMediaCommand(Media *, Media *, bool);
  virtual void doUndo() override;
  virtual void doRedo() override;
  QVector<ClipPtr> clips;
private:
  Media* old_media;
  Media* new_media;
  bool preserve_clip_ins;
  QVector<int> old_clip_ins;
  void replace(bool undo);
};

enum SetClipPropertyType {
  kSetClipPropertyAutoscale,
  kSetClipPropertyReversed,
  kSetClipPropertyMaintainAudioPitch,
  kSetClipPropertyEnabled
};

class SetClipProperty : public OliveAction {
public:
  SetClipProperty(SetClipPropertyType type);
  virtual void doUndo() override;
  virtual void doRedo() override;
  void AddSetting(QVector<Clip *> clips, bool setting);
  void AddSetting(Clip *c, bool setting);
private:
  SetClipPropertyType type_;
  QVector<Clip*> clips_;
  QVector<bool> setting_;
  QVector<bool> old_setting_;
  void MainLoop(bool undo);
};

class SetSpeedAction : public OliveAction {
public:
  SetSpeedAction(Clip* c, double speed);
  virtual void doUndo() override;
  virtual void doRedo() override;
private:
  Clip* clip;
  double old_speed;
  double new_speed;
};

class RenameClipCommand : public OliveAction {
public:
  RenameClipCommand(Clip* clip, QString new_name);
  virtual void doUndo() override;
  virtual void doRedo() override;
private:
  QString old_name_;
  QString new_name_;
  Clip* clip_;
};

class RemoveClipsFromClipboard : public OliveAction {
public:
  RemoveClipsFromClipboard(int index);
  virtual ~RemoveClipsFromClipboard() override;
  virtual void doUndo() override;
  virtual void doRedo() override;
private:
  int pos;
  ClipPtr clip;
  bool done;
};

class RefreshClips : public OliveAction {
public:
  RefreshClips(Media* m);
  virtual void doUndo() override;
  virtual void doRedo() override;
private:
  Media* media;
};

class LinkCommand : public OliveAction {
public:
  LinkCommand();
  virtual void doUndo() override;
  virtual void doRedo() override;
  Sequence* s;
  QVector<int> clips;
  bool link;
private:
  QVector< QVector<int> > old_links;
};

#endif // UNDO_CLIP_H
