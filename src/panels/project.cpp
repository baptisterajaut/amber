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

#include "project.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
}

#include <QApplication>
#include <QFileDialog>
#include <QString>
#include <QVariant>

#include <QDropEvent>
#include <QInputDialog>
#include <QMessageBox>
#include <QMimeData>
#include <QPushButton>
#include <QSizePolicy>
#include <QVBoxLayout>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>

#include "dialogs/loaddialog.h"
#include "dialogs/mediapropertiesdialog.h"
#include "dialogs/newsequencedialog.h"
#include "dialogs/replaceclipmediadialog.h"
#include "global/config.h"
#include "global/debug.h"
#include "global/global.h"
#include "panels.h"
#include "panels/effectcontrols.h"
#include "project/clipboard.h"
#include "project/previewgenerator.h"
#include "project/projectfilter.h"
#include "project/sourcescommon.h"
#include "engine/cacher.h"
#include "rendering/renderfunctions.h"
#include "ui/icons.h"
#include "ui/mainwindow.h"
#include "ui/mediaiconservice.h"
#include "ui/menu.h"
#include "ui/menuhelper.h"
#include "ui/sourceiconview.h"
#include "ui/sourcetable.h"
#include "engine/undo/undo.h"
#include "engine/undo/undostack.h"

constexpr int MAXIMUM_RECENT_PROJECTS = 10;  // FIXME: should be configurable

QString autorecovery_filename;
QStringList recent_projects;

Project::Project(QWidget* parent) : Panel(parent), sorter(this), sources_common(this, sorter) {
  QWidget* dockWidgetContents = new QWidget(this);

  QVBoxLayout* verticalLayout = new QVBoxLayout(dockWidgetContents);
  verticalLayout->setContentsMargins(0, 0, 0, 0);
  verticalLayout->setSpacing(0);

  setWidget(dockWidgetContents);

  ConnectFilterToModel();

  // optional toolbar
  toolbar_widget = new QWidget();
  toolbar_widget->setVisible(amber::CurrentConfig.show_project_toolbar);
  toolbar_widget->setObjectName("project_toolbar");

  QHBoxLayout* toolbar = new QHBoxLayout(toolbar_widget);
  toolbar->setContentsMargins(0, 0, 0, 0);
  toolbar->setSpacing(0);

  QPushButton* toolbar_new = new QPushButton();
  toolbar_new->setIcon(amber::icon::CreateIconFromSVG(QStringLiteral(":/icons/add-button.svg")));
  toolbar_new->setToolTip(tr("New"));
  connect(toolbar_new, &QPushButton::clicked, this, &Project::make_new_menu);
  toolbar->addWidget(toolbar_new);

  QPushButton* toolbar_open = new QPushButton();
  toolbar_open->setIcon(amber::icon::CreateIconFromSVG(QStringLiteral(":/icons/open.svg")));
  toolbar_open->setToolTip(tr("Open Project"));
  connect(toolbar_open, &QPushButton::clicked, amber::Global.get(), &OliveGlobal::OpenProject);
  toolbar->addWidget(toolbar_open);

  QPushButton* toolbar_save = new QPushButton();
  toolbar_save->setIcon(amber::icon::CreateIconFromSVG(QStringLiteral(":/icons/save.svg")));
  toolbar_save->setToolTip(tr("Save Project"));
  connect(toolbar_save, &QPushButton::clicked, amber::Global.get(), &OliveGlobal::save_project);
  toolbar->addWidget(toolbar_save);

  QPushButton* toolbar_undo = new QPushButton();
  toolbar_undo->setIcon(amber::icon::CreateIconFromSVG(QStringLiteral(":/icons/undo.svg")));
  toolbar_undo->setToolTip(tr("Undo"));
  connect(toolbar_undo, &QPushButton::clicked, amber::Global.get(), &OliveGlobal::undo);
  toolbar->addWidget(toolbar_undo);

  QPushButton* toolbar_redo = new QPushButton();
  toolbar_redo->setIcon(amber::icon::CreateIconFromSVG(QStringLiteral(":/icons/redo.svg")));
  toolbar_redo->setToolTip(tr("Redo"));
  connect(toolbar_redo, &QPushButton::clicked, amber::Global.get(), &OliveGlobal::redo);
  toolbar->addWidget(toolbar_redo);

  toolbar_search = new QLineEdit();
  toolbar_search->setClearButtonEnabled(true);
  connect(toolbar_search, &QLineEdit::textChanged, &sorter, &ProjectFilter::update_search_filter);
  toolbar->addWidget(toolbar_search);

  QPushButton* toolbar_tree_view = new QPushButton();
  toolbar_tree_view->setIcon(amber::icon::CreateIconFromSVG(QStringLiteral(":/icons/treeview.svg")));
  toolbar_tree_view->setToolTip(tr("Tree View"));
  connect(toolbar_tree_view, &QPushButton::clicked, this, &Project::set_tree_view);
  toolbar->addWidget(toolbar_tree_view);

  QPushButton* toolbar_icon_view = new QPushButton();
  toolbar_icon_view->setIcon(amber::icon::CreateIconFromSVG(QStringLiteral(":/icons/iconview.svg")));
  toolbar_icon_view->setToolTip(tr("Icon View"));
  connect(toolbar_icon_view, &QPushButton::clicked, this, &Project::set_icon_view);
  toolbar->addWidget(toolbar_icon_view);

  QPushButton* toolbar_list_view = new QPushButton();
  toolbar_list_view->setIcon(amber::icon::CreateIconFromSVG(QStringLiteral(":/icons/listview.svg")));
  toolbar_list_view->setToolTip(tr("List View"));
  connect(toolbar_list_view, &QPushButton::clicked, this, &Project::set_list_view);
  toolbar->addWidget(toolbar_list_view);

  verticalLayout->addWidget(toolbar_widget);

  // tree view
  tree_view = new SourceTable(sources_common);
  tree_view->project_parent = this;
  tree_view->setModel(&sorter);
  verticalLayout->addWidget(tree_view);

  // Set the first column width
  // I'm not sure if there's a better way to do this, default behavior seems to have all columns fixed width
  // and let the last column fill up the remainder when really the opposite would be preferable (having the
  // first column fill up the majority of the space). Anyway, this will probably do for now.
  tree_view->setColumnWidth(0, tree_view->width() / 2);

  // icon view
  icon_view_container = new QWidget();

  QVBoxLayout* icon_view_container_layout = new QVBoxLayout(icon_view_container);
  icon_view_container_layout->setContentsMargins(0, 0, 0, 0);
  icon_view_container_layout->setSpacing(0);

  QHBoxLayout* icon_view_controls = new QHBoxLayout();
  icon_view_controls->setContentsMargins(0, 0, 0, 0);
  icon_view_controls->setSpacing(0);

  directory_up = new QPushButton();
  directory_up->setIcon(amber::icon::CreateIconFromSVG(QStringLiteral(":/icons/dirup.svg")));
  directory_up->setEnabled(false);
  icon_view_controls->addWidget(directory_up);

  icon_view_controls->addStretch();

  icon_size_slider = new QSlider(Qt::Horizontal);
  icon_size_slider->setMinimum(16);
  icon_size_slider->setMaximum(256);
  icon_view_controls->addWidget(icon_size_slider);
  connect(icon_size_slider, &QSlider::valueChanged, this, &Project::set_icon_view_size);

  icon_view_container_layout->addLayout(icon_view_controls);

  icon_view = new SourceIconView(sources_common);
  icon_view->project_parent = this;
  icon_view->setModel(&sorter);
  icon_view->setGridSize(QSize(100, 100));
  icon_view->setViewMode(QListView::IconMode);
  icon_view->setUniformItemSizes(true);
  icon_view_container_layout->addWidget(icon_view);

  icon_size_slider->setValue(icon_view->gridSize().height());

  verticalLayout->addWidget(icon_view_container);

  connect(directory_up, &QPushButton::clicked, this, &Project::go_up_dir);
  connect(icon_view, &SourceIconView::changed_root, this, &Project::set_up_dir_enabled);

  connect(amber::media_icon_service.get(), &MediaIconService::IconChanged, icon_view->viewport(),
          qOverload<>(&QWidget::update));
  connect(amber::media_icon_service.get(), &MediaIconService::IconChanged, tree_view->viewport(),
          qOverload<>(&QWidget::update));

  // SetInt-based undo actions (e.g. color labels) mutate fields silently with no
  // dataChanged signal, so the views never re-query Media::data() on push/undo/redo.
  // Force a viewport repaint whenever the undo stack moves.
  connect(&amber::UndoStack, &QUndoStack::indexChanged, tree_view->viewport(),
          qOverload<>(&QWidget::update));
  connect(&amber::UndoStack, &QUndoStack::indexChanged, icon_view->viewport(),
          qOverload<>(&QWidget::update));

  update_view_type();

  Retranslate();
}

void Project::ConnectFilterToModel() { sorter.setSourceModel(&amber::project_model); }

void Project::DisconnectFilterToModel() { sorter.setSourceModel(nullptr); }

void Project::Retranslate() {
  toolbar_search->setPlaceholderText(tr("Search media, markers, etc."));
  setWindowTitle(tr("Project"));
}

QString Project::get_next_sequence_name(QString start) {
  if (start.isEmpty()) start = tr("Sequence");

  int n = 1;
  bool found = true;
  QString name;
  while (found) {
    found = false;
    name = start + " ";
    if (n < 10) {
      name += "0";
    }
    name += QString::number(n);
    for (int i = 0; i < amber::project_model.childCount(); i++) {
      if (QString::compare(amber::project_model.child(i)->get_name(), name, Qt::CaseInsensitive) == 0) {
        found = true;
        n++;
        break;
      }
    }
  }
  return name;
}

SequencePtr create_sequence_from_media(QVector<amber::timeline::MediaImportData>& media_list) {
  SequencePtr s(new Sequence());

  s->name = panel_project->get_next_sequence_name();

  // Retrieve default Sequence settings from Config
  s->width = amber::CurrentConfig.default_sequence_width;
  s->height = amber::CurrentConfig.default_sequence_height;
  s->frame_rate = amber::CurrentConfig.default_sequence_framerate;
  s->audio_frequency = amber::CurrentConfig.default_sequence_audio_frequency;
  s->audio_layout = amber::CurrentConfig.default_sequence_audio_channel_layout;

  bool got_video_values = false;
  bool got_audio_values = false;
  for (auto i : media_list) {
    Media* media = i.media();
    switch (media->get_type()) {
      case MEDIA_TYPE_FOOTAGE: {
        Footage* m = media->to_footage();
        if (m->ready) {
          if (!got_video_values) {
            for (int j = 0; j < m->video_tracks.size(); j++) {
              const FootageStream& ms = m->video_tracks.at(j);
              s->width = ms.video_width;
              s->height = ms.video_height;
              if (!qFuzzyCompare(ms.video_frame_rate, 0.0)) {
                s->frame_rate = ms.video_frame_rate * m->speed;

                if (ms.video_interlacing != VIDEO_PROGRESSIVE) s->frame_rate *= 2;

                // only break with a decent frame rate, otherwise there may be a better candidate
                got_video_values = true;
                break;
              }
            }
          }
          if (!got_audio_values && m->audio_tracks.size() > 0) {
            const FootageStream& ms = m->audio_tracks.at(0);
            s->audio_frequency = ms.audio_frequency;
            got_audio_values = true;
          }
        }
      } break;
      case MEDIA_TYPE_SEQUENCE: {
        // Clone all attributes of the original sequence (seq) into the new one (s)
        Sequence* seq = media->to_sequence().get();

        s->width = seq->width;
        s->height = seq->height;
        s->frame_rate = seq->frame_rate;
        s->audio_frequency = seq->audio_frequency;
        s->audio_layout = seq->audio_layout;

        got_video_values = true;
        got_audio_values = true;
      } break;
    }
    if (got_video_values && got_audio_values) break;
  }

  return s;
}

void Project::duplicate_selected() {
  QModelIndexList items = get_current_selected();
  bool duped = false;
  ComboAction* ca = new ComboAction(tr("Duplicate Sequence"));
  for (const auto& item : items) {
    Media* i = item_to_media(item);
    if (i->get_type() == MEDIA_TYPE_SEQUENCE) {
      create_sequence_internal(ca, i->to_sequence()->copy(), false, item_to_media(item.parent()));
      duped = true;
    }
  }
  if (duped) {
    amber::UndoStack.push(ca);
  } else {
    delete ca;
  }
}

void Project::replace_selected_file() {
  QModelIndexList selected_items = get_current_selected();
  if (selected_items.size() == 1) {
    MediaPtr item = item_to_media_ptr(selected_items.at(0));
    if (item->get_type() == MEDIA_TYPE_FOOTAGE) {
      replace_media(item, nullptr);
    }
  }
}

void Project::replace_media(MediaPtr item, QString filename) {
  if (filename.isEmpty()) {
    filename =
        QFileDialog::getOpenFileName(this, tr("Replace '%1'").arg(item->get_name()), "", tr("All Files") + " (*)");
  }
  if (!filename.isEmpty()) {
    ReplaceMediaCommand* rmc = new ReplaceMediaCommand(item, filename);
    rmc->setText(tr("Replace Media"));
    amber::UndoStack.push(rmc);
  }
}

void Project::replace_clip_media() {
  if (amber::ActiveSequence == nullptr) {
    QMessageBox::critical(this, tr("No active sequence"),
                          tr("No sequence is active, please open the sequence you want to replace clips from."),
                          QMessageBox::Ok);
  } else {
    QModelIndexList selected_items = get_current_selected();
    if (selected_items.size() == 1) {
      Media* item = item_to_media(selected_items.at(0));
      if (item->get_type() == MEDIA_TYPE_SEQUENCE && amber::ActiveSequence == item->to_sequence()) {
        QMessageBox::critical(
            this, tr("Active sequence selected"),
            tr("You cannot insert a sequence into itself, so no clips of this media would be in this sequence."),
            QMessageBox::Ok);
      } else {
        ReplaceClipMediaDialog dialog(this, item);
        dialog.exec();
      }
    }
  }
}

void Project::open_properties() {
  QModelIndexList selected_items = get_current_selected();
  if (selected_items.size() == 1) {
    Media* item = item_to_media(selected_items.at(0));
    switch (item->get_type()) {
      case MEDIA_TYPE_FOOTAGE: {
        MediaPropertiesDialog mpd(this, item);
        mpd.exec();
      } break;
      case MEDIA_TYPE_SEQUENCE: {
        NewSequenceDialog nsd(this, item);
        nsd.exec();
      } break;
      default: {
        // fall back to renaming
        QString new_name = QInputDialog::getText(this, tr("Rename '%1'").arg(item->get_name()), tr("Enter new name:"),
                                                 QLineEdit::Normal, item->get_name());
        if (!new_name.isEmpty()) {
          MediaRename* mr = new MediaRename(item, new_name);
          mr->setText(tr("Rename Media"));
          amber::UndoStack.push(mr);
        }
      }
    }
  }
}

void Project::new_folder() {
  MediaPtr m = create_folder_internal(nullptr);
  auto* cmd = new AddMediaCommand(m, get_selected_folder());
  cmd->setText(tr("New Folder"));
  amber::UndoStack.push(cmd);

  QModelIndex index = amber::project_model.create_index(m->row(), 0, m.get());
  switch (amber::CurrentConfig.project_view_type) {
    case amber::PROJECT_VIEW_TREE:
      tree_view->edit(sorter.mapFromSource(index));
      break;
    case amber::PROJECT_VIEW_ICON:
      icon_view->edit(sorter.mapFromSource(index));
      break;
  }
}

void Project::new_sequence() {
  NewSequenceDialog nsd(this);
  nsd.set_sequence_name(get_next_sequence_name());
  nsd.exec();
}

MediaPtr Project::create_sequence_internal(ComboAction* ca, SequencePtr s, bool open, Media* parent) {
  MediaPtr item = std::make_shared<Media>();
  item->set_sequence(s);

  if (ca != nullptr) {
    ca->append(new AddMediaCommand(item, parent));

    if (open) {
      ca->append(new ChangeSequenceAction(s));
    }

  } else {
    amber::project_model.appendChild(parent, item);

    if (open) {
      amber::Global->set_sequence(s);
    }
  }

  return item;
}

QString Project::get_file_name_from_path(const QString& path) { return path.mid(path.lastIndexOf('/') + 1); }

bool Project::is_focused() { return tree_view->hasFocus() || icon_view->hasFocus(); }

MediaPtr Project::create_folder_internal(QString name) {
  MediaPtr item = std::make_shared<Media>();
  item->set_folder();
  item->set_name(name);
  return item;
}

Media* Project::item_to_media(const QModelIndex& index) {
  return static_cast<Media*>(sorter.mapToSource(index).internalPointer());
}

MediaPtr Project::item_to_media_ptr(const QModelIndex& index) {
  Media* raw_ptr = item_to_media(index);

  if (raw_ptr == nullptr) {
    return nullptr;
  }

  return raw_ptr->parentItem()->get_shared_ptr(raw_ptr);
}

void Project::get_all_media_from_table(QList<Media*>& items, QList<Media*>& list, int search_type) {
  for (auto item : items) {
    if (item->get_type() == MEDIA_TYPE_FOLDER) {
      QList<Media*> children;
      for (int j = 0; j < item->childCount(); j++) {
        children.append(item->child(j));
      }
      get_all_media_from_table(children, list, search_type);
    } else if (search_type == item->get_type() || search_type == -1) {
      list.append(item);
    }
  }
}

bool Project::IsToolbarVisible() { return toolbar_widget->isVisible(); }

void Project::SetToolbarVisible(bool visible) { toolbar_widget->setVisible(visible); }

bool Project::IsProjectWidget(QObject* child) { return (child == tree_view || child == icon_view); }

bool delete_clips_in_clipboard_with_media(ComboAction* ca, Media* m) {
  int delete_count = 0;
  if (clipboard_type == CLIPBOARD_TYPE_CLIP) {
    for (int i = 0; i < clipboard.size(); i++) {
      ClipPtr c = std::static_pointer_cast<Clip>(clipboard.at(i));
      if (c->media() == m) {
        ca->append(new RemoveClipsFromClipboard(i - delete_count));
        delete_count++;
      }
    }
  }
  return (delete_count > 0);
}

void Project::delete_selected_media() {
  ComboAction* ca = new ComboAction(tr("Delete Media"));
  QModelIndexList selected_items = get_current_selected();

  QList<Media*> items;
  for (const auto& selected_item : selected_items) {
    items.append(item_to_media(selected_item));
  }

  bool remove = true;
  bool redraw = false;

  // check if media is in use
  QVector<Media*> parents;
  QList<Media*> sequence_items;
  QList<Media*> all_top_level_items;
  for (int i = 0; i < amber::project_model.childCount(); i++) {
    all_top_level_items.append(amber::project_model.child(i));
  }
  get_all_media_from_table(all_top_level_items, sequence_items, MEDIA_TYPE_SEQUENCE);  // find all sequences in project
  if (sequence_items.size() > 0) {
    QList<Media*> media_items;
    get_all_media_from_table(items, media_items, MEDIA_TYPE_FOOTAGE);

    for (int i = 0; i < media_items.size(); i++) {
      Media* item = media_items.at(i);
      Footage* media = item->to_footage();
      bool confirm_delete = false;
      for (int j = 0; j < sequence_items.size(); j++) {
        Sequence* s = sequence_items.at(j)->to_sequence().get();
        for (int k = 0; k < s->clips.size(); k++) {
          ClipPtr c = s->clips.at(k);
          if (c != nullptr && c->media() == item) {
            if (!confirm_delete) {
              // we found a reference, so we know we'll need to ask if the user wants to delete it
              QMessageBox confirm(this);
              confirm.setWindowTitle(tr("Delete media in use?"));
              confirm.setText(tr("The media '%1' is currently used in '%2'. Deleting it will remove all instances in "
                                 "the sequence. Are you sure you want to do this?")
                                  .arg(media->name, s->name));
              QAbstractButton* yes_button = confirm.addButton(QMessageBox::Yes);
              QAbstractButton* skip_button = nullptr;
              if (items.size() > 1) skip_button = confirm.addButton(tr("Skip"), QMessageBox::NoRole);
              QAbstractButton* abort_button = confirm.addButton(QMessageBox::Cancel);
              confirm.exec();
              if (confirm.clickedButton() == yes_button) {
                // remove all clips referencing this media
                confirm_delete = true;
                redraw = true;
              } else if (confirm.clickedButton() == skip_button) {
                // remove media item and any folders containing it from the remove list
                Media* parent = item;
                while (parent != nullptr) {
                  parents.append(parent);

                  // re-add item's siblings
                  for (int m = 0; m < parent->childCount(); m++) {
                    Media* child = parent->child(m);
                    bool found = false;
                    for (auto item : items) {
                      if (item == child) {
                        found = true;
                        break;
                      }
                    }
                    if (!found) {
                      items.append(child);
                    }
                  }

                  parent = parent->parentItem();
                }

                j = sequence_items.size();
                k = s->clips.size();
              } else if (confirm.clickedButton() == abort_button) {
                // break out of loop
                i = media_items.size();
                j = sequence_items.size();
                k = s->clips.size();

                remove = false;
              }
            }
            if (confirm_delete) {
              ca->append(new DeleteClipAction(s, k));
            }
          }
        }
      }
      if (confirm_delete) {
        delete_clips_in_clipboard_with_media(ca, item);
      }
    }
  }

  // remove
  if (remove) {
    panel_graph_editor->set_row(nullptr);
    panel_effect_controls->Clear(true);

    if (amber::ActiveSequence != nullptr) amber::ActiveSequence->selections.clear();

    // remove media and parents
    for (auto parent : parents) {
      for (int l = 0; l < items.size(); l++) {
        if (items.at(l) == parent) {
          items.removeAt(l);
          l--;
        }
      }
    }

    // Check if any sequence being deleted is referenced by other sequences
    for (int i = 0; i < items.size(); i++) {
      Media* item = items.at(i);
      if (item->get_type() != MEDIA_TYPE_SEQUENCE) continue;

      for (int j = 0; j < sequence_items.size(); j++) {
        Media* seq_media = sequence_items.at(j);
        if (items.contains(seq_media)) continue;  // also being deleted, skip

        Sequence* s = seq_media->to_sequence().get();
        for (int k = 0; k < s->clips.size(); k++) {
          ClipPtr c = s->clips.at(k);
          if (c != nullptr && c->media() == item) {
            QMessageBox confirm(this);
            confirm.setWindowTitle(tr("Delete sequence in use?"));
            confirm.setText(tr("The sequence '%1' is used as a nested sequence in '%2'. "
                               "Deleting it will remove all instances. Are you sure?")
                               .arg(item->to_sequence()->name, s->name));
            confirm.addButton(QMessageBox::Yes);
            QAbstractButton* cancel_button = confirm.addButton(QMessageBox::Cancel);
            confirm.exec();
            if (confirm.clickedButton() == cancel_button) {
              delete ca;
              return;
            }
            // User confirmed — delete clips referencing this sequence
            redraw = true;
            for (int m = 0; m < s->clips.size(); m++) {
              ClipPtr mc = s->clips.at(m);
              if (mc != nullptr && mc->media() == item) {
                ca->append(new DeleteClipAction(s, m));
              }
            }
            break;  // Only ask once per referencing sequence
          }
        }
      }
    }

    for (auto item : items) {
      ca->append(new DeleteMediaCommand(item->parentItem()->get_shared_ptr(item)));

      if (item->get_type() == MEDIA_TYPE_SEQUENCE) {
        redraw = true;

        Sequence* s = item->to_sequence().get();

        if (s == amber::ActiveSequence.get()) {
          ca->append(new ChangeSequenceAction(nullptr));
        }

        if (s == panel_footage_viewer->seq.get()) {
          panel_footage_viewer->set_media(nullptr);
        }
      } else if (item->get_type() == MEDIA_TYPE_FOOTAGE) {
        if (panel_footage_viewer->seq != nullptr) {
          for (int j = 0; j < panel_footage_viewer->seq->clips.size(); j++) {
            ClipPtr c = panel_footage_viewer->seq->clips.at(j);
            if (c != nullptr && c->media() == item) {
              panel_footage_viewer->set_media(nullptr);
              break;
            }
          }
        }
      }
    }
    amber::UndoStack.push(ca);

    // redraw clips
    if (redraw) {
      update_ui(true);
    }
  } else {
    delete ca;
  }
}

void Project::process_file_list(QStringList& files, bool recursive, MediaPtr replace, Media* parent) {
  bool imported = false;

  // retrieve the array of image formats from the user's configuration
  QStringList image_sequence_formats = amber::CurrentConfig.img_seq_formats.split("|");

  // a cache of image sequence formatted URLS to assist the user in importing image sequences
  QVector<QString> image_sequence_urls;
  QVector<bool> image_sequence_importassequence;

  if (!recursive) last_imported_media.clear();

  bool create_undo_action = (!recursive && replace == nullptr);
  ComboAction* ca = nullptr;
  if (create_undo_action) ca = new ComboAction(tr("Import Media"));

  // Loop through received files
  for (const auto& i : files) {
    // If this file is a directory, we'll recursively call this function again to process the directory's contents
    if (QFileInfo(i).isDir()) {
      QString folder_name = get_file_name_from_path(i);
      MediaPtr folder = create_folder_internal(folder_name);

      QDir directory(i);
      directory.setFilter(QDir::NoDotAndDotDot | QDir::AllEntries);

      QFileInfoList subdir_files = directory.entryInfoList();
      QStringList subdir_filenames;

      for (const auto& subdir_file : subdir_files) {
        subdir_filenames.append(subdir_file.filePath());
      }

      if (create_undo_action) {
        ca->append(new AddMediaCommand(folder, parent));
      } else {
        amber::project_model.appendChild(parent, folder);
      }

      process_file_list(subdir_filenames, true, nullptr, folder.get());

      imported = true;

    } else if (!i.isEmpty()) {
      QString file = i;

      // Check if the user is importing an Olive project file
      if (file.endsWith(".ove", Qt::CaseInsensitive)) {
        // This file is an Olive project file. Ask the user if they really want to import it.
        if (QMessageBox::question(this, tr("Import a Project"),
                                  tr("\"%1\" is an Olive project file. It will merge with this project. "
                                     "Do you wish to continue?")
                                      .arg(file),
                                  QMessageBox::Yes | QMessageBox::No) == QMessageBox::Yes) {
          // load the project without clearing the current one
          amber::Global->ImportProject(file);
        }

      } else {
        // This file is NOT an Olive project file

        // Used later if this file is part of an already processed image sequence
        bool skip = false;

        /* Heuristic to determine whether file is part of an image sequence */

        // Firstly, we run a heuristic on whether this file is an image by checking its file extension

        bool file_is_an_image = false;

        // Get the string position of the extension in the filename
        int lastcharindex = file.lastIndexOf(".");

        if (lastcharindex != -1 && lastcharindex > file.lastIndexOf('/')) {
          QString ext = file.mid(lastcharindex + 1);

          // If the file extension is part of a predetermined list (from Config::img_seq_formats), we'll treat it
          // as an image
          if (image_sequence_formats.contains(ext, Qt::CaseInsensitive)) {
            file_is_an_image = true;
          }

        } else {
          // If we're here, the file has no extension, but we'll still check if its an image sequence just in case
          lastcharindex = file.length();
          file_is_an_image = true;
        }

        // Some image sequence's don't start at "0", if it is indeed an image sequence, we'll use this variable
        // later to determine where it does start
        int start_number = 0;

        // Check if we passed the earlier heuristic to check whether this is a file, and whether the last number in
        // the filename (before the extension) is a number
        if (file_is_an_image && file[lastcharindex - 1].isDigit()) {
          // Check how many digits are at the end of this filename
          int digit_count = 0;
          int digit_test = lastcharindex - 1;
          while (file[digit_test].isDigit()) {
            digit_count++;
            digit_test--;
          }

          // Retrieve the integer represented at the end of this filename
          digit_test++;
          int file_number = file.mid(digit_test, digit_count).toInt();

          // Check whether a file exists with the same format but one number higher or one number lower
          if (QFileInfo::exists(QString(file.left(digit_test) +
                                        QString("%1").arg(file_number - 1, digit_count, 10, QChar('0')) +
                                        file.mid(lastcharindex))) ||
              QFileInfo::exists(QString(file.left(digit_test) +
                                        QString("%1").arg(file_number + 1, digit_count, 10, QChar('0')) +
                                        file.mid(lastcharindex)))) {
            //
            // If so, it certainly looks like it *could* be an image sequence, but we'll ask the user just in case
            //

            // Firstly we should check if this file is part of a sequence the user has already confirmed as either a
            // sequence or not a sequence (e.g. if the user happened to select a bunch of images that happen to increase
            // consecutively). We format the filename with FFmpeg's '%Nd' (N = digits) formatting for reading image
            // sequences
            QString new_filename =
                file.left(digit_test) + "%" + QString::number(digit_count) + "d" + file.mid(lastcharindex);

            int does_url_cache_already_contain_this = image_sequence_urls.indexOf(new_filename);

            if (does_url_cache_already_contain_this > -1) {
              // We've already processed an image with the same formatting

              // Check if the last time we saw this formatting, the user chose to import as a sequence
              if (image_sequence_importassequence.at(does_url_cache_already_contain_this)) {
                // If so, no need to import this file too, so we signal to the rest of the function to skip this file
                skip = true;
              }

              // If not, we can fall-through to the next step which is importing normally

            } else {
              // If we're here, we've never seen a file with this formatting before, so we'll ask whether to import
              // as a sequence or not

              // Add this file formatting file to the URL cache
              image_sequence_urls.append(new_filename);

              // This does look like an image sequence, let's ask the user if it'll indeed be an image sequence
              if (QMessageBox::question(this, tr("Image sequence detected"),
                                        tr("The file '%1' appears to be part of an image sequence. "
                                           "Would you like to import it as such?")
                                            .arg(file),
                                        QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes) == QMessageBox::Yes) {
                // Proceed to the next step of this with the formatted filename
                file = new_filename;

                // Cache the user's answer alongside the image_sequence_urls value - in this case, YES, this will be an
                // image sequence
                image_sequence_importassequence.append(true);

                // FFmpeg needs to know what file number to start at in the sequence. In case the image sequence doesn't
                // start at a zero, we'll loop decreasing the number until it doesn't exist anymore
                QString test_filename_format =
                    QString("%1%2%3").arg(file.left(digit_test), "%1", file.mid(lastcharindex));
                int test_file_number = file_number;
                do {
                  test_file_number--;
                } while (QFileInfo::exists(
                    test_filename_format.arg(QString("%1").arg(test_file_number, digit_count, 10, QChar('0')))));

                // set the image sequence's start number to the last that existed
                start_number = test_file_number + 1;

              } else {
                // Cache the user's response to the image sequence question - i.e. none of the files imported with this
                // formatting should be imported as an image sequence
                image_sequence_importassequence.append(false);
              }
            }
          }
        }

        // If we're not skipping this file, let's import it
        if (!skip) {
          MediaPtr item;
          FootagePtr m;

          if (replace != nullptr) {
            item = replace;
          } else {
            item = std::make_shared<Media>();
          }

          m = std::make_shared<Footage>();

          m->using_inout = false;
          m->url = file;
          m->name = get_file_name_from_path(i);
          m->start_number = start_number;

          item->set_footage(m);

          last_imported_media.append(item.get());

          if (replace == nullptr) {
            if (create_undo_action) {
              ca->append(new AddMediaCommand(item, parent));
            } else {
              amber::project_model.appendChild(parent, item);
            }
          }

          imported = true;
        }
      }
    }
  }
  if (create_undo_action) {
    if (imported) {
      amber::UndoStack.push(ca);

      for (auto i : last_imported_media) {
        // generate waveform/thumbnail in another thread
        PreviewGenerator::AnalyzeMedia(i);
      }
    } else {
      delete ca;
    }
  }
}

Media* Project::get_selected_folder() {
  // if one item is selected and it's a folder, return it
  QModelIndexList selected_items = get_current_selected();
  if (selected_items.size() == 1) {
    Media* m = item_to_media(selected_items.at(0));
    if (m->get_type() == MEDIA_TYPE_FOLDER) return m;
  }
  return nullptr;
}

bool Project::reveal_media(Media* media, QModelIndex parent) {
  for (int i = 0; i < amber::project_model.rowCount(parent); i++) {
    const QModelIndex& item = amber::project_model.index(i, 0, parent);
    Media* m = amber::project_model.getItem(item);

    if (m->get_type() == MEDIA_TYPE_FOLDER) {
      // if this item is a folder, recursively run this function to search it too
      if (reveal_media(media, item)) return true;

    } else if (m == media) {
      // if m == media, then we found the media object we were looking for

      // get sorter proxy item (the item that's "visible")
      QModelIndex sorted_index = sorter.mapFromSource(item);

      // retrieve its parent item
      QModelIndex hierarchy = sorted_index.parent();

      if (amber::CurrentConfig.project_view_type == amber::PROJECT_VIEW_TREE) {
        // if we're in tree view, expand every folder in the hierarchy containing the media
        while (hierarchy.isValid()) {
          tree_view->setExpanded(hierarchy, true);
          hierarchy = hierarchy.parent();
        }

        // select item (requires a QItemSelection object to select the whole row)
        QItemSelection row_select(sorter.index(sorted_index.row(), 0, sorted_index.parent()),
                                  sorter.index(sorted_index.row(), sorter.columnCount() - 1, sorted_index.parent()));

        tree_view->selectionModel()->select(row_select, QItemSelectionModel::Select);
      } else if (amber::CurrentConfig.project_view_type == amber::PROJECT_VIEW_ICON) {
        // if we're in icon view, we just "browse" to the parent folder
        icon_view->setRootIndex(hierarchy);

        // select item in this folder
        icon_view->selectionModel()->select(sorted_index, QItemSelectionModel::Select);

        // update the "up" button state
        set_up_dir_enabled();
      }

      return true;
    }
  }

  return false;
}

void Project::import_dialog() {
  QFileDialog fd(this, tr("Import media..."), "", tr("All Files") + " (*)");
  fd.setFileMode(QFileDialog::ExistingFiles);

  if (fd.exec()) {
    QStringList files = fd.selectedFiles();
    process_file_list(files, false, nullptr, get_selected_folder());
  }
}

void Project::delete_clips_using_selected_media() {
  if (amber::ActiveSequence == nullptr) {
    QMessageBox::critical(this, tr("No active sequence"),
                          tr("No sequence is active, please open the sequence you want to delete clips from."),
                          QMessageBox::Ok);
  } else {
    ComboAction* ca = new ComboAction(tr("Delete Clips"));
    bool deleted = false;
    QModelIndexList items = get_current_selected();
    for (int i = 0; i < amber::ActiveSequence->clips.size(); i++) {
      const ClipPtr& c = amber::ActiveSequence->clips.at(i);
      if (c != nullptr) {
        for (const auto& item : items) {
          Media* m = item_to_media(item);
          if (c->media() == m) {
            ca->append(new DeleteClipAction(amber::ActiveSequence.get(), i));
            deleted = true;
          }
        }
      }
    }
    for (const auto& item : items) {
      Media* m = item_to_media(item);
      if (delete_clips_in_clipboard_with_media(ca, m)) deleted = true;
    }
    if (deleted) {
      amber::UndoStack.push(ca);
      update_ui(true);
    } else {
      delete ca;
    }
  }
}

void Project::clear() {
  // clear graph editor
  panel_graph_editor->set_row(nullptr);

  // clear effects cache
  panel_effect_controls->Clear(true);

  // delete sequences first because it's important to close all the clips before deleting the media
  QVector<Media*> sequences = list_all_project_sequences();
  for (auto sequence : sequences) {
    sequence->set_sequence(nullptr);
  }

  // delete everything else
  amber::project_model.clear();

  // update tree view (sometimes this doesn't seem to update reliably)
  tree_view->update();
}

void save_marker(QXmlStreamWriter& stream, const Marker& m) {
  stream.writeStartElement("marker");
  stream.writeAttribute("frame", QString::number(m.frame));
  stream.writeAttribute("name", m.name);
  if (m.color_label > 0) {
    stream.writeAttribute("label", QString::number(m.color_label));
  }
  stream.writeEndElement();
}

void Project::save_folder(QXmlStreamWriter& stream, int type, bool set_ids_only, const QModelIndex& parent) {
  for (int i = 0; i < amber::project_model.rowCount(parent); i++) {
    const QModelIndex& item = amber::project_model.index(i, 0, parent);
    Media* m = amber::project_model.getItem(item);

    if (type == m->get_type()) {
      if (m->get_type() == MEDIA_TYPE_FOLDER) {
        if (set_ids_only) {
          m->temp_id = folder_id;  // saves a temporary ID for matching in the project file
          folder_id++;
        } else {
          // if we're saving folders, save the folder
          stream.writeStartElement("folder");
          stream.writeAttribute("name", m->get_name());
          stream.writeAttribute("id", QString::number(m->temp_id));
          if (!item.parent().isValid()) {
            stream.writeAttribute("parent", "0");
          } else {
            stream.writeAttribute("parent", QString::number(amber::project_model.getItem(item.parent())->temp_id));
          }
          if (m->color_label() > 0) {
            stream.writeAttribute("label", QString::number(m->color_label()));
          }
          stream.writeEndElement();
        }
        // save_folder(stream, item, type, set_ids_only);
      } else {
        int folder = m->parentItem()->temp_id;
        if (type == MEDIA_TYPE_FOOTAGE) {
          Footage* f = m->to_footage();
          f->save_id = media_id;
          stream.writeStartElement("footage");
          stream.writeAttribute("id", QString::number(media_id));
          stream.writeAttribute("folder", QString::number(folder));
          stream.writeAttribute("name", f->name);
          stream.writeAttribute("url", proj_dir.relativeFilePath(QDir::cleanPath(f->url)));
          stream.writeAttribute("duration", QString::number(f->length));
          stream.writeAttribute("using_inout", QString::number(f->using_inout));
          stream.writeAttribute("in", QString::number(f->in));
          stream.writeAttribute("out", QString::number(f->out));
          stream.writeAttribute("speed", QString::number(f->speed));
          stream.writeAttribute("alphapremul", QString::number(f->alpha_is_premultiplied));
          stream.writeAttribute("startnumber", QString::number(f->start_number));

          stream.writeAttribute("proxy", QString::number(f->proxy));
          stream.writeAttribute("proxypath", f->proxy_path);
          if (m->color_label() > 0) {
            stream.writeAttribute("label", QString::number(m->color_label()));
          }

          // save video stream metadata
          for (const auto& ms : f->video_tracks) {
            stream.writeStartElement("video");
            stream.writeAttribute("id", QString::number(ms.file_index));
            stream.writeAttribute("width", QString::number(ms.video_width));
            stream.writeAttribute("height", QString::number(ms.video_height));
            stream.writeAttribute("framerate", QString::number(ms.video_frame_rate, 'f', 10));
            stream.writeAttribute("infinite", QString::number(ms.infinite_length));
            stream.writeEndElement();  // video
          }

          // save audio stream metadata
          for (const auto& ms : f->audio_tracks) {
            stream.writeStartElement("audio");
            stream.writeAttribute("id", QString::number(ms.file_index));
            stream.writeAttribute("channels", QString::number(ms.audio_channels));
            stream.writeAttribute("layout", QString::number(ms.audio_layout));
            stream.writeAttribute("frequency", QString::number(ms.audio_frequency));
            stream.writeEndElement();  // audio
          }

          // save footage markers
          for (const auto& marker : f->markers) {
            save_marker(stream, marker);
          }

          stream.writeEndElement();  // footage
          media_id++;
        } else if (type == MEDIA_TYPE_SEQUENCE) {
          Sequence* s = m->to_sequence().get();
          if (set_ids_only) {
            s->save_id = sequence_id;
            sequence_id++;
          } else {
            stream.writeStartElement("sequence");
            stream.writeAttribute("id", QString::number(s->save_id));
            stream.writeAttribute("folder", QString::number(folder));
            stream.writeAttribute("name", s->name);
            stream.writeAttribute("width", QString::number(s->width));
            stream.writeAttribute("height", QString::number(s->height));
            stream.writeAttribute("framerate", QString::number(s->frame_rate, 'f', 10));
            stream.writeAttribute("afreq", QString::number(s->audio_frequency));
            stream.writeAttribute("alayout", QString::number(s->audio_layout));
            if (s == amber::ActiveSequence.get()) {
              stream.writeAttribute("open", "1");
            }
            stream.writeAttribute("workarea", QString::number(s->using_workarea));
            stream.writeAttribute("workareaIn", QString::number(s->workarea_in));
            stream.writeAttribute("workareaOut", QString::number(s->workarea_out));
            if (m->color_label() > 0) {
              stream.writeAttribute("label", QString::number(m->color_label()));
            }

            for (const auto& guide : s->guides) {
              stream.writeStartElement("guide");
              stream.writeAttribute("orientation", QString::number(guide.orientation));
              stream.writeAttribute("position", QString::number(guide.position));
              if (guide.mirror) stream.writeAttribute("mirror", "1");
              stream.writeEndElement();  // guide
            }

            QVector<TransitionPtr> transition_save_cache;
            QVector<int> transition_clip_save_cache;

            for (int j = 0; j < s->clips.size(); j++) {
              const ClipPtr& c = s->clips.at(j);
              if (c != nullptr) {
                stream.writeStartElement("clip");  // clip
                stream.writeAttribute("id", QString::number(j));
                stream.writeAttribute("enabled", QString::number(c->enabled()));
                stream.writeAttribute("name", c->name());
                stream.writeAttribute("clipin", QString::number(c->clip_in()));
                stream.writeAttribute("in", QString::number(c->timeline_in()));
                stream.writeAttribute("out", QString::number(c->timeline_out()));
                stream.writeAttribute("track", QString::number(c->track()));

                stream.writeAttribute("r", QString::number(c->color().red()));
                stream.writeAttribute("g", QString::number(c->color().green()));
                stream.writeAttribute("b", QString::number(c->color().blue()));
                if (c->color_label() > 0) {
                  stream.writeAttribute("label", QString::number(c->color_label()));
                }

                stream.writeAttribute("autoscale", QString::number(c->autoscaled()));
                stream.writeAttribute("speed", QString::number(c->speed().value, 'f', 10));
                stream.writeAttribute("maintainpitch", QString::number(c->speed().maintain_audio_pitch));
                stream.writeAttribute("reverse", QString::number(c->reversed()));
                if (c->loop_mode() != kLoopNone) {
                  stream.writeAttribute("loop", QString::number(c->loop_mode()));
                }

                if (c->media() != nullptr) {
                  stream.writeAttribute("type", QString::number(c->media()->get_type()));
                  switch (c->media()->get_type()) {
                    case MEDIA_TYPE_FOOTAGE:
                      stream.writeAttribute("media", QString::number(c->media()->to_footage()->save_id));
                      stream.writeAttribute("stream", QString::number(c->media_stream_index()));
                      break;
                    case MEDIA_TYPE_SEQUENCE:
                      stream.writeAttribute("sequence", QString::number(c->media()->to_sequence()->save_id));
                      break;
                  }
                }

                // save markers
                // only necessary for null media clips, since media has its own markers
                if (c->media() == nullptr) {
                  for (const auto& k : c->get_markers()) {
                    save_marker(stream, k);
                  }
                }

                // save clip links
                stream.writeStartElement("linked");  // linked
                for (int k : c->linked) {
                  stream.writeStartElement("link");  // link
                  stream.writeAttribute("id", QString::number(k));
                  stream.writeEndElement();  // link
                }
                stream.writeEndElement();  // linked

                // save opening and closing transitions
                for (int t = kTransitionOpening; t <= kTransitionClosing; t++) {
                  TransitionPtr transition = (t == kTransitionOpening) ? c->opening_transition : c->closing_transition;

                  if (transition != nullptr) {
                    stream.writeStartElement((t == kTransitionOpening) ? "opening" : "closing");

                    // check if this is a shared transition and the transition has already been saved
                    int transition_cache_index = transition_save_cache.indexOf(transition);

                    if (transition_cache_index > -1) {
                      // if so, just save a reference to the other clip
                      stream.writeAttribute("shared",
                                            QString::number(transition_clip_save_cache.at(transition_cache_index)));
                    } else {
                      // otherwise save the whole transition
                      transition->save(stream);
                      transition_save_cache.append(transition);
                      transition_clip_save_cache.append(j);
                    }

                    stream.writeEndElement();  // opening
                  }
                }

                for (const auto& effect : c->effects) {
                  stream.writeStartElement("effect");  // effect
                  effect->save(stream);
                  stream.writeEndElement();  // effect
                }

                stream.writeEndElement();  // clip
              }
            }
            for (const auto& marker : s->markers) {
              save_marker(stream, marker);
            }
            stream.writeEndElement();
          }
        }
      }
    }

    if (m->get_type() == MEDIA_TYPE_FOLDER) {
      save_folder(stream, type, set_ids_only, item);
    }
  }
}

void Project::save_project(bool autorecovery) {
  folder_id = 1;
  media_id = 1;
  sequence_id = 1;

  QFile file(autorecovery ? autorecovery_filename : amber::ActiveProjectFilename);
  if (!file.open(QIODevice::WriteOnly)) {
    qCritical() << "Could not open file";
    return;
  }

  QXmlStreamWriter stream(&file);
  stream.setAutoFormatting(true);
  stream.writeStartDocument();  // doc

  stream.writeStartElement("project");  // project

  stream.writeTextElement("version", QString::number(amber::kSaveVersion));

  stream.writeTextElement("url", amber::ActiveProjectFilename);
  stream.writeTextElement("previewresolution", QString::number(amber::CurrentConfig.preview_resolution_divider));
  proj_dir = QFileInfo(amber::ActiveProjectFilename).absoluteDir();

  save_folder(stream, MEDIA_TYPE_FOLDER, true);

  stream.writeStartElement("folders");  // folders
  save_folder(stream, MEDIA_TYPE_FOLDER, false);
  stream.writeEndElement();  // folders

  stream.writeStartElement("media");  // media
  save_folder(stream, MEDIA_TYPE_FOOTAGE, false);
  stream.writeEndElement();  // media

  save_folder(stream, MEDIA_TYPE_SEQUENCE, true);

  stream.writeStartElement("sequences");  // sequences
  save_folder(stream, MEDIA_TYPE_SEQUENCE, false);
  stream.writeEndElement();  // sequences

  stream.writeEndElement();  // project

  stream.writeEndDocument();  // doc

  file.close();

  if (!autorecovery) {
    add_recent_project(amber::ActiveProjectFilename);
    amber::Global->set_modified(false);
  }
}

void Project::update_view_type() {
  tree_view->setVisible(amber::CurrentConfig.project_view_type == amber::PROJECT_VIEW_TREE);
  icon_view_container->setVisible(amber::CurrentConfig.project_view_type == amber::PROJECT_VIEW_ICON ||
                                  amber::CurrentConfig.project_view_type == amber::PROJECT_VIEW_LIST);

  switch (amber::CurrentConfig.project_view_type) {
    case amber::PROJECT_VIEW_TREE:
      sources_common.view = tree_view;
      break;
    case amber::PROJECT_VIEW_ICON:
    case amber::PROJECT_VIEW_LIST:
      icon_view->setViewMode(amber::CurrentConfig.project_view_type == amber::PROJECT_VIEW_ICON ? QListView::IconMode
                                                                                                : QListView::ListMode);

      // update list/grid size since they use this value slightly differently
      set_icon_view_size(icon_size_slider->value());

      sources_common.view = icon_view;
      break;
  }
}

void Project::set_icon_view() {
  amber::CurrentConfig.project_view_type = amber::PROJECT_VIEW_ICON;
  update_view_type();
}

void Project::set_list_view() {
  amber::CurrentConfig.project_view_type = amber::PROJECT_VIEW_LIST;
  update_view_type();
}

void Project::set_tree_view() {
  amber::CurrentConfig.project_view_type = amber::PROJECT_VIEW_TREE;
  update_view_type();
}

void Project::save_recent_projects() {
  // save to file
  QFile f(amber::Global->get_recent_project_list_file());
  if (f.open(QFile::WriteOnly | QFile::Truncate | QFile::Text)) {
    QTextStream out(&f);
    for (int i = 0; i < recent_projects.size(); i++) {
      if (i > 0) {
        out << "\n";
      }
      out << recent_projects.at(i);
    }
    f.close();
  } else {
    qWarning() << "Could not save recent projects";
  }
}

void Project::clear_recent_projects() {
  recent_projects.clear();
  save_recent_projects();
}

void Project::set_icon_view_size(int s) {
  if (icon_view->viewMode() == QListView::IconMode) {
    icon_view->setGridSize(QSize(s, s));
  } else {
    icon_view->setGridSize(QSize());
    icon_view->setIconSize(QSize(s, s));
  }
}

void Project::set_up_dir_enabled() { directory_up->setEnabled(icon_view->rootIndex().isValid()); }

void Project::go_up_dir() {
  icon_view->setRootIndex(icon_view->rootIndex().parent());
  set_up_dir_enabled();
}

void Project::make_new_menu() {
  Menu new_menu(this);
  amber::MenuHelper.make_new_menu(&new_menu);
  new_menu.exec(QCursor::pos());
}

void Project::add_recent_project(QString url) {
  bool found = false;
  for (int i = 0; i < recent_projects.size(); i++) {
    if (url == recent_projects.at(i)) {
      found = true;
      recent_projects.move(i, 0);
      break;
    }
  }
  if (!found) {
    recent_projects.insert(0, url);
    if (recent_projects.size() > MAXIMUM_RECENT_PROJECTS) {
      recent_projects.removeLast();
    }
  }
  save_recent_projects();
}

void Project::list_all_sequences_worker(QVector<Media*>* list, Media* parent) {
  for (int i = 0; i < amber::project_model.childCount(parent); i++) {
    Media* item = amber::project_model.child(i, parent);
    switch (item->get_type()) {
      case MEDIA_TYPE_SEQUENCE:
        list->append(item);
        break;
      case MEDIA_TYPE_FOLDER:
        list_all_sequences_worker(list, item);
        break;
    }
  }
}

QVector<Media*> Project::list_all_project_sequences() {
  QVector<Media*> list;
  list_all_sequences_worker(&list, nullptr);
  return list;
}

QModelIndexList Project::get_current_selected() {
  if (amber::CurrentConfig.project_view_type == amber::PROJECT_VIEW_TREE) {
    return tree_view->selectionModel()->selectedRows();
  }
  return icon_view->selectionModel()->selectedIndexes();
}
