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

#include "core/appcontext.h"
#include "rendering/renderfunctions.h"

OliveAction::OliveAction(bool iset_window_modified) {
  set_window_modified = iset_window_modified;
}

OliveAction::~OliveAction() = default;

void OliveAction::undo() {
  doUndo();

  if (set_window_modified && amber::app_ctx) {
    amber::app_ctx->setModified(old_window_modified);
  }
}

void OliveAction::redo() {
  doRedo();

  if (set_window_modified && amber::app_ctx) {

    // store current modified state
    old_window_modified = amber::app_ctx->isModified();

    // set modified to true
    amber::app_ctx->setModified(true);

  }
}

SetBool::SetBool(bool* b, bool setting) {
  boolean = b;
  old_setting = *b;
  new_setting = setting;
}

void SetBool::doUndo() {
  if (!boolean) {
    qWarning() << "SetBool::doUndo: pointer is null";
    return;
  }
  *boolean = old_setting;
}

void SetBool::doRedo() {
  if (!boolean) {
    qWarning() << "SetBool::doRedo: pointer is null";
    return;
  }
  *boolean = new_setting;
}

SetInt::SetInt(int* pointer, int new_value) {
  p = pointer;
  oldval = *pointer;
  newval = new_value;
}

void SetInt::doUndo() {
  if (!p) {
    qWarning() << "SetInt::doUndo: pointer is null";
    return;
  }
  *p = oldval;
}

void SetInt::doRedo() {
  if (!p) {
    qWarning() << "SetInt::doRedo: pointer is null";
    return;
  }
  *p = newval;
}

SetLong::SetLong(long *pointer, long old_value, long new_value) {
  p = pointer;
  oldval = old_value;
  newval = new_value;
}

void SetLong::doUndo() {
  if (!p) {
    qWarning() << "SetLong::doUndo: pointer is null";
    return;
  }
  *p = oldval;
}

void SetLong::doRedo() {
  if (!p) {
    qWarning() << "SetLong::doRedo: pointer is null";
    return;
  }
  *p = newval;
}

SetDouble::SetDouble(double* pointer, double old_value, double new_value) :
  p(pointer),
  oldval(old_value),
  newval(new_value)
{
}

void SetDouble::doUndo() {
  if (!p) {
    qWarning() << "SetDouble::doUndo: pointer is null";
    return;
  }
  *p = oldval;
}

void SetDouble::doRedo() {
  if (!p) {
    qWarning() << "SetDouble::doRedo: pointer is null";
    return;
  }
  *p = newval;
}

SetString::SetString(QString* pointer, QString new_value) {
  p = pointer;
  oldval = *pointer;
  newval = new_value;
}

void SetString::doUndo() {
  if (!p) {
    qWarning() << "SetString::doUndo: pointer is null";
    return;
  }
  *p = oldval;
}

void SetString::doRedo() {
  if (!p) {
    qWarning() << "SetString::doRedo: pointer is null";
    return;
  }
  *p = newval;
}

SetPointer::SetPointer(void **pointer, void *data) {
  p = pointer;
  new_data = data;
}

void SetPointer::doUndo() {
  if (!p) {
    qWarning() << "SetPointer::doUndo: pointer is null";
    return;
  }
  *p = old_data;
}

void SetPointer::doRedo() {
  if (!p) {
    qWarning() << "SetPointer::doRedo: pointer is null";
    return;
  }
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
  if (!target) {
    qWarning() << "SetQVariant::doUndo: target is null";
    return;
  }
  *target = old_val;
}

void SetQVariant::doRedo() {
  if (!target) {
    qWarning() << "SetQVariant::doRedo: target is null";
    return;
  }
  *target = new_val;
}

void CloseAllClipsCommand::doUndo() {
  redo();
}

void CloseAllClipsCommand::doRedo() {
  if (amber::ActiveSequence != nullptr) {
    close_active_clips(amber::ActiveSequence.get());
  }
}

void UpdateViewer::doUndo() {
  redo();
}

void UpdateViewer::doRedo() {
  if (amber::app_ctx) amber::app_ctx->refreshViewer();
}
