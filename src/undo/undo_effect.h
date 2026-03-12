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

#ifndef UNDO_EFFECT_H
#define UNDO_EFFECT_H

#include "undoactions.h"

class SetEffectEnabled : public OliveAction {
public:
  SetEffectEnabled(Effect* e, bool enabled);
  void doUndo() override;
  void doRedo() override;
private:
  Effect* effect_;
  bool old_val_;
  bool new_val_;
};

class AddEffectCommand : public OliveAction {
public:
  AddEffectCommand(Clip* c, EffectPtr e, const EffectMeta* m, int insert_pos = -1);
  void doUndo() override;
  void doRedo() override;
private:
  Clip* clip;
  const EffectMeta* meta;
  EffectPtr ref;
  int pos;
  bool done;
};

class EffectDeleteCommand : public OliveAction {
public:
  explicit EffectDeleteCommand(Effect* e);
  void doUndo() override;
  void doRedo() override;
private:
  Effect* effect_;
  EffectPtr deleted_obj_;
  Clip* parent_clip_;
  int index_;
};

class MoveEffectCommand : public OliveAction {
public:
  MoveEffectCommand();
  void doUndo() override;
  void doRedo() override;
  Clip* clip;
  int from;
  int to;
};

class SetEffectData : public OliveAction {
public:
  SetEffectData(Effect* e, const QByteArray &s);
  void doUndo() override;
  void doRedo() override;
private:
  Effect* effect;
  QByteArray data;
  QByteArray old_data;
};

class ReloadEffectsCommand : public OliveAction {
public:
  void doUndo() override;
  void doRedo() override;
};

class SetIsKeyframing : public OliveAction {
public:
  SetIsKeyframing(EffectRow* irow, bool ib);
  void doUndo() override;
  void doRedo() override;
private:
  EffectRow* row;
  bool b;
};

class AddTransitionCommand : public OliveAction {
public:
  AddTransitionCommand(Clip* iopen, Clip* iclose, TransitionPtr copy, const EffectMeta* itransition, int ilength);
  void doUndo() override;
  void doRedo() override;
private:
  Clip* open_;
  Clip* close_;
  TransitionPtr transition_to_copy_;
  const EffectMeta* transition_meta_;
  int length_;
  TransitionPtr old_open_transition_;
  TransitionPtr old_close_transition_;
  TransitionPtr new_transition_ref_;
};

class ModifyTransitionCommand : public OliveAction {
public:
  ModifyTransitionCommand(TransitionPtr t, long ilength);
  void doUndo() override;
  void doRedo() override;
private:
  TransitionPtr transition_ref_;
  long new_length_;
  long old_length_;
};

class DeleteTransitionCommand : public OliveAction {
public:
  explicit DeleteTransitionCommand(TransitionPtr t);
  void doUndo() override;
  void doRedo() override;
private:
  TransitionPtr transition_ref_;
  Clip* opened_clip_;
  Clip* closed_clip_;
};

class KeyframeDelete : public OliveAction {
public:
  KeyframeDelete(EffectField* ifield, int iindex);
  void doUndo() override;
  void doRedo() override;
private:
  EffectField* field;
  int index;
  bool done;
  EffectKeyframe deleted_key;
};

class KeyframeAdd : public OliveAction {
public:
  KeyframeAdd(EffectField* ifield, int ii);
  void doUndo() override;
  void doRedo() override;
private:
  EffectField* field;
  int index;
  EffectKeyframe key;
  bool done{true};
};

class KeyframeDataChange : public OliveAction {
public:
  explicit KeyframeDataChange(EffectField* field);

  void SetNewKeyframes();

  void doUndo() override;
  void doRedo() override;

private:
  EffectField* field_;
  QVector<EffectKeyframe> old_keys_;
  QVector<EffectKeyframe> new_keys_;
  QVariant old_persistent_data_;
  QVariant new_persistent_data_;
  bool done_{true};
};

#endif // UNDO_EFFECT_H
