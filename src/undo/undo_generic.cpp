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

#include "undo_generic.h"

#include "global/global.h"
#include "panels/panels.h"
#include "panels/viewer.h"
#include "rendering/renderfunctions.h"
#include "ui/viewerwidget.h"

OliveAction::OliveAction(bool iset_window_modified) {
  set_window_modified = iset_window_modified;
}

OliveAction::~OliveAction() {}

void OliveAction::undo() {
  doUndo();

  if (set_window_modified) {
    olive::Global->set_modified(old_window_modified);
  }
}

void OliveAction::redo() {
  doRedo();

  if (set_window_modified) {

    // store current modified state
    old_window_modified = olive::Global->is_modified();

    // set modified to true
    olive::Global->set_modified(true);

  }
}

CheckboxCommand::CheckboxCommand(QCheckBox* b) {
  box = b;
  checked = box->isChecked();
  done = true;
}

CheckboxCommand::~CheckboxCommand() {}

void CheckboxCommand::doUndo() {
  box->setChecked(!checked);
  done = false;

}

void CheckboxCommand::doRedo() {
  if (!done) {
    box->setChecked(checked);
  }
}

SetBool::SetBool(bool* b, bool setting) {
  boolean = b;
  old_setting = *b;
  new_setting = setting;
}

void SetBool::doUndo() {
  *boolean = old_setting;
}

void SetBool::doRedo() {
  *boolean = new_setting;
}

SetInt::SetInt(int* pointer, int new_value) {
  p = pointer;
  oldval = *pointer;
  newval = new_value;
}

void SetInt::doUndo() {
  *p = oldval;

}

void SetInt::doRedo() {
  *p = newval;
}

SetLong::SetLong(long *pointer, long old_value, long new_value) {
  p = pointer;
  oldval = old_value;
  newval = new_value;
}

void SetLong::doUndo() {
  *p = oldval;

}

void SetLong::doRedo() {
  *p = newval;
}

SetDouble::SetDouble(double* pointer, double old_value, double new_value) :
  p(pointer),
  oldval(old_value),
  newval(new_value)
{
}

void SetDouble::doUndo() {
  *p = oldval;

}

void SetDouble::doRedo() {
  *p = newval;
}

SetString::SetString(QString* pointer, QString new_value) {
  p = pointer;
  oldval = *pointer;
  newval = new_value;
}

void SetString::doUndo() {
  *p = oldval;

}

void SetString::doRedo() {
  *p = newval;
}

SetPointer::SetPointer(void **pointer, void *data) {
  p = pointer;
  new_data = data;
}

void SetPointer::doUndo() {
  *p = old_data;
}

void SetPointer::doRedo() {
  old_data = *p;
  *p = new_data;
}

SetQVariant::SetQVariant(QVariant *itarget, const QVariant &iold, const QVariant &inew) :
  target(itarget),
  old_val(iold),
  new_val(inew)
{
}

void SetQVariant::doUndo() {
  *target = old_val;
}

void SetQVariant::doRedo() {
  *target = new_val;
}

void CloseAllClipsCommand::doUndo() {
  redo();
}

void CloseAllClipsCommand::doRedo() {
  close_active_clips(olive::ActiveSequence.get());
}

void UpdateViewer::doUndo() {
  redo();
}

void UpdateViewer::doRedo() {
  panel_sequence_viewer->viewer_widget->frame_update();
}
