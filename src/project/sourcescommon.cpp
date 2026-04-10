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

#include "sourcescommon.h"

#include <QAbstractItemView>
#include <QDebug>
#include <QDesktopServices>
#include <QMessageBox>
#include <QMimeData>
#include <QProcess>

#include "dialogs/proxydialog.h"
#include "engine/sequence.h"
#include "engine/undo/undo.h"
#include "engine/undo/undostack.h"
#include "global/config.h"
#include "global/global.h"
#include "panels/panels.h"
#include "panels/project.h"
#include "panels/timeline.h"
#include "panels/viewer.h"
#include "project/footage.h"
#include "project/media.h"
#include "project/projectfilter.h"
#include "project/proxygenerator.h"
#include "rendering/renderfunctions.h"
#include "ui/mainwindow.h"
#include "ui/menu.h"
#include "ui/menuhelper.h"
#include "ui/viewerwidget.h"

SourcesCommon::SourcesCommon(Project* parent, ProjectFilter& sort_filter)
    :

      project_parent(parent),
      sort_filter_(sort_filter) {
  rename_timer.setInterval(1000);
  connect(&rename_timer, &QTimer::timeout, this, &SourcesCommon::rename_interval);
}

void SourcesCommon::create_seq_from_selected() {
  if (!selected_items.isEmpty()) {
    QVector<amber::timeline::MediaImportData> media_list;
    for (const auto& selected_item : selected_items) {
      media_list.append(project_parent->item_to_media(selected_item));
    }

    ComboAction* ca = new ComboAction(tr("Create Sequence"));
    SequencePtr s = create_sequence_from_media(media_list);

    // add clips to it
    panel_timeline->create_ghosts_from_media(s.get(), 0, media_list);
    panel_timeline->add_clips_from_ghosts(ca, s.get());

    project_parent->create_sequence_internal(ca, s, true, nullptr);
    amber::UndoStack.push(ca);
  }
}

void SourcesCommon::build_proxy_submenu(Menu& menu) {
  QAction* delete_footage_from_sequences = menu.addAction(tr("Delete All Clips Using This Media"));
  QObject::connect(delete_footage_from_sequences, &QAction::triggered, project_parent,
                   &Project::delete_clips_using_selected_media);

  Menu* proxies = new Menu(tr("Proxy"));
  menu.addMenu(proxies);

  if (cached_selected_footage.size() == 1 && cached_selected_footage.at(0)->to_footage()->proxy &&
      cached_selected_footage.at(0)->to_footage()->proxy_path.isEmpty()) {
    QAction* action =
        proxies->addAction(tr("Generating proxy: %1% complete")
                               .arg(amber::proxy_generator.get_proxy_progress(cached_selected_footage.at(0))));
    action->setEnabled(false);
    return;
  }

  bool has_proxy = false, no_proxy = false;
  for (auto i : cached_selected_footage) {
    if (i->to_footage()->proxy)
      has_proxy = true;
    else
      no_proxy = true;
  }

  if (no_proxy) {
    proxies->addAction(has_proxy ? tr("Create/Modify Proxy") : tr("Create Proxy"), this,
                       &SourcesCommon::open_create_proxy_dialog);
  }
  if (has_proxy) {
    if (!no_proxy) proxies->addAction(tr("Modify Proxy"), this, &SourcesCommon::open_create_proxy_dialog);
    proxies->addAction(tr("Restore Original"), this, &SourcesCommon::clear_proxies_from_selected);
  }
}

void SourcesCommon::show_context_menu(QWidget* parent, const QModelIndexList& items) {
  Menu menu(parent);

  selected_items = items;

  QAction* import_action = menu.addAction(tr("Import..."));
  QObject::connect(import_action, &QAction::triggered, project_parent, &Project::import_dialog);

  Menu* new_menu = new Menu(tr("New"));
  menu.addMenu(new_menu);
  amber::MenuHelper.make_new_menu(new_menu);

  Menu* view_menu = new Menu(tr("View"));
  menu.addMenu(view_menu);

  QAction* tree_view_action = view_menu->addAction(tr("Tree View"));
  connect(tree_view_action, &QAction::triggered, project_parent, &Project::set_tree_view);
  QAction* icon_view_action = view_menu->addAction(tr("Icon View"));
  connect(icon_view_action, &QAction::triggered, project_parent, &Project::set_icon_view);

  QAction* toolbar_action = view_menu->addAction(tr("Show Toolbar"));
  toolbar_action->setCheckable(true);
  toolbar_action->setChecked(project_parent->IsToolbarVisible());
  connect(toolbar_action, &QAction::triggered, project_parent, &Project::SetToolbarVisible);

  QAction* show_sequences = view_menu->addAction(tr("Show Sequences"));
  show_sequences->setCheckable(true);
  show_sequences->setChecked(sort_filter_.get_show_sequences());
  connect(show_sequences, &QAction::triggered, &sort_filter_, &ProjectFilter::set_show_sequences);

  if (items.isEmpty()) {
    menu.exec(QCursor::pos());
    return;
  }

  if (items.size() == 1) {
    Media* first_media = project_parent->item_to_media(items.at(0));
    int type = first_media->get_type();
    if (type == MEDIA_TYPE_FOOTAGE) {
      QAction* replace_action = menu.addAction(tr("Replace/Relink Media"));
      QObject::connect(replace_action, &QAction::triggered, project_parent, &Project::replace_selected_file);
#if defined(Q_OS_WIN)
      QAction* reveal_in_explorer = menu.addAction(tr("Reveal in Explorer"));
#elif defined(Q_OS_MAC)
      QAction* reveal_in_explorer = menu.addAction(tr("Reveal in Finder"));
#else
      QAction* reveal_in_explorer = menu.addAction(tr("Reveal in File Manager"));
#endif
      QObject::connect(reveal_in_explorer, &QAction::triggered, this, &SourcesCommon::reveal_in_browser);
    }
    if (type != MEDIA_TYPE_FOLDER) {
      QAction* replace_clip_media = menu.addAction(tr("Replace Clips Using This Media"));
      QObject::connect(replace_clip_media, &QAction::triggered, project_parent, &Project::replace_clip_media);
    }
  }

  bool all_sequences = true, all_footage = true;
  cached_selected_footage.clear();
  for (const auto& item : items) {
    Media* m = project_parent->item_to_media(item);
    if (m->get_type() != MEDIA_TYPE_SEQUENCE) all_sequences = false;
    if (m->get_type() == MEDIA_TYPE_FOOTAGE)
      cached_selected_footage.append(m);
    else
      all_footage = false;
  }

  QAction* create_seq_from = menu.addAction(tr("Create Sequence With This Media"));
  QObject::connect(create_seq_from, &QAction::triggered, this, &SourcesCommon::create_seq_from_selected);

  if (all_sequences) {
    QAction* duplicate_action = menu.addAction(tr("Duplicate"));
    QObject::connect(duplicate_action, &QAction::triggered, project_parent, &Project::duplicate_selected);
  }

  if (all_footage) {
    build_proxy_submenu(menu);
  }

  QAction* delete_action = menu.addAction(tr("Delete"));
  QObject::connect(delete_action, &QAction::triggered, project_parent, &Project::delete_selected_media);

  if (items.size() == 1) {
    Media* media_item = project_parent->item_to_media(items.at(0));
    if (media_item->get_type() != MEDIA_TYPE_FOLDER) {
      QAction* preview_action =
          menu.addAction(tr("Preview in Media Viewer"), this, &SourcesCommon::OpenSelectedMediaInMediaViewerFromAction);
      preview_action->setData(reinterpret_cast<quintptr>(media_item));
    }
    QAction* properties_action = menu.addAction(tr("Properties..."));
    QObject::connect(properties_action, &QAction::triggered, project_parent, &Project::open_properties);
  }

  menu.exec(QCursor::pos());
}

void SourcesCommon::mousePressEvent(QMouseEvent*) { stop_rename_timer(); }

void SourcesCommon::item_click(Media* m, const QModelIndex& index) {
  if (editing_item == m) {
    rename_timer.start();
  } else {
    editing_item = m;
    editing_index = index;
  }
}

void SourcesCommon::mouseDoubleClickEvent(const QModelIndexList& selected_items) {
  stop_rename_timer();
  if (selected_items.size() == 0) {
    project_parent->import_dialog();
  } else if (selected_items.size() == 1) {
    Media* media = project_parent->item_to_media(selected_items.at(0));
    if (media->get_type() == MEDIA_TYPE_SEQUENCE) {
      auto* cmd = new ChangeSequenceAction(media->to_sequence());
      cmd->setText(tr("Open Sequence"));
      amber::UndoStack.push(cmd);
    } else {
      OpenSelectedMediaInMediaViewer(project_parent->item_to_media(selected_items.at(0)));
    }
  }
}

void SourcesCommon::handle_url_drop(QWidget* parent, QDropEvent* event, const QModelIndex& drop_item, MediaPtr m,
                                    const QList<QUrl>& urls) {
  if (urls.isEmpty()) return;

  QStringList paths;
  for (const auto& url : urls) {
    paths.append(url.toLocalFile());
  }

  bool replace = false;
  if (urls.size() == 1 && drop_item.isValid() && m->get_type() == MEDIA_TYPE_FOOTAGE &&
      !QFileInfo(paths.at(0)).isDir() && amber::CurrentConfig.drop_on_media_to_replace &&
      QMessageBox::question(
          parent, tr("Replace Media"),
          tr("You dropped a file onto '%1'. Would you like to replace it with the dropped file?").arg(m->get_name()),
          QMessageBox::Yes | QMessageBox::No, QMessageBox::No) == QMessageBox::Yes) {
    replace = true;
    project_parent->replace_media(m, paths.at(0));
  }

  if (!replace) {
    QModelIndex folder_parent;
    if (drop_item.isValid()) {
      folder_parent = (m->get_type() == MEDIA_TYPE_FOLDER) ? drop_item : drop_item.parent();
    }
    project_parent->process_file_list(paths, false, nullptr, panel_project->item_to_media(folder_parent));
  }
  event->acceptProposedAction();
}

void SourcesCommon::handle_internal_drop(const QModelIndex& drop_item, MediaPtr m, const QModelIndexList& items) {
  if (drop_item.isValid() && m->get_type() != MEDIA_TYPE_FOLDER) return;

  QVector<MediaPtr> move_items;
  for (int i = 0; i < items.size(); i++) {
    const QModelIndex& item = items.at(i);
    const QModelIndex& item_parent = item.parent();
    MediaPtr s = project_parent->item_to_media_ptr(item);
    if (item_parent == drop_item || item == drop_item) continue;

    bool ignore = false;
    if (item_parent.isValid()) {
      QModelIndex par = item_parent;
      while (par.isValid() && !ignore) {
        for (const auto& other : items) {
          if (par == other) {
            ignore = true;
            break;
          }
        }
        par = par.parent();
      }
    }
    if (!ignore) move_items.append(s);
  }

  if (!move_items.isEmpty()) {
    MediaMove* mm = new MediaMove();
    mm->setText(tr("Move Media"));
    mm->to = m.get();
    mm->items = move_items;
    amber::UndoStack.push(mm);
  }
}

void SourcesCommon::dropEvent(QWidget* parent, QDropEvent* event, const QModelIndex& drop_item,
                              const QModelIndexList& items) {
  const QMimeData* mimeData = event->mimeData();
  MediaPtr m = project_parent->item_to_media_ptr(drop_item);
  if (mimeData->hasUrls()) {
    handle_url_drop(parent, event, drop_item, m, mimeData->urls());
  } else {
    event->ignore();
    handle_internal_drop(drop_item, m, items);
  }
}

void SourcesCommon::reveal_in_browser() {
  Media* media = project_parent->item_to_media(selected_items.at(0));
  Footage* m = media->to_footage();

#if defined(Q_OS_WIN)
  QStringList args;
  args << "/select," << QDir::toNativeSeparators(m->url);
  QProcess::startDetached("explorer", args);
#elif defined(Q_OS_MAC)
  QStringList args;
  args << "-e";
  args << "tell application \"Finder\"";
  args << "-e";
  args << "activate";
  args << "-e";
  args << "select POSIX file \"" + m->url + "\"";
  args << "-e";
  args << "end tell";
  QProcess::startDetached("osascript", args);
#else
  QDesktopServices::openUrl(QUrl::fromLocalFile(m->url.left(m->url.lastIndexOf('/'))));
#endif
}

void SourcesCommon::stop_rename_timer() { rename_timer.stop(); }

void SourcesCommon::rename_interval() {
  stop_rename_timer();
  if (view->hasFocus() && editing_item != nullptr) {
    view->edit(editing_index);
  }
}

void SourcesCommon::item_renamed(Media* item) {
  if (editing_item == item) {
    MediaRename* mr = new MediaRename(item, "idk");
    mr->setText(tr("Rename Media"));
    amber::UndoStack.push(mr);
    editing_item = nullptr;
  }
}

void SourcesCommon::OpenSelectedMediaInMediaViewerFromAction() {
  OpenSelectedMediaInMediaViewer(reinterpret_cast<Media*>(static_cast<QAction*>(sender())->data().value<quintptr>()));
}

void SourcesCommon::OpenSelectedMediaInMediaViewer(Media* item) {
  if (item->get_type() != MEDIA_TYPE_FOLDER) {
    panel_footage_viewer->set_media(item);
    panel_footage_viewer->setFocus();
  }
}

void SourcesCommon::open_create_proxy_dialog() {
  // open the proxy dialog and send it a list of currently selected footage
  ProxyDialog pd(amber::MainWindow, cached_selected_footage);
  pd.exec();
}

void SourcesCommon::clear_proxies_from_selected() {
  QList<QString> delete_list;

  for (auto i : cached_selected_footage) {
    Footage* f = i->to_footage();

    if (f->proxy && !f->proxy_path.isEmpty()) {
      if (QFileInfo::exists(f->proxy_path)) {
        if (QMessageBox::question(amber::MainWindow, tr("Delete proxy"),
                                  tr("Would you like to delete the proxy file \"%1\" as well?").arg(f->proxy_path),
                                  QMessageBox::Yes | QMessageBox::No) == QMessageBox::Yes) {
          delete_list.append(f->proxy_path);
        }
      }
    }

    f->proxy = false;
    f->proxy_path.clear();
  }

  if (amber::ActiveSequence != nullptr) {
    // close all clips so we can delete any proxies requested to be deleted
    close_active_clips(amber::ActiveSequence.get());
  }

  // delete proxies requested to be deleted
  for (const auto& i : delete_list) {
    QFile::remove(i);
  }

  if (amber::ActiveSequence != nullptr) {
    // update viewer (will re-open active clips with original media)
    panel_sequence_viewer->viewer_widget->frame_update();
  }

  amber::Global->set_modified(true);
}
