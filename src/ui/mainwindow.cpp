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

#include "mainwindow.h"

#include <QActionGroup>
#include <QApplication>
#include <QCloseEvent>
#include <QFileDialog>
#include <QInputDialog>
#include <QLayout>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QMovie>
#include <QPushButton>
#include <QRegularExpression>
#include <QStatusBar>
#include <QStyleFactory>
#include <QTimer>
#include <QTranslator>

#include "core/appcontext.h"
#include "core/path.h"
#include "dialogs/debugdialog.h"
#include "dialogs/footagerelinkdialog.h"
#include "effects/internal/srtparser.h"
#include "effects/internal/subtitleeffect.h"
#include "engine/clip.h"
#include "engine/undo/undostack.h"
#include "global/config.h"
#include "global/debug.h"
#include "global/global.h"
#include "global/projectio.h"
#include "panels/panels.h"
#include "project/projectelements.h"
#include "project/projectfilter.h"
#include "project/projectmodel.h"
#include "project/proxygenerator.h"
#include "rendering/audio.h"
#include "rendering/renderfunctions.h"
#include "ui/appcontextimpl.h"
#include "ui/cursors.h"
#include "ui/focusfilter.h"
#include "ui/icons.h"
#include "ui/menuhelper.h"
#include "ui/sourceiconview.h"
#include "ui/sourcetable.h"
#include "ui/styling.h"
#include "ui/timelineheader.h"
#include "ui/viewerwidget.h"

MainWindow* amber::MainWindow;

void MainWindow::setup_layout(bool reset) {
  // load panels from file
  if (!reset) {
    QFile panel_config(get_config_dir().filePath("layout"));
    if (panel_config.exists() && panel_config.open(QFile::ReadOnly)) {
      // default to resetting unless we find layout data in the XML file
      reset = true;

      // read XML layout file
      QXmlStreamReader stream(&panel_config);

      // loop through XML for all data
      while (!stream.atEnd()) {
        stream.readNext();

        if (stream.name() == QLatin1String("panels") && stream.isStartElement()) {
          // element contains MainWindow layout data to restore
          stream.readNext();
          restoreState(QByteArray::fromBase64(stream.text().toUtf8()), 0);
          reset = false;

        } else if (stream.name() == QLatin1String("panel") && stream.isStartElement()) {
          // element contains layout data specific to a panel, we'll find the panel and load it

          // get panel name from XML attribute
          QString panel_name;
          const QXmlStreamAttributes& attributes = stream.attributes();
          for (const auto& attr : attributes) {
            if (attr.name() == QLatin1String("name")) {
              panel_name = attr.value().toString();
              break;
            }
          }

          if (panel_name.isEmpty()) {
            qWarning() << "Layout file specified data for a panel but didn't specify a name. Layout wasn't loaded.";
          } else {
            // loop through panels for a panel with the same name

            bool found_panel = false;

            for (auto panel : amber::panels) {
              if (panel->objectName() == panel_name) {
                // found the panel, so we can load its state
                stream.readNext();
                panel->LoadLayoutState(QByteArray::fromBase64(stream.text().toUtf8()));

                // we found it, no more need to loop through panels
                found_panel = true;

                break;
              }
            }

            if (!found_panel) {
              qWarning() << "Panel specified in layout data doesn't exist. Layout wasn't loaded.";
            }
          }
        }
      }

      panel_config.close();
    } else {
      reset = true;
    }
  }

  if (reset) {
    // remove all panels from the main window
    for (auto panel : amber::panels) {
      removeDockWidget(panel);
    }

    addDockWidget(Qt::TopDockWidgetArea, panel_project);
    addDockWidget(Qt::TopDockWidgetArea, panel_graph_editor);
    addDockWidget(Qt::TopDockWidgetArea, panel_effect_controls);
    tabifyDockWidget(panel_effect_controls, panel_footage_viewer);
    panel_effect_controls->raise();
    addDockWidget(Qt::TopDockWidgetArea, panel_sequence_viewer);
    addDockWidget(Qt::BottomDockWidgetArea, panel_timeline);
    tabifyDockWidget(panel_project, panel_undo_history);

    panel_project->show();
    panel_effect_controls->show();
    panel_footage_viewer->show();
    panel_sequence_viewer->show();
    panel_timeline->show();
    panel_graph_editor->hide();
    panel_undo_history->hide();

    panel_project->setFloating(false);
    panel_effect_controls->setFloating(false);
    panel_footage_viewer->setFloating(false);
    panel_sequence_viewer->setFloating(false);
    panel_timeline->setFloating(false);
    panel_graph_editor->setFloating(true);

    resizeDocks({panel_project, panel_footage_viewer, panel_sequence_viewer}, {width() / 3, width() / 3, width() / 3},
                Qt::Horizontal);

    resizeDocks({panel_project, panel_timeline}, {height() / 2, height() / 2}, Qt::Vertical);
  }

  layout()->update();
}

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)

{
  amber::cursor::Initialize();

  open_debug_file();

  amber::DebugDialog = new DebugDialog(this);

  amber::MainWindow = this;

  QWidget* centralWidget = new QWidget(this);
  centralWidget->setMaximumSize(QSize(0, 0));
  setCentralWidget(centralWidget);

  setTabPosition(Qt::AllDockWidgetAreas, QTabWidget::North);

  setDockNestingEnabled(true);

  layout()->invalidate();

  QString data_dir = get_data_path();
  if (!data_dir.isEmpty()) {
    QDir dir(data_dir);
    dir.mkpath(".");
    if (dir.exists()) {
      qint64 a_month_ago = QDateTime::currentMSecsSinceEpoch() - 2592000000;
      qint64 a_week_ago = QDateTime::currentMSecsSinceEpoch() - 604800000;

      // TODO put delete functions in another thread?

      // delete auto-recoveries older than 7 days
      QStringList old_autorecoveries = dir.entryList(QStringList("autorecovery.ove.*"), QDir::Files);
      int deleted_ars = 0;
      for (const auto& old_autorecoverie : old_autorecoveries) {
        QString file_name = data_dir + "/" + old_autorecoverie;
        qint64 file_time = QFileInfo(file_name).lastModified().toMSecsSinceEpoch();
        if (file_time < a_week_ago) {
          if (QFile(file_name).remove()) deleted_ars++;
        }
      }
      if (deleted_ars > 0)
        qInfo() << "Deleted" << deleted_ars << "autorecovery"
                << ((deleted_ars == 1) ? "file that was" : "files that were") << "older than 7 days";

      // delete previews older than 30 days
      QDir preview_dir = QDir(dir.filePath("previews"));
      if (preview_dir.exists()) {
        deleted_ars = 0;
        QStringList old_prevs = preview_dir.entryList(QDir::Files);
        for (const auto& old_prev : old_prevs) {
          QString file_name = preview_dir.filePath(old_prev);
          qint64 file_time = QFileInfo(file_name).lastRead().toMSecsSinceEpoch();
          if (file_time < a_month_ago) {
            if (QFile(file_name).remove()) deleted_ars++;
          }
        }
        if (deleted_ars > 0)
          qInfo() << "Deleted" << deleted_ars << "preview" << ((deleted_ars == 1) ? "file that was" : "files that were")
                  << "last read over 30 days ago";
      }

      // search for open recents list
      QFile f(amber::Global->get_recent_project_list_file());
      if (f.exists() && f.open(QFile::ReadOnly | QFile::Text)) {
        QTextStream text_stream(&f);
        while (true) {
          QString line = text_stream.readLine();
          if (line.isNull()) {
            break;
          } else {
            amber::project_io->recentProjects().append(line);
          }
        }
        f.close();
      }
    }
  }
  QString config_path = get_config_path();
  if (!config_path.isEmpty()) {
    QDir config_dir(config_path);
    config_dir.mkpath(".");
    QString config_fn = config_dir.filePath("config.xml");
    if (QFileInfo::exists(config_fn)) {
      amber::CurrentConfig.load(config_fn);
    }
  }

  Restyle();

  amber::icon::Initialize();

  alloc_panels(this);
  amber::app_ctx = new AppContextImpl();

  // populate menu bars
  setup_menus();

  QStatusBar* statusBar = new QStatusBar(this);
  statusBar->showMessage(tr("Welcome to %1").arg(amber::AppName));
  setStatusBar(statusBar);

  amber::Global->check_for_autorecovery_file();

  // lock panels if the config says so
  set_panels_locked(amber::CurrentConfig.locked_panels);

  // set up output audio device
  init_audio();

  // start omnipotent proxy generator process
  amber::proxy_generator.start();

  // load preferred language from file
  amber::Global->load_translation_from_config();

  // set default strings
  Retranslate();
}

MainWindow::~MainWindow() {
  delete amber::app_ctx;
  amber::app_ctx = nullptr;
  free_panels();
  close_debug_file();
}

void kbd_shortcut_processor(QByteArray& file, QMenu* menu, bool save, bool first) {
  QList<QAction*> actions = menu->actions();
  for (auto a : actions) {
    if (a->menu() != nullptr) {
      kbd_shortcut_processor(file, a->menu(), save, first);
    } else if (!a->isSeparator()) {
      if (save) {
        // saving custom shortcuts
        if (!a->property("default").isNull()) {
          QKeySequence defks(a->property("default").toString());
          if (a->shortcut() != defks) {
            // custom shortcut
            if (!file.isEmpty()) file.append('\n');
            file.append(a->property("id").toString().toUtf8());
            file.append('\t');
            file.append(a->shortcut().toString().toUtf8());
          }
        }
      } else {
        // loading custom shortcuts
        if (first) {
          // store default shortcut
          a->setProperty("default", a->shortcut().toString());
        } else {
          // restore default shortcut
          a->setShortcut(a->property("default").toString());
        }
        if (!a->property("id").isNull()) {
          QString comp_str = a->property("id").toString();
          int shortcut_index = file.indexOf(comp_str.toUtf8());
          if (shortcut_index == 0 || (shortcut_index > 0 && file.at(shortcut_index - 1) == '\n')) {
            shortcut_index += comp_str.size() + 1;
            QString shortcut;
            while (shortcut_index < file.size() && file.at(shortcut_index) != '\n') {
              shortcut.append(file.at(shortcut_index));
              shortcut_index++;
            }
            QKeySequence ks(shortcut);
            if (!ks.isEmpty()) {
              a->setShortcut(ks);
            }
          }
        }
      }
    }
  }
}

void MainWindow::load_shortcuts(const QString& fn) {
  QByteArray shortcut_bytes;
  QFile shortcut_path(fn);
  if (shortcut_path.exists() && shortcut_path.open(QFile::ReadOnly)) {
    shortcut_bytes = shortcut_path.readAll();
    shortcut_path.close();
  }
  QList<QAction*> menus = menuBar()->actions();
  for (auto i : menus) {
    QMenu* menu = i->menu();
    kbd_shortcut_processor(shortcut_bytes, menu, false, true);
  }
}

void MainWindow::save_shortcuts(const QString& fn) {
  // save main menu actions
  QList<QAction*> menus = menuBar()->actions();
  QByteArray shortcut_file;
  for (auto i : menus) {
    QMenu* menu = i->menu();
    kbd_shortcut_processor(shortcut_file, menu, true, false);
  }
  QFile shortcut_file_io(fn);
  if (shortcut_file_io.open(QFile::WriteOnly)) {
    shortcut_file_io.write(shortcut_file);
    shortcut_file_io.close();
  } else {
    qCritical() << "Failed to save shortcut file";
  }
}

bool MainWindow::load_css_from_file(const QString& fn) {
  QFile css_file(fn);
  if (css_file.exists() && css_file.open(QFile::ReadOnly)) {
    setStyleSheet(css_file.readAll());
    css_file.close();
    return true;
  }
  return false;
}

void MainWindow::Restyle() {
  // Set up UI style
  if (!amber::styling::UseNativeUI()) {
    qApp->setStyle(QStyleFactory::create("Fusion"));

    // Set up whether to load custom CSS or default CSS+palette
    if (!amber::CurrentConfig.css_path.isEmpty() && load_css_from_file(amber::CurrentConfig.css_path)) {
      qApp->setPalette(qApp->style()->standardPalette());

    } else {
      // set default palette
      QPalette palette;

      if (amber::CurrentConfig.style == amber::styling::kOliveDefaultLight) {
        palette.setColor(QPalette::Window, QColor(208, 208, 208));
        palette.setColor(QPalette::WindowText, Qt::black);
        palette.setColor(QPalette::Base, QColor(240, 240, 240));
        palette.setColor(QPalette::AlternateBase, QColor(208, 208, 208));
        palette.setColor(QPalette::ToolTipBase, QColor(255, 255, 255));
        palette.setColor(QPalette::ToolTipText, Qt::black);
        palette.setColor(QPalette::Text, Qt::black);
        palette.setColor(QPalette::Button, QColor(208, 208, 208));
        palette.setColor(QPalette::ButtonText, Qt::black);
        palette.setColor(QPalette::BrightText, Qt::red);
        palette.setColor(QPalette::Disabled, QPalette::ButtonText, QColor(208, 208, 208));
        palette.setColor(QPalette::Link, QColor(42, 130, 218));
        palette.setColor(QPalette::Highlight, QColor(42, 130, 218));
        palette.setColor(QPalette::HighlightedText, Qt::white);

        /* Olive Mid
        palette.setColor(QPalette::Window, QColor(128, 128, 128));
        palette.setColor(QPalette::WindowText, Qt::black);
        palette.setColor(QPalette::Base, QColor(192, 192, 192));
        palette.setColor(QPalette::AlternateBase, QColor(128, 128, 128));
        palette.setColor(QPalette::ToolTipBase, QColor(192, 192, 192));
        palette.setColor(QPalette::ToolTipText, Qt::black);
        palette.setColor(QPalette::Text, Qt::black);
        palette.setColor(QPalette::Button, QColor(128, 128, 128));
        palette.setColor(QPalette::ButtonText, Qt::black);
        palette.setColor(QPalette::BrightText, Qt::red);
        palette.setColor(QPalette::Disabled, QPalette::ButtonText, QColor(128, 128, 128));
        palette.setColor(QPalette::Link, QColor(42, 130, 218));
        palette.setColor(QPalette::Highlight, QColor(42, 130, 218));
        palette.setColor(QPalette::HighlightedText, Qt::black);
        */

      } else {
        palette.setColor(QPalette::Window, QColor(53, 53, 53));
        palette.setColor(QPalette::WindowText, Qt::white);
        palette.setColor(QPalette::Base, QColor(25, 25, 25));
        palette.setColor(QPalette::AlternateBase, QColor(53, 53, 53));
        palette.setColor(QPalette::ToolTipBase, QColor(25, 25, 25));
        palette.setColor(QPalette::ToolTipText, Qt::white);
        palette.setColor(QPalette::Text, Qt::white);
        palette.setColor(QPalette::Button, QColor(53, 53, 53));
        palette.setColor(QPalette::ButtonText, Qt::white);
        palette.setColor(QPalette::BrightText, Qt::red);
        palette.setColor(QPalette::Disabled, QPalette::ButtonText, QColor(128, 128, 128));
        palette.setColor(QPalette::Link, QColor(42, 130, 218));
        palette.setColor(QPalette::Highlight, QColor(42, 130, 218));
        palette.setColor(QPalette::HighlightedText, Qt::white);

        // set default CSS
        QString stylesheet = "QPushButton::checked { background: rgb(25, 25, 25); }";

        // Windows menus have the option of being native, so we may not need this CSS
#ifdef Q_OS_WIN
        if (!amber::CurrentConfig.use_native_menu_styling) {
#endif
          stylesheet.append("QMenu::separator { background: #404040; }");
#ifdef Q_OS_WIN
        }
#endif
        setStyleSheet(stylesheet);
      }

      qApp->setPalette(palette);
    }
  }
}

void MainWindow::editMenu_About_To_Be_Shown() {
  undo_action->setEnabled(amber::UndoStack.canUndo());
  redo_action->setEnabled(amber::UndoStack.canRedo());

  QVector<Clip*> selected;
  if (amber::ActiveSequence != nullptr) {
    selected = amber::ActiveSequence->SelectedClips();
  }
  amber::MenuHelper.updateClipActions(selected);
}

void MainWindow::setup_menus() {
  QMenuBar* menuBar = new QMenuBar(this);

  if (amber::CurrentConfig.use_native_menu_styling) {
    OliveGlobal::SetNativeStyling(menuBar);
  }

  setMenuBar(menuBar);

  amber::MenuHelper.InitializeSharedMenus();

  // INITIALIZE FILE MENU

  file_menu = MenuHelper::create_submenu(menuBar, this, SLOT(fileMenu_About_To_Be_Shown()));

  new_menu = MenuHelper::create_submenu(file_menu);
  amber::MenuHelper.make_new_menu(new_menu);

  open_project = MenuHelper::create_menu_action(file_menu, "openproj", amber::Global.get(), SLOT(OpenProject()),
                                                QKeySequence("Ctrl+O"));

  open_recent = MenuHelper::create_submenu(file_menu);

  clear_open_recent_action =
      MenuHelper::create_menu_action(nullptr, "clearopenrecent", panel_project, SLOT(clear_recent_projects()));

  save_project = MenuHelper::create_menu_action(file_menu, "saveproj", amber::Global.get(), SLOT(save_project()),
                                                QKeySequence("Ctrl+S"));

  save_project_as = MenuHelper::create_menu_action(file_menu, "saveprojas", amber::Global.get(),
                                                   SLOT(save_project_as()), QKeySequence("Ctrl+Shift+S"));

  file_menu->addSeparator();

  import_action =
      MenuHelper::create_menu_action(file_menu, "import", panel_project, SLOT(import_dialog()), QKeySequence("Ctrl+I"));

  import_subtitle_action = MenuHelper::create_menu_action(file_menu, "importsubtitle", this, SLOT(import_subtitle()));

  relink_media_action = MenuHelper::create_menu_action(file_menu, "relinkmedia", this, SLOT(relink_media()));

  file_menu->addSeparator();

  export_action = MenuHelper::create_menu_action(file_menu, "export", amber::Global.get(), SLOT(open_export_dialog()),
                                                 QKeySequence("Ctrl+M"));
  export_frame_action = MenuHelper::create_menu_action(file_menu, "exportframe", panel_sequence_viewer->viewer_widget,
                                                       SLOT(save_frame()), QKeySequence("Ctrl+Shift+E"));

  file_menu->addSeparator();

  exit_action = MenuHelper::create_menu_action(file_menu, "exit", this, SLOT(close()));

  // INITIALIZE EDIT MENU

  edit_menu = MenuHelper::create_submenu(menuBar, this, SLOT(editMenu_About_To_Be_Shown()));

  undo_action =
      MenuHelper::create_menu_action(edit_menu, "undo", amber::Global.get(), SLOT(undo()), QKeySequence("Ctrl+Z"));
  redo_action = MenuHelper::create_menu_action(edit_menu, "redo", amber::Global.get(), SLOT(redo()),
                                               QKeySequence("Ctrl+Shift+Z"));

  edit_menu->addSeparator();

  amber::MenuHelper.make_edit_functions_menu(edit_menu);

  edit_menu->addSeparator();

  select_all_action = MenuHelper::create_menu_action(edit_menu, "selectall", &amber::FocusFilter, SLOT(select_all()),
                                                     QKeySequence("Ctrl+A"));
  deselect_all_action = MenuHelper::create_menu_action(edit_menu, "deselectall", panel_timeline, SLOT(deselect()),
                                                       QKeySequence("Ctrl+Shift+A"));

  edit_menu->addSeparator();

  amber::MenuHelper.make_clip_functions_menu(edit_menu, true);

  edit_menu->addSeparator();

  ripple_to_in_point_ = MenuHelper::create_menu_action(edit_menu, "rippletoin", panel_timeline,
                                                       SLOT(ripple_to_in_point()), QKeySequence("Q"));
  ripple_to_out_point_ = MenuHelper::create_menu_action(edit_menu, "rippletoout", panel_timeline,
                                                        SLOT(ripple_to_out_point()), QKeySequence("W"));
  edit_to_in_point_ = MenuHelper::create_menu_action(edit_menu, "edittoin", panel_timeline, SLOT(edit_to_in_point()),
                                                     QKeySequence("Ctrl+Alt+Q"));
  edit_to_out_point_ = MenuHelper::create_menu_action(edit_menu, "edittoout", panel_timeline, SLOT(edit_to_out_point()),
                                                      QKeySequence("Ctrl+Alt+W"));

  edit_menu->addSeparator();

  amber::MenuHelper.make_inout_menu(edit_menu, true);
  delete_inout_point_ = MenuHelper::create_menu_action(amber::MenuHelper.inout_submenu_, "deleteinout", panel_timeline,
                                                       SLOT(delete_inout()), QKeySequence(";"));
  ripple_delete_inout_point_ =
      MenuHelper::create_menu_action(amber::MenuHelper.inout_submenu_, "rippledeleteinout", panel_timeline,
                                     SLOT(ripple_delete_inout()), QKeySequence("'"));

  edit_menu->addSeparator();

  setedit_marker_ =
      MenuHelper::create_menu_action(edit_menu, "marker", &amber::FocusFilter, SLOT(set_marker()), QKeySequence("M"));

  edit_menu->addSeparator();

  preferences_action_ = MenuHelper::create_menu_action(edit_menu, "prefs", amber::Global.get(),
                                                       SLOT(open_preferences()), QKeySequence("Ctrl+,"));

  // INITIALIZE VIEW MENU

  view_menu = MenuHelper::create_submenu(menuBar, this, SLOT(viewMenu_About_To_Be_Shown()));

  zoom_in_ =
      MenuHelper::create_menu_action(view_menu, "zoomin", &amber::FocusFilter, SLOT(zoom_in()), QKeySequence("="));
  zoom_out_ =
      MenuHelper::create_menu_action(view_menu, "zoomout", &amber::FocusFilter, SLOT(zoom_out()), QKeySequence("-"));
  increase_track_height_ = MenuHelper::create_menu_action(view_menu, "vzoomin", panel_timeline,
                                                          SLOT(IncreaseTrackHeight()), QKeySequence("Ctrl+="));
  decrease_track_height_ = MenuHelper::create_menu_action(view_menu, "vzoomout", panel_timeline,
                                                          SLOT(DecreaseTrackHeight()), QKeySequence("Ctrl+-"));

  show_all =
      MenuHelper::create_menu_action(view_menu, "showall", panel_timeline, SLOT(toggle_show_all()), QKeySequence("\\"));
  show_all->setCheckable(true);

  view_menu->addSeparator();

  track_lines = MenuHelper::create_menu_action(view_menu, "tracklines", &amber::MenuHelper, SLOT(toggle_bool_action()));
  track_lines->setCheckable(true);
  track_lines->setData(reinterpret_cast<quintptr>(&amber::CurrentConfig.show_track_lines));

  rectified_waveforms =
      MenuHelper::create_menu_action(view_menu, "rectifiedwaveforms", &amber::MenuHelper, SLOT(toggle_bool_action()));
  rectified_waveforms->setCheckable(true);
  rectified_waveforms->setData(reinterpret_cast<quintptr>(&amber::CurrentConfig.rectified_waveforms));

  view_menu->addSeparator();

  QActionGroup* frame_view_mode_group = new QActionGroup(this);

  frames_action =
      MenuHelper::create_menu_action(view_menu, "modeframes", &amber::MenuHelper, SLOT(set_timecode_view()));
  frames_action->setData(amber::kTimecodeFrames);
  frames_action->setCheckable(true);
  frame_view_mode_group->addAction(frames_action);

  drop_frame_action =
      MenuHelper::create_menu_action(view_menu, "modedropframe", &amber::MenuHelper, SLOT(set_timecode_view()));
  drop_frame_action->setData(amber::kTimecodeDrop);
  drop_frame_action->setCheckable(true);
  frame_view_mode_group->addAction(drop_frame_action);

  nondrop_frame_action =
      MenuHelper::create_menu_action(view_menu, "modenondropframe", &amber::MenuHelper, SLOT(set_timecode_view()));
  nondrop_frame_action->setData(amber::kTimecodeNonDrop);
  nondrop_frame_action->setCheckable(true);
  frame_view_mode_group->addAction(nondrop_frame_action);

  milliseconds_action =
      MenuHelper::create_menu_action(view_menu, "milliseconds", &amber::MenuHelper, SLOT(set_timecode_view()));
  milliseconds_action->setData(amber::kTimecodeMilliseconds);
  milliseconds_action->setCheckable(true);
  frame_view_mode_group->addAction(milliseconds_action);

  view_menu->addSeparator();

  preview_resolution_menu = MenuHelper::create_submenu(view_menu);
  QActionGroup* preview_res_group = new QActionGroup(this);

  preview_res_full_ =
      MenuHelper::create_menu_action(preview_resolution_menu, "previewfull", this, SLOT(set_preview_resolution()));
  preview_res_full_->setData(1);
  preview_res_full_->setCheckable(true);
  preview_res_group->addAction(preview_res_full_);

  preview_res_half_ =
      MenuHelper::create_menu_action(preview_resolution_menu, "previewhalf", this, SLOT(set_preview_resolution()));
  preview_res_half_->setData(2);
  preview_res_half_->setCheckable(true);
  preview_res_group->addAction(preview_res_half_);

  preview_res_quarter_ =
      MenuHelper::create_menu_action(preview_resolution_menu, "previewquarter", this, SLOT(set_preview_resolution()));
  preview_res_quarter_->setData(4);
  preview_res_quarter_->setCheckable(true);
  preview_res_group->addAction(preview_res_quarter_);

  view_menu->addSeparator();

  title_safe_area_menu = MenuHelper::create_submenu(view_menu);

  QActionGroup* title_safe_group = new QActionGroup(this);

  title_safe_off = MenuHelper::create_menu_action(title_safe_area_menu, "titlesafeoff", &amber::MenuHelper,
                                                  SLOT(set_titlesafe_from_menu()));
  title_safe_off->setCheckable(true);
  title_safe_off->setData(qSNaN());
  title_safe_group->addAction(title_safe_off);

  title_safe_default = MenuHelper::create_menu_action(title_safe_area_menu, "titlesafedefault", &amber::MenuHelper,
                                                      SLOT(set_titlesafe_from_menu()));
  title_safe_default->setCheckable(true);
  title_safe_default->setData(0.0);
  title_safe_group->addAction(title_safe_default);

  title_safe_43 = MenuHelper::create_menu_action(title_safe_area_menu, "titlesafe43", &amber::MenuHelper,
                                                 SLOT(set_titlesafe_from_menu()));
  title_safe_43->setCheckable(true);
  title_safe_43->setData(4.0 / 3.0);
  title_safe_group->addAction(title_safe_43);

  title_safe_169 = MenuHelper::create_menu_action(title_safe_area_menu, "titlesafe169", &amber::MenuHelper,
                                                  SLOT(set_titlesafe_from_menu()));
  title_safe_169->setCheckable(true);
  title_safe_169->setData(16.0 / 9.0);
  title_safe_group->addAction(title_safe_169);

  title_safe_custom = MenuHelper::create_menu_action(title_safe_area_menu, "titlesafecustom", &amber::MenuHelper,
                                                     SLOT(set_titlesafe_from_menu()));
  title_safe_custom->setCheckable(true);
  title_safe_custom->setData(-1.0);
  title_safe_group->addAction(title_safe_custom);

  view_menu->addSeparator();

  show_guides_ = MenuHelper::create_menu_action(view_menu, "showguides", &amber::MenuHelper, SLOT(toggle_bool_action()),
                                                QKeySequence("Ctrl+;"));
  show_guides_->setCheckable(true);
  show_guides_->setData(reinterpret_cast<quintptr>(&amber::CurrentConfig.show_guides));
  connect(show_guides_, &QAction::triggered, this, [this]() {
    panel_sequence_viewer->viewer_widget->container->adjust();
    panel_footage_viewer->viewer_widget->container->adjust();
  });

  guides_menu_ = MenuHelper::create_submenu(view_menu);
  lock_guides_ = MenuHelper::create_menu_action(guides_menu_, "lockguides", &amber::MenuHelper,
                                                SLOT(toggle_bool_action()), QKeySequence("Ctrl+Alt+;"));
  lock_guides_->setCheckable(true);
  lock_guides_->setData(reinterpret_cast<quintptr>(&amber::CurrentConfig.lock_guides));
  guides_menu_->addSeparator();
  guides_menu_->addAction(panel_sequence_viewer->viewer_widget->guide_delete_action_);
  guides_menu_->addAction(panel_sequence_viewer->viewer_widget->guide_mirror_action_);

  view_menu->addSeparator();

  full_screen =
      MenuHelper::create_menu_action(view_menu, "fullscreen", this, SLOT(toggle_full_screen()), QKeySequence("F11"));
  full_screen->setCheckable(true);

  full_screen_viewer_ =
      MenuHelper::create_menu_action(view_menu, "fullscreenviewer", &amber::FocusFilter, SLOT(set_viewer_fullscreen()));

  // INITIALIZE PLAYBACK MENU

  playback_menu = MenuHelper::create_submenu(menuBar, this, SLOT(playbackMenu_About_To_Be_Shown()));

  go_to_start_ = MenuHelper::create_menu_action(playback_menu, "gotostart", &amber::FocusFilter, SLOT(go_to_start()),
                                                QKeySequence("Home"));
  previous_frame_ = MenuHelper::create_menu_action(playback_menu, "prevframe", &amber::FocusFilter, SLOT(prev_frame()),
                                                   QKeySequence("Left"));
  playpause_ = MenuHelper::create_menu_action(playback_menu, "playpause", &amber::FocusFilter, SLOT(playpause()),
                                              QKeySequence("Space"));
  play_in_to_out_ = MenuHelper::create_menu_action(playback_menu, "playintoout", &amber::FocusFilter,
                                                   SLOT(play_in_to_out()), QKeySequence("Shift+Space"));
  next_frame_ = MenuHelper::create_menu_action(playback_menu, "nextframe", &amber::FocusFilter, SLOT(next_frame()),
                                               QKeySequence("Right"));
  go_to_end_ = MenuHelper::create_menu_action(playback_menu, "gotoend", &amber::FocusFilter, SLOT(go_to_end()),
                                              QKeySequence("End"));

  playback_menu->addSeparator();

  previous_frames_ = MenuHelper::create_menu_action(playback_menu, "prevframes", &amber::FocusFilter,
                                                    SLOT(prev_frames()), QKeySequence("Ctrl+["));
  next_frames_ = MenuHelper::create_menu_action(playback_menu, "nextframes", &amber::FocusFilter, SLOT(next_frames()),
                                                QKeySequence("Ctrl+]"));

  playback_menu->addSeparator();

  go_to_prev_cut_ = MenuHelper::create_menu_action(playback_menu, "prevcut", panel_timeline, SLOT(previous_cut()),
                                                   QKeySequence("Up"));
  go_to_next_cut_ =
      MenuHelper::create_menu_action(playback_menu, "nextcut", panel_timeline, SLOT(next_cut()), QKeySequence("Down"));

  playback_menu->addSeparator();

  go_to_in_point_ = MenuHelper::create_menu_action(playback_menu, "gotoin", &amber::FocusFilter, SLOT(go_to_in()),
                                                   QKeySequence("Shift+I"));
  go_to_out_point_ = MenuHelper::create_menu_action(playback_menu, "gotoout", &amber::FocusFilter, SLOT(go_to_out()),
                                                    QKeySequence("Shift+O"));

  playback_menu->addSeparator();

  shuttle_left_ = MenuHelper::create_menu_action(playback_menu, "decspeed", &amber::FocusFilter, SLOT(decrease_speed()),
                                                 QKeySequence("J"));
  shuttle_stop_ =
      MenuHelper::create_menu_action(playback_menu, "pause", &amber::FocusFilter, SLOT(pause()), QKeySequence("K"));
  shuttle_right_ = MenuHelper::create_menu_action(playback_menu, "incspeed", &amber::FocusFilter,
                                                  SLOT(increase_speed()), QKeySequence("L"));

  playback_menu->addSeparator();

  loop_action_ = MenuHelper::create_menu_action(playback_menu, "loop", &amber::MenuHelper, SLOT(toggle_bool_action()));
  loop_action_->setCheckable(true);
  loop_action_->setData(reinterpret_cast<quintptr>(&amber::CurrentConfig.loop));

  // INITIALIZE WINDOW MENU

  window_menu = MenuHelper::create_submenu(menuBar, this, SLOT(windowMenu_About_To_Be_Shown()));

  window_project_action =
      MenuHelper::create_menu_action(window_menu, "panelproject", this, SLOT(toggle_panel_visibility()));
  window_project_action->setCheckable(true);
  window_project_action->setData(reinterpret_cast<quintptr>(panel_project));

  window_effectcontrols_action =
      MenuHelper::create_menu_action(window_menu, "paneleffectcontrols", this, SLOT(toggle_panel_visibility()));
  window_effectcontrols_action->setCheckable(true);
  window_effectcontrols_action->setData(reinterpret_cast<quintptr>(panel_effect_controls));

  window_timeline_action =
      MenuHelper::create_menu_action(window_menu, "paneltimeline", this, SLOT(toggle_panel_visibility()));
  window_timeline_action->setCheckable(true);
  window_timeline_action->setData(reinterpret_cast<quintptr>(panel_timeline));

  window_graph_editor_action =
      MenuHelper::create_menu_action(window_menu, "panelgrapheditor", this, SLOT(toggle_panel_visibility()));
  window_graph_editor_action->setCheckable(true);
  window_graph_editor_action->setData(reinterpret_cast<quintptr>(panel_graph_editor));

  window_footageviewer_action =
      MenuHelper::create_menu_action(window_menu, "panelfootageviewer", this, SLOT(toggle_panel_visibility()));
  window_footageviewer_action->setCheckable(true);
  window_footageviewer_action->setData(reinterpret_cast<quintptr>(panel_footage_viewer));

  window_sequenceviewer_action =
      MenuHelper::create_menu_action(window_menu, "panelsequenceviewer", this, SLOT(toggle_panel_visibility()));
  window_sequenceviewer_action->setCheckable(true);
  window_sequenceviewer_action->setData(reinterpret_cast<quintptr>(panel_sequence_viewer));

  window_undo_history_action =
      MenuHelper::create_menu_action(window_menu, "panelundohistory", this, SLOT(toggle_panel_visibility()));
  window_undo_history_action->setCheckable(true);
  window_undo_history_action->setData(reinterpret_cast<quintptr>(panel_undo_history));

  window_menu->addSeparator();

  maximize_panel_ =
      MenuHelper::create_menu_action(window_menu, "maximizepanel", this, SLOT(maximize_panel()), QKeySequence("`"));

  lock_panels_ = MenuHelper::create_menu_action(window_menu, "lockpanels", this, SLOT(set_panels_locked(bool)));
  lock_panels_->setCheckable(true);

  window_menu->addSeparator();

  reset_default_layout_ = MenuHelper::create_menu_action(window_menu, "resetdefaultlayout", this, SLOT(reset_layout()));

  // INITIALIZE TOOLS MENU

  tools_menu = MenuHelper::create_submenu(menuBar, this, SLOT(toolMenu_About_To_Be_Shown()));
  tools_menu->setToolTipsVisible(true);

  QActionGroup* tools_group = new QActionGroup(this);

  pointer_tool_action = MenuHelper::create_menu_action(tools_menu, "pointertool", &amber::MenuHelper,
                                                       SLOT(menu_click_button()), QKeySequence("V"));
  pointer_tool_action->setCheckable(true);
  pointer_tool_action->setData(reinterpret_cast<quintptr>(panel_timeline->toolArrowButton));
  tools_group->addAction(pointer_tool_action);

  edit_tool_action = MenuHelper::create_menu_action(tools_menu, "edittool", &amber::MenuHelper,
                                                    SLOT(menu_click_button()), QKeySequence("X"));
  edit_tool_action->setCheckable(true);
  edit_tool_action->setData(reinterpret_cast<quintptr>(panel_timeline->toolEditButton));
  tools_group->addAction(edit_tool_action);

  ripple_tool_action = MenuHelper::create_menu_action(tools_menu, "rippletool", &amber::MenuHelper,
                                                      SLOT(menu_click_button()), QKeySequence("B"));
  ripple_tool_action->setCheckable(true);
  ripple_tool_action->setData(reinterpret_cast<quintptr>(panel_timeline->toolRippleButton));
  tools_group->addAction(ripple_tool_action);

  razor_tool_action = MenuHelper::create_menu_action(tools_menu, "razortool", &amber::MenuHelper,
                                                     SLOT(menu_click_button()), QKeySequence("C"));
  razor_tool_action->setCheckable(true);
  razor_tool_action->setData(reinterpret_cast<quintptr>(panel_timeline->toolRazorButton));
  tools_group->addAction(razor_tool_action);

  slip_tool_action = MenuHelper::create_menu_action(tools_menu, "sliptool", &amber::MenuHelper,
                                                    SLOT(menu_click_button()), QKeySequence("Y"));
  slip_tool_action->setCheckable(true);
  slip_tool_action->setData(reinterpret_cast<quintptr>(panel_timeline->toolSlipButton));
  tools_group->addAction(slip_tool_action);

  slide_tool_action = MenuHelper::create_menu_action(tools_menu, "slidetool", &amber::MenuHelper,
                                                     SLOT(menu_click_button()), QKeySequence("U"));
  slide_tool_action->setCheckable(true);
  slide_tool_action->setData(reinterpret_cast<quintptr>(panel_timeline->toolSlideButton));
  tools_group->addAction(slide_tool_action);

  hand_tool_action = MenuHelper::create_menu_action(tools_menu, "handtool", &amber::MenuHelper,
                                                    SLOT(menu_click_button()), QKeySequence("H"));
  hand_tool_action->setCheckable(true);
  hand_tool_action->setData(reinterpret_cast<quintptr>(panel_timeline->toolHandButton));
  tools_group->addAction(hand_tool_action);

  transition_tool_action = MenuHelper::create_menu_action(tools_menu, "transitiontool", &amber::MenuHelper,
                                                          SLOT(menu_click_button()), QKeySequence("T"));
  transition_tool_action->setCheckable(true);
  transition_tool_action->setData(reinterpret_cast<quintptr>(panel_timeline->toolTransitionButton));
  tools_group->addAction(transition_tool_action);

  tools_menu->addSeparator();

  snap_toggle = MenuHelper::create_menu_action(tools_menu, "snapping", &amber::MenuHelper, SLOT(menu_click_button()),
                                               QKeySequence("S"));
  snap_toggle->setCheckable(true);
  snap_toggle->setData(reinterpret_cast<quintptr>(panel_timeline->snappingButton));

  color_labels_toggle = MenuHelper::create_menu_action(tools_menu, "colorlabels", this, SLOT(toggle_color_labels()));
  color_labels_toggle->setCheckable(true);

  tools_menu->addSeparator();

  autocut_silence_ = MenuHelper::create_menu_action(tools_menu, "autocutsilence", amber::Global.get(),
                                                    SLOT(open_autocut_silence_dialog()));

  tools_menu->addSeparator();

  QActionGroup* autoscroll_group = new QActionGroup(this);

  no_autoscroll =
      MenuHelper::create_menu_action(tools_menu, "autoscrollno", &amber::MenuHelper, SLOT(set_autoscroll()));
  no_autoscroll->setData(amber::AUTOSCROLL_NO_SCROLL);
  no_autoscroll->setCheckable(true);
  autoscroll_group->addAction(no_autoscroll);

  page_autoscroll =
      MenuHelper::create_menu_action(tools_menu, "autoscrollpage", &amber::MenuHelper, SLOT(set_autoscroll()));
  page_autoscroll->setData(amber::AUTOSCROLL_PAGE_SCROLL);
  page_autoscroll->setCheckable(true);
  autoscroll_group->addAction(page_autoscroll);

  smooth_autoscroll =
      MenuHelper::create_menu_action(tools_menu, "autoscrollsmooth", &amber::MenuHelper, SLOT(set_autoscroll()));
  smooth_autoscroll->setData(amber::AUTOSCROLL_SMOOTH_SCROLL);
  smooth_autoscroll->setCheckable(true);
  autoscroll_group->addAction(smooth_autoscroll);

  tools_menu->addSeparator();
  tools_menu->addAction(preferences_action_);

#ifdef QT_DEBUG
  clear_undo_action_ =
      MenuHelper::create_menu_action(tools_menu, "clearundo", amber::Global.get(), SLOT(clear_undo_stack()));
#endif

  // INITIALIZE HELP MENU

  help_menu = MenuHelper::create_submenu(menuBar);

  action_search_ = MenuHelper::create_menu_action(help_menu, "actionsearch", amber::Global.get(),
                                                  SLOT(open_action_search()), QKeySequence("/"));

  help_menu->addSeparator();

  debug_log_ = MenuHelper::create_menu_action(help_menu, "debuglog", amber::Global.get(), SLOT(open_debug_log()));

  help_menu->addSeparator();

  about_action_ = MenuHelper::create_menu_action(help_menu, "about", amber::Global.get(), SLOT(open_about_dialog()));

  load_shortcuts(get_config_path() + "/shortcuts");
}

void MainWindow::Retranslate() {
  file_menu->setTitle(tr("&File"));
  new_menu->setTitle(tr("&New"));
  open_project->setText(tr("&Open Project"));
  clear_open_recent_action->setText(tr("Clear Recent List"));
  open_recent->setTitle(tr("Open Recent"));
  save_project->setText(tr("&Save Project"));
  save_project_as->setText(tr("Save Project &As"));
  import_action->setText(tr("&Import..."));
  import_subtitle_action->setText(tr("Import Subtitle (.srt)..."));
  relink_media_action->setText(tr("Relink Media..."));
  export_action->setText(tr("&Export..."));
  export_frame_action->setText(tr("Export &Frame..."));
  exit_action->setText(tr("E&xit"));

  edit_menu->setTitle(tr("&Edit"));
  undo_action->setText(tr("&Undo"));
  redo_action->setText(tr("Redo"));
  select_all_action->setText(tr("Select &All"));
  deselect_all_action->setText(tr("Deselect All"));
  ripple_to_in_point_->setText(tr("Ripple to In Point"));
  ripple_to_out_point_->setText(tr("Ripple to Out Point"));
  edit_to_in_point_->setText(tr("Edit to In Point"));
  edit_to_out_point_->setText(tr("Edit to Out Point"));
  delete_inout_point_->setText(tr("Delete In/Out Point"));
  ripple_delete_inout_point_->setText(tr("Ripple Delete In/Out Point"));
  setedit_marker_->setText(tr("Set/Edit Marker"));

  view_menu->setTitle(tr("&View"));
  zoom_in_->setText(tr("Zoom In"));
  zoom_out_->setText(tr("Zoom Out"));
  increase_track_height_->setText(tr("Increase Track Height"));
  decrease_track_height_->setText(tr("Decrease Track Height"));
  show_all->setText(tr("Toggle Show All"));
  track_lines->setText(tr("Track Lines"));
  rectified_waveforms->setText(tr("Rectified Waveforms"));
  frames_action->setText(tr("Frames"));
  drop_frame_action->setText(tr("Drop Frame"));
  nondrop_frame_action->setText(tr("Non-Drop Frame"));
  milliseconds_action->setText(tr("Milliseconds"));

  preview_resolution_menu->setTitle(tr("Preview Resolution"));
  preview_res_full_->setText(tr("Full"));
  preview_res_half_->setText(tr("1/2 (Half)"));
  preview_res_quarter_->setText(tr("1/4 (Quarter)"));

  title_safe_area_menu->setTitle(tr("Title/Action Safe Area"));
  title_safe_off->setText(tr("Off"));
  title_safe_default->setText(tr("Default"));
  title_safe_43->setText(tr("4:3"));
  title_safe_169->setText(tr("16:9"));
  title_safe_custom->setText(tr("Custom"));

  show_guides_->setText(tr("Show Guides"));
  guides_menu_->setTitle(tr("Guides"));
  lock_guides_->setText(tr("Lock Guides"));
  panel_sequence_viewer->viewer_widget->guide_delete_action_->setText(tr("Delete Guide"));
  panel_sequence_viewer->viewer_widget->guide_mirror_action_->setText(tr("Toggle Mirror"));

  full_screen->setText(tr("Full Screen"));
  full_screen_viewer_->setText(tr("Full Screen Viewer"));

  playback_menu->setTitle(tr("&Playback"));
  go_to_start_->setText(tr("Go to Start"));
  previous_frame_->setText(tr("Previous Frame"));
  playpause_->setText(tr("Play/Pause"));
  play_in_to_out_->setText(tr("Play In to Out"));
  next_frame_->setText(tr("Next Frame"));
  go_to_end_->setText(tr("Go to End"));

  previous_frames_->setText(tr("Jump Backward"));
  next_frames_->setText(tr("Jump Forward"));

  go_to_prev_cut_->setText(tr("Go to Previous Cut"));
  go_to_next_cut_->setText(tr("Go to Next Cut"));
  go_to_in_point_->setText(tr("Go to In Point"));
  go_to_out_point_->setText(tr("Go to Out Point"));

  shuttle_left_->setText(tr("Shuttle Left"));
  shuttle_stop_->setText(tr("Shuttle Stop"));
  shuttle_right_->setText(tr("Shuttle Right"));

  loop_action_->setText(tr("Loop"));

  window_menu->setTitle(tr("&Window"));

  window_project_action->setText(tr("Project"));
  window_effectcontrols_action->setText(tr("Effect Controls"));
  window_timeline_action->setText(tr("Timeline"));
  window_graph_editor_action->setText(tr("Graph Editor"));
  window_footageviewer_action->setText(tr("Media Viewer"));
  window_sequenceviewer_action->setText(tr("Sequence Viewer"));
  window_undo_history_action->setText(tr("Undo History"));

  maximize_panel_->setText(tr("Maximize Panel"));
  lock_panels_->setText(tr("Lock Panels"));
  reset_default_layout_->setText(tr("Reset to Default Layout"));

  tools_menu->setTitle(tr("&Tools"));

  pointer_tool_action->setText(tr("Pointer Tool"));
  edit_tool_action->setText(tr("Edit Tool"));
  ripple_tool_action->setText(tr("Ripple Tool"));
  razor_tool_action->setText(tr("Razor Tool"));
  slip_tool_action->setText(tr("Slip Tool"));
  slide_tool_action->setText(tr("Slide Tool"));
  hand_tool_action->setText(tr("Hand Tool"));
  transition_tool_action->setText(tr("Transition Tool"));
  snap_toggle->setText(tr("Enable Snapping"));
  color_labels_toggle->setText(tr("Color Labels"));
  autocut_silence_->setText(tr("Auto-Cut Silence"));

  no_autoscroll->setText(tr("No Auto-Scroll"));
  page_autoscroll->setText(tr("Page Auto-Scroll"));
  smooth_autoscroll->setText(tr("Smooth Auto-Scroll"));

  preferences_action_->setText(tr("Preferences"));
#ifdef QT_DEBUG
  clear_undo_action_->setText(tr("Clear Undo"));
#endif

  help_menu->setTitle(tr("&Help"));

  action_search_->setText(tr("A&ction Search"));
  debug_log_->setText(tr("Debug Log"));
  about_action_->setText(tr("&About..."));

  panel_sequence_viewer->set_panel_name(QCoreApplication::translate("Viewer", "Sequence Viewer"));
  panel_footage_viewer->set_panel_name(QCoreApplication::translate("Viewer", "Media Viewer"));

  // the recommended changeEvent() and event() methods of propagating language change messages provided mixed results
  // (i.e. different panels failed to translate in different sessions), so we translate them manually here
  for (auto panel : amber::panels) {
    panel->Retranslate();
  }
  amber::MenuHelper.Retranslate();

  updateTitle();
}

void MainWindow::updateTitle() {
  setWindowTitle(QString("%1 - %2[*]")
                     .arg(amber::AppName,
                          (amber::ActiveProjectFilename.isEmpty()) ? tr("<untitled>") : amber::ActiveProjectFilename));
}

void MainWindow::closeEvent(QCloseEvent* e) {
  if (amber::Global->can_close_project()) {
    // stop proxy generator thread
    amber::proxy_generator.cancel();

    panel_graph_editor->set_row(nullptr);
    panel_effect_controls->Clear(true);

    amber::Global->set_sequence(nullptr);

    panel_footage_viewer->viewer_widget->close_window();
    panel_sequence_viewer->viewer_widget->close_window();

    panel_footage_viewer->set_main_sequence();

    QString data_dir = get_data_path();
    QString config_path = get_config_path();
    if (!data_dir.isEmpty() && !amber::project_io->autorecoveryFilename().isEmpty()) {
      if (QFile::exists(amber::project_io->autorecoveryFilename())) {
        QFile::rename(amber::project_io->autorecoveryFilename(),
                      amber::project_io->autorecoveryFilename() + "." +
                          QDateTime::currentDateTimeUtc().toString("yyyyMMddHHmmss"));
      }
    }
    if (!config_path.isEmpty()) {
      QDir config_dir = QDir(config_path);

      QString config_fn = config_dir.filePath("config.xml");

      // save settings
      amber::CurrentConfig.save(config_fn);

      // save panel layout
      QFile panel_config(get_config_dir().filePath("layout"));
      if (panel_config.open(QFile::WriteOnly)) {
        QXmlStreamWriter stream(&panel_config);
        stream.setAutoFormatting(true);
        stream.writeStartDocument();

        stream.writeStartElement("layout");

        stream.writeTextElement("panels", saveState(0).toBase64());

        // if the panels have any specific layout data to save, save it now
        for (int i = 0; i < amber::panels.size(); i++) {
          QByteArray layout_data = amber::panels.at(i)->SaveLayoutState();

          if (!layout_data.isEmpty()) {
            // layout data is matched with the panel's objectName(), which we can't do if the panel has no name
            const QString& panel_name = amber::panels.at(i)->objectName();
            if (panel_name.isEmpty()) {
              qWarning() << "Panel" << i << "had layout state data but no objectName(). Layout was not saved.";
            } else {
              stream.writeStartElement("panel");

              stream.writeAttribute("name", panel_name);

              stream.writeCharacters(layout_data.toBase64());

              stream.writeEndElement();
            }
          }
        }

        stream.writeEndElement();  // layout

        stream.writeEndDocument();
        panel_config.close();
      } else {
        qCritical() << "Failed to save layout";
      }

      save_shortcuts(config_path + "/shortcuts");
    }

    stop_audio();

    // Quit instead of accepting — accepting destroys the Vulkan surface
    // mid-event-loop, causing a double-free crash on Wayland.
    e->ignore();
    QCoreApplication::quit();
  } else {
    e->ignore();
  }
}

void MainWindow::paintEvent(QPaintEvent* event) {
  QMainWindow::paintEvent(event);

  if (first_show) {
    // set this to false immediately to prevent anything here being called again
    first_show = false;

    /**
     * @brief Set up the dock widget layout on the main window
     *
     * For some reason, Qt didn't like this in the constructor. It would lead to several geometry issues with HiDPI
     * on Windows, and also seemed to break QMainWindow::restoreState() which is why it took so long to implement
     * saving/restoring panel layouts. Putting it in showEvent() didn't help either, nor did putting it in
     * changeEvent() (QEvent::type() == QEvent::Polish). This is the only place it's functioned as expected.
     */
    setup_layout(false);

    /**
      Signal that window has finished loading.
     */
    emit finished_first_paint();
  }
}

void MainWindow::changeEvent(QEvent* e) {
  if (e->type() == QEvent::LanguageChange) {
    // if this was a LanguageEvent, run the retranslation function
    Retranslate();

  } else {
    // otherwise pass it to the base class
    QMainWindow::changeEvent(e);
  }
}

void MainWindow::reset_layout() { setup_layout(true); }

void MainWindow::maximize_panel() {
  // toggles between normal state and a state of one panel being maximized
  if (temp_panel_state.isEmpty()) {
    // get currently hovered panel
    QDockWidget* focused_panel = get_focused_panel(true);

    // if the mouse is in fact hovering over a panel
    if (focused_panel != nullptr) {
      // store the current state of panels
      temp_panel_state = saveState();

      // remove all dock widgets that aren't the hovered panel
      for (auto panel : amber::panels) {
        if (panel != focused_panel) {
          // hide the panel
          panel->setVisible(false);

          // set it to floating
          panel->setFloating(true);
        }
      }
    }
  } else {
    // we must be maximized, restore previous state
    restoreState(temp_panel_state);

    // clear temp panel state for next maximize call
    temp_panel_state.clear();
  }
}

void MainWindow::windowMenu_About_To_Be_Shown() {
  QList<QAction*> window_actions = window_menu->actions();
  for (auto a : window_actions) {
    if (!a->data().isNull()) {
      a->setChecked(reinterpret_cast<QDockWidget*>(a->data().value<quintptr>())->isVisible());
    }
  }
}

void MainWindow::playbackMenu_About_To_Be_Shown() { amber::MenuHelper.set_bool_action_checked(loop_action_); }

void MainWindow::viewMenu_About_To_Be_Shown() {
  amber::MenuHelper.set_bool_action_checked(track_lines);

  amber::MenuHelper.set_bool_action_checked(rectified_waveforms);

  amber::MenuHelper.set_int_action_checked(frames_action, amber::CurrentConfig.timecode_view);
  amber::MenuHelper.set_int_action_checked(drop_frame_action, amber::CurrentConfig.timecode_view);
  amber::MenuHelper.set_int_action_checked(nondrop_frame_action, amber::CurrentConfig.timecode_view);
  amber::MenuHelper.set_int_action_checked(milliseconds_action, amber::CurrentConfig.timecode_view);

  title_safe_off->setChecked(!amber::CurrentConfig.show_title_safe_area);
  title_safe_default->setChecked(amber::CurrentConfig.show_title_safe_area &&
                                 !amber::CurrentConfig.use_custom_title_safe_ratio);
  title_safe_43->setChecked(
      amber::CurrentConfig.show_title_safe_area && amber::CurrentConfig.use_custom_title_safe_ratio &&
      qFuzzyCompare(amber::CurrentConfig.custom_title_safe_ratio, title_safe_43->data().toDouble()));
  title_safe_169->setChecked(
      amber::CurrentConfig.show_title_safe_area && amber::CurrentConfig.use_custom_title_safe_ratio &&
      qFuzzyCompare(amber::CurrentConfig.custom_title_safe_ratio, title_safe_169->data().toDouble()));
  title_safe_custom->setChecked(amber::CurrentConfig.show_title_safe_area &&
                                amber::CurrentConfig.use_custom_title_safe_ratio && !title_safe_43->isChecked() &&
                                !title_safe_169->isChecked());

  amber::MenuHelper.set_bool_action_checked(show_guides_);
  amber::MenuHelper.set_bool_action_checked(lock_guides_);

  full_screen->setChecked(windowState() == Qt::WindowFullScreen);

  show_all->setChecked(panel_timeline->showing_all);

  int div = amber::CurrentConfig.preview_resolution_divider;
  preview_res_full_->setChecked(div <= 1);
  preview_res_half_->setChecked(div == 2);
  preview_res_quarter_->setChecked(div == 4);
}

void MainWindow::set_preview_resolution() {
  QAction* action = static_cast<QAction*>(sender());
  amber::CurrentConfig.preview_resolution_divider = action->data().toInt();
  panel_sequence_viewer->update_preview_res_label();
  panel_footage_viewer->update_preview_res_label();
  update_ui(false);
}

void MainWindow::toolMenu_About_To_Be_Shown() {
  amber::MenuHelper.set_button_action_checked(pointer_tool_action);
  amber::MenuHelper.set_button_action_checked(edit_tool_action);
  amber::MenuHelper.set_button_action_checked(ripple_tool_action);
  amber::MenuHelper.set_button_action_checked(razor_tool_action);
  amber::MenuHelper.set_button_action_checked(slip_tool_action);
  amber::MenuHelper.set_button_action_checked(slide_tool_action);
  amber::MenuHelper.set_button_action_checked(hand_tool_action);
  amber::MenuHelper.set_button_action_checked(transition_tool_action);
  amber::MenuHelper.set_button_action_checked(snap_toggle);

  amber::MenuHelper.set_int_action_checked(no_autoscroll, amber::CurrentConfig.autoscroll);
  amber::MenuHelper.set_int_action_checked(page_autoscroll, amber::CurrentConfig.autoscroll);
  amber::MenuHelper.set_int_action_checked(smooth_autoscroll, amber::CurrentConfig.autoscroll);

  color_labels_toggle->setChecked(amber::CurrentConfig.show_color_labels);
}

void MainWindow::toggle_color_labels() {
  amber::CurrentConfig.show_color_labels = !amber::CurrentConfig.show_color_labels;
  color_labels_toggle->setChecked(amber::CurrentConfig.show_color_labels);
  update_ui(false);
}

static void collect_invalid_footage(Media* parent, QVector<QPair<Media*, Footage*>>& result) {
  for (int i = 0; i < parent->childCount(); i++) {
    Media* m = parent->child(i);
    if (m->get_type() == MEDIA_TYPE_FOOTAGE) {
      Footage* f = m->to_footage();
      if (f->invalid) {
        result.append(qMakePair(m, f));
      }
    } else if (m->get_type() == MEDIA_TYPE_FOLDER) {
      collect_invalid_footage(m, result);
    }
  }
}

void MainWindow::relink_media() {
  QVector<QPair<Media*, Footage*>> invalid;
  Media* root = amber::project_model.get_root();
  if (root != nullptr) {
    collect_invalid_footage(root, invalid);
  }
  if (invalid.isEmpty()) {
    QMessageBox::information(this, tr("Relink Media"), tr("All media is linked."));
  } else {
    FootageRelinkDialog dlg(this, invalid);
    dlg.exec();
  }
}

void MainWindow::import_subtitle() {
  if (amber::ActiveSequence == nullptr) {
    QMessageBox::warning(this, tr("No Sequence"), tr("Please open a sequence before importing subtitles."));
    return;
  }

  QString filepath = QFileDialog::getOpenFileName(this, tr("Import Subtitle"), QString(),
                                                  tr("SRT Subtitle Files (*.srt);;All Files (*)"));

  if (filepath.isEmpty()) return;

  SrtParseResult result = parse_srt(filepath);

  if (result.cues.isEmpty()) {
    QMessageBox::warning(this, tr("Import Failed"), tr("No valid subtitle cues found in the file."));
    return;
  }

  if (result.skipped > 0) {
    QMessageBox::information(
        this, tr("Import Subtitle"),
        tr("Imported %1 cues, %2 skipped (malformed).").arg(result.cues.size()).arg(result.skipped));
  }

  Sequence* seq = amber::ActiveSequence.get();
  long playhead = seq->playhead;

  // Calculate clip duration from last cue's end_ms
  qint64 last_end_ms = 0;
  for (const SubtitleCue& cue : result.cues) {
    if (cue.end_ms > last_end_ms) last_end_ms = cue.end_ms;
  }
  long duration_frames = qRound64(last_end_ms * seq->frame_rate / 1000.0);

  // Find first free video track at playhead (start at -1, go down)
  int track = -1;
  while (track > -100) {
    bool collision = false;
    long check_in = playhead;
    long check_out = playhead + duration_frames;
    for (const ClipPtr& c : seq->clips) {
      if (c != nullptr && c->track() == track && check_in < c->timeline_out() && check_out > c->timeline_in()) {
        collision = true;
        break;
      }
    }
    if (!collision) break;
    track--;
  }

  // Create clip
  ClipPtr clip = std::make_shared<Clip>(seq);
  clip->set_media(nullptr, 0);
  clip->set_timeline_in(playhead);
  clip->set_timeline_out(playhead + duration_frames);
  clip->set_clip_in(0);
  clip->set_track(track);
  clip->set_name(QFileInfo(filepath).baseName());
  clip->set_color(220, 180, 60);

  // Add Transform effect (standard for video clips)
  if (amber::CurrentConfig.add_default_effects_to_clips) {
    clip->effects.append(
        Effect::Create(clip.get(), Effect::GetInternalMeta(EFFECT_INTERNAL_TRANSFORM, EFFECT_TYPE_EFFECT)));
  }

  // Add SubtitleEffect with parsed cues
  EffectPtr sub_effect =
      Effect::Create(clip.get(), Effect::GetInternalMeta(EFFECT_INTERNAL_SUBTITLE, EFFECT_TYPE_EFFECT));
  static_cast<SubtitleEffect*>(sub_effect.get())->SetCues(result.cues);
  clip->effects.append(sub_effect);

  // Add via undo system
  ComboAction* ca = new ComboAction(tr("Import Subtitle"));
  QVector<ClipPtr> clips_to_add;
  clips_to_add.append(clip);
  ca->append(new AddClipCommand(seq, clips_to_add));
  amber::UndoStack.push(ca);

  update_ui(true);

  // Scroll video area to reveal the track where the subtitle was placed
  panel_timeline->scroll_to_track(track);
}

void MainWindow::toggle_panel_visibility() {
  QAction* action = static_cast<QAction*>(sender());
  QDockWidget* w = reinterpret_cast<QDockWidget*>(action->data().value<quintptr>());
  w->setVisible(!w->isVisible());

  // layout has changed, we're no longer in maximized panel mode,
  // so we clear this byte array
  temp_panel_state.clear();
}

void MainWindow::set_panels_locked(bool locked) {
  for (auto panel : amber::panels) {
    if (locked) {
      // disable moving on QDockWidget
      panel->setFeatures(panel->features() & ~QDockWidget::DockWidgetMovable);

      // hide the title bar (only real way to do this is to replace it with an empty QWidget)
      panel->setTitleBarWidget(new QWidget(panel));
    } else {
      // re-enable moving on QDockWidget
      panel->setFeatures(panel->features() | QDockWidget::DockWidgetMovable);

      // set the "custom" titlebar to null so the default gets restored
      panel->setTitleBarWidget(nullptr);
    }
  }

  amber::CurrentConfig.locked_panels = locked;
}

void MainWindow::fileMenu_About_To_Be_Shown() {
  if (amber::project_io->recentProjects().size() > 0) {
    open_recent->clear();
    open_recent->setEnabled(true);
    for (int i = 0; i < amber::project_io->recentProjects().size(); i++) {
      QAction* action = open_recent->addAction(amber::project_io->recentProjects().at(i));
      action->setProperty("keyignore", true);
      action->setData(i);
      connect(action, &QAction::triggered, &amber::MenuHelper, &MenuHelper::open_recent_from_menu);
    }
    open_recent->addSeparator();

    open_recent->addAction(clear_open_recent_action);
  } else {
    open_recent->setEnabled(false);
  }
}

void MainWindow::toggle_full_screen() {
  if (windowState() == Qt::WindowFullScreen) {
    setWindowState(Qt::WindowNoState);  // seems to be necessary for it to return to Maximized correctly on Linux
    setWindowState(Qt::WindowMaximized);
  } else {
    setWindowState(Qt::WindowFullScreen);
  }
}
