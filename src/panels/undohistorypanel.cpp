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

#include <QHeaderView>
#include <QLocale>
#include <QTreeWidget>
#include <QVBoxLayout>

#include "engine/undo/undostack.h"
#include "panels.h"

// ---------------------------------------------------------------------------
// Column indices
// ---------------------------------------------------------------------------
static constexpr int kColAction    = 0;
static constexpr int kColTimestamp = 1;

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

UndoHistoryPanel::UndoHistoryPanel(QWidget* parent) : Panel(parent) {
  setObjectName("undo_history");

  QWidget* central = new QWidget(this);
  QVBoxLayout* layout = new QVBoxLayout(central);
  layout->setContentsMargins(0, 0, 0, 0);

  // Two-column tree: Action | Time
  tree_ = new QTreeWidget(this);
  tree_->setColumnCount(2);
  tree_->setRootIsDecorated(false);
  tree_->setSelectionMode(QAbstractItemView::SingleSelection);
  tree_->setUniformRowHeights(true);
  tree_->header()->setStretchLastSection(false);
  tree_->header()->setSectionResizeMode(kColAction, QHeaderView::Stretch);
  tree_->header()->setSectionResizeMode(kColTimestamp, QHeaderView::ResizeToContents);

  layout->addWidget(tree_);
  setWidget(central);

  // -------------------------------------------------------------------------
  // Connections
  // -------------------------------------------------------------------------

  // Rebuild (and re-highlight) whenever the stack changes.
  connect(&amber::UndoStack, &QUndoStack::indexChanged,    this, &UndoHistoryPanel::onStackChanged);
  connect(&amber::UndoStack, &QUndoStack::cleanChanged,    this, &UndoHistoryPanel::onStackChanged);

  // Let the user click a row to navigate the stack.
  connect(tree_, &QTreeWidget::itemSelectionChanged, this, &UndoHistoryPanel::onItemClicked);

  // Populate for the current state (e.g. panel opened mid-session).
  rebuildTree();
}

// ---------------------------------------------------------------------------
// Panel interface
// ---------------------------------------------------------------------------

void UndoHistoryPanel::Retranslate() {
  setWindowTitle(tr("Undo History"));
  tree_->setHeaderLabels({tr("Action"), tr("Time")});
}

// ---------------------------------------------------------------------------
// Private slots
// ---------------------------------------------------------------------------

void UndoHistoryPanel::onStackChanged() {
  // Block signals while we repopulate so currentRowChanged doesn't fire and
  // accidentally trigger an undo-stack navigation.
  QSignalBlocker blocker(tree_);
  rebuildTree();
}

void UndoHistoryPanel::onItemClicked() {
  const int row = tree_->currentIndex().row();
  if (row < 0) return;

  // QUndoStack::setIndex() uses 0-based indexing where 0 == "initial state"
  // (nothing done yet) and N == the Nth command has been applied.
  // Our tree rows map 1-to-1: row 0 == initial state, row N == command N.
  if (row != amber::UndoStack.index()) {
    amber::UndoStack.setIndex(row);
    // update_ui is already connected to indexChanged in the constructor's
    // QUndoStack::indexChanged → onStackChanged path, but that only repaints
    // the panel. We also need to refresh all other panels.
    update_ui(true);
  }
}

// ---------------------------------------------------------------------------
// Private helpers
// ---------------------------------------------------------------------------

void UndoHistoryPanel::rebuildTree() {
  tree_->clear();

  // Row 0 — "Initial State" placeholder (no timestamp)
  {
    QTreeWidgetItem* item = new QTreeWidgetItem(tree_);
    item->setText(kColAction, tr("Initial State"));
    item->setText(kColTimestamp, QString());
    item->setFlags(item->flags() & ~Qt::ItemIsEditable);
  }

  // Rows 1..count — one per command
  const int cmd_count = amber::UndoStack.count();
  for (int i = 1; i <= cmd_count; ++i) {
    const QUndoCommand* cmd = amber::UndoStack.command(i - 1);  // command() is 0-based
    QTreeWidgetItem* item = new QTreeWidgetItem(tree_);

    item->setText(kColAction, cmd ? cmd->text() : QString());
    item->setFlags(item->flags() & ~Qt::ItemIsEditable);

    // Timestamp: format as HH:mm:ss so it stays compact.
    const QDateTime ts = amber::UndoStack.timestampAt(i);
    if (ts.isValid()) {
      item->setText(kColTimestamp, ts.toString(QStringLiteral("HH:mm:ss")));
      // Full date in the tooltip for actions done the previous day etc.
      item->setToolTip(kColTimestamp, QLocale::system().toString(ts, QLocale::LongFormat));
    }
  }

  highlightCurrentRow();
}

void UndoHistoryPanel::highlightCurrentRow() {
  const int current_idx = amber::UndoStack.index();  // 0..count

  // Gray out all items beyond the current index (the "future" redo branch),
  // and bold the current (active) row.
  const int total = tree_->topLevelItemCount();
  for (int r = 0; r < total; ++r) {
    QTreeWidgetItem* item = tree_->topLevelItem(r);
    if (!item) continue;

    const bool is_future  = (r > current_idx);
    const bool is_current = (r == current_idx);

    // Dim future entries
    const QColor text_color = is_future
        ? QColor(Qt::gray)
        : tree_->palette().color(QPalette::Text);
    for (int col = 0; col < tree_->columnCount(); ++col) {
      item->setForeground(col, text_color);
    }

    // Bold the active entry
    QFont f = item->font(kColAction);
    f.setBold(is_current);
    item->setFont(kColAction, f);
  }

  // Scroll the current entry into view and select it.
  if (current_idx >= 0 && current_idx < total) {
    QTreeWidgetItem* cur = tree_->topLevelItem(current_idx);
    tree_->setCurrentItem(cur);
    tree_->scrollToItem(cur);
  }
}
