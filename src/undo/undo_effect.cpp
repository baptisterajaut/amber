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

#include "undo_effect.h"

#include "panels/panels.h"
#include "panels/effectcontrols.h"

SetEffectEnabled::SetEffectEnabled(Effect *e, bool enabled) :
  effect_(e),
  old_val_(e->IsEnabled()),
  new_val_(enabled)
{}

void SetEffectEnabled::doUndo() {
  effect_->SetEnabled(old_val_);
}

void SetEffectEnabled::doRedo() {
  effect_->SetEnabled(new_val_);
}

AddEffectCommand::AddEffectCommand(Clip* c, EffectPtr e, const EffectMeta *m, int insert_pos) {
  clip = c;
  ref = e;
  meta = m;
  pos = insert_pos;
}

void AddEffectCommand::doUndo() {
  clip->effects.last()->close();
  if (pos < 0) {
    clip->effects.removeLast();
  } else {
    clip->effects.removeAt(pos);
  }
}

void AddEffectCommand::doRedo() {
  if (ref == nullptr) {
    ref = Effect::Create(clip, meta);
  }
  if (pos < 0) {
    clip->effects.append(ref);
  } else {
    clip->effects.insert(pos, ref);
  }
}

EffectDeleteCommand::EffectDeleteCommand(Effect *e) :
  effect_(e)
{}

void EffectDeleteCommand::doUndo() {
  parent_clip_->effects.insert(index_, deleted_obj_);

  panel_effect_controls->Reload();
}

void EffectDeleteCommand::doRedo() {
  parent_clip_ = effect_->parent_clip;

  index_ = parent_clip_->IndexOfEffect(effect_);

  Q_ASSERT(index_ > -1);

  effect_->close();
  deleted_obj_ = parent_clip_->effects.at(index_);
  parent_clip_->effects.removeAt(index_);

  panel_effect_controls->Reload();
}

MoveEffectCommand::MoveEffectCommand() {}

void MoveEffectCommand::doUndo() {
  clip->effects.move(to, from);

}

void MoveEffectCommand::doRedo() {
  clip->effects.move(from, to);
}

SetEffectData::SetEffectData(Effect *e, const QByteArray &s) {
  effect = e;
  data = s;
}

void SetEffectData::doUndo() {
  effect->load_from_string(old_data);

  old_data.clear();
}

void SetEffectData::doRedo() {
  old_data = effect->save_to_string();

  effect->load_from_string(data);
}

void ReloadEffectsCommand::doUndo() {
  redo();
}

void ReloadEffectsCommand::doRedo() {
  panel_effect_controls->Reload();
}

SetIsKeyframing::SetIsKeyframing(EffectRow *irow, bool ib) {
  row = irow;
  b = ib;
}

void SetIsKeyframing::doUndo() {
  row->SetKeyframingInternal(!b);
}

void SetIsKeyframing::doRedo() {
  row->SetKeyframingInternal(b);
}

AddTransitionCommand::AddTransitionCommand(Clip* iopen,
                                           Clip* iclose,
                                           TransitionPtr copy,
                                           const EffectMeta *itransition,
                                           int ilength) {
  open_ = iopen;
  close_ = iclose;
  transition_to_copy_ = copy;
  transition_meta_ = itransition;
  length_ = ilength;
  new_transition_ref_ = nullptr;
}

void AddTransitionCommand::doUndo() {
  if (open_ != nullptr) {
    open_->opening_transition = old_open_transition_;
  }

  if (close_ != nullptr)  {
    close_->closing_transition = old_close_transition_;
  }
}

void AddTransitionCommand::doRedo() {
  // convert open/close clips to primary/secondary for transition object
  Clip* primary = open_;
  Clip* secondary = close_;
  if (primary == nullptr) {
    primary = secondary;
    secondary = nullptr;
  }

  // create new transition object
  if (new_transition_ref_ == nullptr) {
    if (transition_to_copy_ == nullptr) {
      new_transition_ref_ = Transition::CreateFromMeta(primary, secondary, transition_meta_);
    } else {
      new_transition_ref_ = transition_to_copy_->copy(primary, nullptr);
    }
  }

  // set opening clip's opening transition to this and store the old one
  if (open_ != nullptr) {
    old_open_transition_ = open_->opening_transition;

    open_->opening_transition = new_transition_ref_;
  }

  // set closing clip's closing transition to this and store the old one
  if (close_ != nullptr) {
    old_close_transition_ = close_->closing_transition;

    close_->closing_transition = new_transition_ref_;
  }

  // if a length was specified, set it now
  if (length_ > 0) {
    new_transition_ref_->set_length(length_);
  }
}

ModifyTransitionCommand::ModifyTransitionCommand(TransitionPtr t, long ilength) {
  transition_ref_ = t;
  new_length_ = ilength;
  old_length_ = transition_ref_->get_true_length();
}

void ModifyTransitionCommand::doUndo() {
  transition_ref_->set_length(old_length_);
}

void ModifyTransitionCommand::doRedo() {

  transition_ref_->set_length(new_length_);
}

DeleteTransitionCommand::DeleteTransitionCommand(TransitionPtr t) {
  transition_ref_ = t;
}

void DeleteTransitionCommand::doUndo() {
  if (opened_clip_ != nullptr) {
    opened_clip_->opening_transition = transition_ref_;
  }

  if (closed_clip_ != nullptr) {
    closed_clip_->closing_transition = transition_ref_;
  }
}

void DeleteTransitionCommand::doRedo() {
  opened_clip_ = transition_ref_->get_opened_clip();
  closed_clip_ = transition_ref_->get_closed_clip();

  if (opened_clip_ != nullptr) {
    opened_clip_->opening_transition = nullptr;
  }

  if (closed_clip_ != nullptr) {
    closed_clip_->closing_transition = nullptr;
  }
}

KeyframeDelete::KeyframeDelete(EffectField *ifield, int iindex) :
  field(ifield),
  index(iindex)
{
}

void KeyframeDelete::doUndo() {
  field->keyframes.insert(index, deleted_key);
}

void KeyframeDelete::doRedo() {
  deleted_key = field->keyframes.at(index);
  field->keyframes.removeAt(index);
}

KeyframeAdd::KeyframeAdd(EffectField *ifield, int ii) :
  field(ifield),
  index(ii),
  key(ifield->keyframes.at(ii)),
  done(true)
{
}

void KeyframeAdd::doUndo() {
  field->keyframes.removeAt(index);
  done = false;
}

void KeyframeAdd::doRedo() {
  if (!done) {
    field->keyframes.insert(index, key);
    done = true;
  }
}

KeyframeDataChange::KeyframeDataChange(EffectField *field) :
  field_(field),
  done_(true)
{
  old_keys_ = field_->keyframes;
  old_persistent_data_ = field_->persistent_data_;
}

void KeyframeDataChange::SetNewKeyframes()
{
  new_keys_ = field_->keyframes;
  new_persistent_data_ = field_->persistent_data_;
}

void KeyframeDataChange::doUndo()
{
  field_->keyframes = old_keys_;
  field_->persistent_data_ = old_persistent_data_;
  done_ = false;
}

void KeyframeDataChange::doRedo()
{
  if (!done_) {
    field_->keyframes = new_keys_;
    field_->persistent_data_ = new_persistent_data_;
    done_ = true;
  }
}
