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

#ifndef UNDO_GENERIC_H
#define UNDO_GENERIC_H

#include "undoactions.h"

class CheckboxCommand : public OliveAction {
public:
  CheckboxCommand(QCheckBox* b);
  virtual ~CheckboxCommand() override;
  virtual void doUndo() override;
  virtual void doRedo() override;
private:
  QCheckBox* box;
  bool checked;
  bool done;
};

class SetBool : public OliveAction {
public:
  SetBool(bool* b, bool setting);
  virtual void doUndo() override;
  virtual void doRedo() override;
private:
  bool* boolean;
  bool old_setting;
  bool new_setting;
};

class SetInt : public OliveAction {
public:
  SetInt(int* pointer, int new_value);
  virtual void doUndo() override;
  virtual void doRedo() override;
private:
  int* p;
  int oldval;
  int newval;
};

class SetLong : public OliveAction {
public:
  SetLong(long* pointer, long old_value, long new_value);
  virtual void doUndo() override;
  virtual void doRedo() override;
private:
  long* p;
  long oldval;
  long newval;
};

class SetDouble : public OliveAction {
public:
  SetDouble(double* pointer, double old_value, double new_value);
  virtual void doUndo() override;
  virtual void doRedo() override;
private:
  double* p;
  double oldval;
  double newval;
};

class SetString : public OliveAction {
public:
  SetString(QString* pointer, QString new_value);
  virtual void doUndo() override;
  virtual void doRedo() override;
private:
  QString* p;
  QString oldval;
  QString newval;
};

class SetPointer : public OliveAction {
public:
  SetPointer(void** pointer, void* data);
  virtual void doUndo() override;
  virtual void doRedo() override;
private:
  bool old_changed;
  void** p;
  void* new_data;
  void* old_data;
};

class SetQVariant : public OliveAction {
public:
  SetQVariant(QVariant* itarget, const QVariant& iold, const QVariant& inew);
  virtual void doUndo() override;
  virtual void doRedo() override;
private:
  QVariant* target;
  QVariant old_val;
  QVariant new_val;
};

class CloseAllClipsCommand : public OliveAction {
public:
  virtual void doUndo() override;
  virtual void doRedo() override;
};

class UpdateViewer : public OliveAction {
public:
  virtual void doUndo() override;
  virtual void doRedo() override;
};

#endif // UNDO_GENERIC_H
