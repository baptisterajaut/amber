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

#ifndef UNDOHISTORYPANEL_H
#define UNDOHISTORYPANEL_H

#include "ui/panel.h"

class QTreeWidget;
class QTreeWidgetItem;

class UndoHistoryPanel : public Panel {
  Q_OBJECT
 public:
  explicit UndoHistoryPanel(QWidget* parent);
  void Retranslate() override;

 private slots:
  /** Rebuilds the rows when commands were pushed or cleared (deferred if we're inside a tree signal). */
  void onHistoryChanged();

  /** Navigates the undo stack to the entry the user picked (mouse or keyboard). */
  void onCurrentItemChanged(QTreeWidgetItem* current);

 private:
  QTreeWidget* tree_;

  /** True while onCurrentItemChanged() runs, i.e. while the tree is dispatching its own signal. */
  bool in_tree_signal_ = false;

  /** Rebuild the full list of rows from scratch. */
  void rebuildTree();

  /** Restyle rows in place (bold current, grey undone) and select the current one, without emitting tree signals. */
  void highlightCurrentRow();
};

#endif  // UNDOHISTORYPANEL_H
