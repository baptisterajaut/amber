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

class UndoHistoryPanel : public Panel {
  Q_OBJECT
 public:
  explicit UndoHistoryPanel(QWidget* parent);
  void Retranslate() override;

 private slots:
  /** Rebuilds the tree to reflect the current stack contents and highlights
   *  the active entry whenever the stack index changes. */
  void onStackChanged();

  /** Navigates the undo stack to the entry the user clicked. */
  void onItemClicked();

 private:
  QTreeWidget* tree_;

  /** Rebuild the full list of rows from scratch. */
  void rebuildTree();

  /** Scroll to and visually select the row that matches the current index. */
  void highlightCurrentRow();
};

#endif  // UNDOHISTORYPANEL_H
