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
#include "panels.h"

UndoHistoryPanel::UndoHistoryPanel(QWidget* parent) : Panel(parent) {
  setObjectName("undo_history");

  QWidget* central = new QWidget(this);
  QVBoxLayout* layout = new QVBoxLayout(central);
  layout->setContentsMargins(0, 0, 0, 0);

  view_ = new QUndoView(&amber::UndoStack, this);
  layout->addWidget(view_);

  setWidget(central);

  // QUndoView calls QUndoStack::setIndex() when the user clicks an entry,
  // but that bypasses OliveGlobal::undo()/redo() which normally refresh the UI.
  // Connect indexChanged to update_ui so all panels repaint after navigation.
  connect(&amber::UndoStack, &QUndoStack::indexChanged, this, [](int) {
    update_ui(true);
  });
}

void UndoHistoryPanel::Retranslate() {
  setWindowTitle(tr("Undo History"));
  view_->setEmptyLabel(tr("Initial State"));
}
