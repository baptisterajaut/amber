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

#include "actionsearch.h"

#include <QVBoxLayout>
#include <QKeyEvent>
#include <QMenuBar>
#include <QLabel>
#include <QSettings>
#include <algorithm>

#include "ui/mainwindow.h"

ActionSearch::ActionSearch(QWidget *parent) :
  QDialog(parent)
{
  // ActionSearch requires a parent widget
  Q_ASSERT(parent != nullptr);

  // Set styling (object name is required for CSS specific to this object)
  setObjectName("ASDiag");
  setStyleSheet("#ASDiag{border: 2px solid #808080;}");

  // Size proportionally to the parent (usually MainWindow).
  resize(parent->width()/3, parent->height()/3);

  // Show dialog as a "popup", which will make the dialog close if the user clicks out of it.
  setWindowFlags(Qt::Popup);

  QVBoxLayout* layout = new QVBoxLayout(this);

  // Construct the main entry text field.
  ActionSearchEntry* entry_field = new ActionSearchEntry(this);

  // Set the main entry field font size to 1.2x its standard font size.
  QFont entry_field_font = entry_field->font();
  entry_field_font.setPointSize(qRound(entry_field_font.pointSize()*1.2));
  entry_field->setFont(entry_field_font);

  // Set placeholder text for the main entry field
  entry_field->setPlaceholderText(tr("Search for action..."));

  // Connect signals/slots
  connect(entry_field, &QLineEdit::textChanged, this, [this](const QString& s){ search_update(s); });
  connect(entry_field, &QLineEdit::returnPressed, this, &ActionSearch::perform_action);

  // moveSelectionUp() and moveSelectionDown() are emitted when the user pressed up or down on the text field.
  // We override it here to select the upper or lower item in the list.
  connect(entry_field, &ActionSearchEntry::moveSelectionUp, this, &ActionSearch::move_selection_up);
  connect(entry_field, &ActionSearchEntry::moveSelectionDown, this, &ActionSearch::move_selection_down);
  layout->addWidget(entry_field);

  // Construct list of actions
  list_widget = new ActionSearchList(this);

  // Set list's font to 1.2x its standard font size
  QFont list_widget_font = list_widget->font();
  list_widget_font.setPointSize(qRound(list_widget_font.pointSize()*1.2));
  list_widget->setFont(list_widget_font);

  layout->addWidget(list_widget);

  connect(list_widget, &ActionSearchList::dbl_click, this, &ActionSearch::perform_action);

  // Instantly focus on the entry field to allow for fully keyboard operation (if this popup was initiated by keyboard
  // shortcut for example).
  entry_field->setFocus();

  search_update("");
}

void ActionSearch::search_update(const QString &s, const QString &p, QMenu *parent) {
  Q_UNUSED(p);
  Q_UNUSED(parent);

  list_widget->clear();

  QList<SearchResultAction> results;
  QList<QAction*> menus = amber::MainWindow->menuBar()->actions();
  for (auto i : menus) {
    QMenu* menu = i->menu();
    if (menu != nullptr) {
      collect_actions(s, "", menu, results);
    }
  }

  // Sort the collected actions by usage_count (descending)
  std::stable_sort(results.begin(), results.end(), [](const SearchResultAction &a, const SearchResultAction &b) {
    return a.usage_count > b.usage_count;
  });

  // Populate the list_widget with sorted results
  for (const auto& res : results) {
    QListWidgetItem* item = new QListWidgetItem(QString("%1\n(%2)").arg(res.comp, res.menu_text), list_widget);
    item->setData(Qt::UserRole+1, reinterpret_cast<quintptr>(res.action));
    list_widget->addItem(item);
  }

  if (list_widget->count() > 0) {
    list_widget->item(0)->setSelected(true);
  }
}

void ActionSearch::collect_actions(const QString &s, const QString &p, QMenu *parent, QList<SearchResultAction> &results) {
  if (parent == nullptr) return;

  QString menu_text;
  if (!p.isEmpty()) menu_text += p + " > ";
  menu_text += parent->title().replace("&", "");

  QList<QAction*> actions = parent->actions();
  for (auto a : actions) {
    if (!a->isSeparator()) {
      if (a->menu() != nullptr) {
        collect_actions(s, menu_text, a->menu(), results);
      } else {
        QString comp = a->text().replace("&", "");
        if (s.isEmpty() || comp.contains(s, Qt::CaseInsensitive)) {
          // Retrieve usage count from QSettings
          QString action_id = a->property("id").toString();
          if (action_id.isEmpty()) {
            action_id = a->text();
          }
          int usage_count = 0;
          if (!action_id.isEmpty()) {
            QSettings settings;
            settings.beginGroup("action_search_usage");
            usage_count = settings.value(action_id, 0).toInt();
            settings.endGroup();
          }

          SearchResultAction res;
          res.action = a;
          res.comp = comp;
          res.menu_text = menu_text;
          res.usage_count = usage_count;
          results.append(res);
        }
      }
    }
  }
}

void ActionSearch::perform_action() {

  // Loop over all the items in the list and if we find one that's selected, we trigger it.
  QList<QListWidgetItem*> selected_items = list_widget->selectedItems();
  if (list_widget->count() > 0 && selected_items.size() > 0) {

    QListWidgetItem* item = selected_items.at(0);

    // Get QAction pointer from item's data
    QAction* a = reinterpret_cast<QAction*>(item->data(Qt::UserRole+1).value<quintptr>());

    // Save usage count to QSettings
    QString action_id = a->property("id").toString();
    if (action_id.isEmpty()) {
      action_id = a->text();
    }
    if (!action_id.isEmpty()) {
      QSettings settings;
      settings.beginGroup("action_search_usage");
      int count = settings.value(action_id, 0).toInt();
      settings.setValue(action_id, count + 1);
      settings.endGroup();
    }

    a->trigger();

  }

  // Close this popup
  accept();

}

void ActionSearch::move_selection_up() {

  // Here we loop over all the items to find the currently selected one, and then select the one above it. We start
  // iterating at 1 (instead of 0) to efficiently ignore the first item (since the selection can't go below the very
  // bottom item).

  int lim = list_widget->count();
  for (int i=1;i<lim;i++) {
    if (list_widget->item(i)->isSelected()) {
      list_widget->item(i-1)->setSelected(true);
      list_widget->scrollToItem(list_widget->item(i-1));
      break;
    }
  }
}

void ActionSearch::move_selection_down() {

  // Here we loop over all the items to find the currently selected one, and then select the one below it. We limit it
  // one entry before count() to efficiently ignore the item at the end (since the selection can't go below the very
  // bottom item).

  int lim = list_widget->count()-1;
  for (int i=0;i<lim;i++) {
    if (list_widget->item(i)->isSelected()) {
      list_widget->item(i+1)->setSelected(true);
      list_widget->scrollToItem(list_widget->item(i+1));
      break;
    }
  }
}

ActionSearchEntry::ActionSearchEntry(QWidget *parent) : QLineEdit(parent) {}

void ActionSearchEntry::keyPressEvent(QKeyEvent * event) {

  // Listen for up/down, otherwise pass the key event to the base class.

  switch (event->key()) {
  case Qt::Key_Up:
    emit moveSelectionUp();
    break;
  case Qt::Key_Down:
    emit moveSelectionDown();
    break;
  default:
    QLineEdit::keyPressEvent(event);
  }

}

ActionSearchList::ActionSearchList(QWidget *parent) : QListWidget(parent) {}

void ActionSearchList::mouseDoubleClickEvent(QMouseEvent *) {

  // Indiscriminately emit a signal on any double click
  emit dbl_click();

}
