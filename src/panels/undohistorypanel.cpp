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

#include "undohistorypanel.h"

#include <QUndoView>
#include <QVBoxLayout>

#include "engine/undo/undostack.h"

UndoHistoryPanel::UndoHistoryPanel(QWidget* parent) : Panel(parent) {
  setObjectName("undo_history");
  setWindowTitle(tr("Undo History"));

  QWidget* central = new QWidget(this);
  QVBoxLayout* layout = new QVBoxLayout(central);
  layout->setContentsMargins(0, 0, 0, 0);

  view_ = new QUndoView(&amber::UndoStack, this);
  view_->setEmptyLabel(tr("Initial State"));
  layout->addWidget(view_);

  setWidget(central);
}
