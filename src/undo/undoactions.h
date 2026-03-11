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

#ifndef UNDOACTIONS_H
#define UNDOACTIONS_H

#include <QCheckBox>
#include <QUndoStack>
#include <QUndoCommand>
#include <QVector>
#include <QVariant>
#include <QModelIndex>

#include "comboaction.h"

#include "timeline/marker.h"
#include "timeline/selection.h"
#include "effects/keyframe.h"

class Clip;
using ClipPtr = std::shared_ptr<Clip>;

class Sequence;
using SequencePtr = std::shared_ptr<Sequence>;

class Media;
using MediaPtr = std::shared_ptr<Media>;

class Effect;
using EffectPtr = std::shared_ptr<Effect>;

class Transition;
using TransitionPtr = std::shared_ptr<Transition>;

struct EffectMeta;
class EffectRow;
class EffectField;

class OliveAction : public QUndoCommand {
public:
  OliveAction(bool iset_window_modified = true);
  virtual ~OliveAction() override;

  virtual void undo() override;
  virtual void redo() override;

  virtual void doUndo() = 0;
  virtual void doRedo() = 0;
private:
  /**
     * @brief Setting whether to change the windowModified state of MainWindow
     */
  bool set_window_modified;

  /**
     * @brief Cache previous window modified value to return to if the user undoes this action
     */
  bool old_window_modified;
};

#endif // UNDOACTIONS_H
