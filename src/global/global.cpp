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

#include "global/global.h"

#include <QAction>
#include <QApplication>
#include <QDebug>
#include <QFileDialog>
#include <QMessageBox>
#include <QStyleFactory>

#include "core/path.h"
#include "dialogs/aboutdialog.h"
#include "dialogs/actionsearch.h"
#include "dialogs/autocutsilencedialog.h"
#include "dialogs/debugdialog.h"
#include "dialogs/demonotice.h"
#include "dialogs/exportdialog.h"
#include "dialogs/footagerelinkdialog.h"
#include "dialogs/loaddialog.h"
#include "dialogs/preferencesdialog.h"
#include "dialogs/speeddialog.h"
#include "engine/sequence.h"
#include "engine/undo/undostack.h"
#include "global/config.h"
#include "panels/panels.h"
#include "project/loadthread.h"
#include "project/projectmodel.h"
#include "rendering/audio.h"
#include "ui/mainwindow.h"
#include "ui/mediaiconservice.h"
#include "ui/menuhelper.h"

std::unique_ptr<AmberGlobal> amber::Global;
QString amber::ActiveProjectFilename;
QString amber::AppName;

AmberGlobal::AmberGlobal()

{
  // sets current app name
  QString version_id;

  // if available, append the current Git hash (defined by `qmake` and the Makefile)
#ifdef GITHASH
  version_id = QString(" | %1").arg(GITHASH);
#endif

  amber::AppName = QString("Amber (%1%2)").arg(APPVERSION, version_id);

  // set the file filter used in all file dialogs pertaining to Amber project files.
  project_file_filter = tr("Amber Project %1").arg("(*.ove)");

  // set default value
  enable_load_project_on_init = false;

  // alloc QTranslator
  translator = std::unique_ptr<QTranslator>(new QTranslator());

  project_io_ = new ProjectIO(this);
  amber::project_io = project_io_;

  connect(project_io_, &ProjectIO::autorecoverySaveRequested, this, [this]() {
    if (panel_project != nullptr) {
      panel_project->save_project(true);
    }
  });
}

const QString& AmberGlobal::get_project_file_filter() { return project_file_filter; }

void AmberGlobal::update_project_filename(const QString& s) {
  // set filename to s
  amber::ActiveProjectFilename = s;
  project_io_->setProjectFilename(s);

  // update main window title to reflect new project filename
  if (amber::MainWindow != nullptr) amber::MainWindow->updateTitle();
}

void AmberGlobal::check_for_autorecovery_file() {
  project_io_->initAutorecovery();
  const QString& ar_filename = project_io_->autorecoveryFilename();
  if (!ar_filename.isEmpty() && QFile::exists(ar_filename)) {
    if (QMessageBox::question(nullptr, tr("Auto-recovery"),
                              tr("Amber didn't close properly and an autorecovery file "
                                 "was detected. Would you like to open it?"),
                              QMessageBox::Yes, QMessageBox::No) == QMessageBox::Yes) {
      enable_load_project_on_init = false;
      OpenProjectWorker(ar_filename, true);
    }
  }
}

void AmberGlobal::reconfigure_autorecovery() {
  autorecovery_timer.stop();
  if (amber::CurrentConfig.autorecovery_enabled) {
    autorecovery_timer.setInterval(amber::CurrentConfig.autorecovery_interval * 60000);
    autorecovery_timer.start();
  }
  project_io_->reconfigureAutorecovery();
}

void AmberGlobal::set_rendering_state(bool rendering) {
  project_io_->setRenderingState(rendering);
  if (rendering) {
    autorecovery_timer.stop();
  } else if (amber::CurrentConfig.autorecovery_enabled) {
    autorecovery_timer.start();
  }
}

void AmberGlobal::set_modified(bool modified) {
  project_io_->setModified(modified);
  if (amber::MainWindow == nullptr) return;
  amber::MainWindow->setWindowModified(modified);
  changed_since_last_autorecovery = modified;
}

bool AmberGlobal::is_modified() { return project_io_->isModified(); }

void AmberGlobal::load_project_on_launch(const QString& s) {
  amber::ActiveProjectFilename = s;
  enable_load_project_on_init = true;
}

QString AmberGlobal::get_recent_project_list_file() { return project_io_->recentProjectListFile(); }

void AmberGlobal::load_translation_from_config() {
  QString language_file = amber::CurrentRuntimeConfig.external_translation_file.isEmpty()
                              ? amber::CurrentConfig.language_file
                              : amber::CurrentRuntimeConfig.external_translation_file;

  // clear runtime language file so if the user sets a different language, we won't load it next time
  amber::CurrentRuntimeConfig.external_translation_file.clear();

  // remove current translation if there is one
  QApplication::removeTranslator(translator.get());

  if (!language_file.isEmpty()) {
    // translation files are stored relative to app path (see GitHub issue #454)
    QString full_language_path = QDir(get_app_path()).filePath(language_file);

    // load translation file
    if (QFileInfo::exists(full_language_path) && translator->load(full_language_path)) {
      QApplication::installTranslator(translator.get());
    } else {
      qWarning() << "Failed to load translation file" << full_language_path << ". No language will be loaded.";
    }
  }
}

void AmberGlobal::SetNativeStyling(QWidget* w) {
#ifdef Q_OS_WIN
  w->setStyleSheet("");
  w->setPalette(w->style()->standardPalette());
  w->setStyle(QStyleFactory::create("windowsvista"));
#endif
}

void AmberGlobal::LoadProject(const QString& fn, bool autorecovery) {
  // QSortFilterProxyModels are not thread-safe, and as we'll be loading in another thread, leaving it connected
  // can cause glitches in its presentation. Therefore for the duration of the loading process, we disconnect it,
  // and reconnect it later once the loading is complete.

  panel_project->DisconnectFilterToModel();

  LoadDialog ld(amber::MainWindow);

  ld.open();

  LoadThread* lt = new LoadThread(fn, autorecovery);
  connect(&ld, &LoadDialog::cancel, lt, &LoadThread::cancel);
  connect(lt, &LoadThread::success, &ld, &QDialog::accept);
  connect(lt, &LoadThread::error, &ld, &QDialog::reject);
  connect(lt, &LoadThread::error, this, &AmberGlobal::new_project);
  connect(lt, &LoadThread::report_progress, &ld, &LoadDialog::setValue);
  connect(lt, &LoadThread::found_invalid_footage, this, [](QVector<QPair<Media*, Footage*>> invalid) {
    FootageRelinkDialog dlg(amber::MainWindow, invalid);
    dlg.exec();
    if (dlg.relinked_any()) {
      amber::Global->set_modified(true);
    }
  });
  lt->start();

  panel_project->ConnectFilterToModel();
}

void AmberGlobal::ClearProject() {
  // clear graph editor
  panel_graph_editor->set_row(nullptr);

  // clear effects panel
  panel_effect_controls->Clear(true);

  // clear existing project
  amber::Global->set_sequence(nullptr);
  panel_footage_viewer->set_media(nullptr);

  // clear project contents (footage, sequences, etc.)
  panel_project->clear();

  // clear undo stack
  amber::UndoStack.clear();

  // empty current project filename
  update_project_filename("");

  // full update of all panels
  update_ui(false);

  // set to unmodified
  amber::Global->set_modified(false);
}

void AmberGlobal::ImportProject(const QString& fn) {
  LoadProject(fn, false);
  set_modified(true);
}

void AmberGlobal::new_project() {
  if (amber::project_model.childCount() == 0 && !is_modified()) {
    QString shortcut = amber::MenuHelper.new_sequence_action()->shortcut().toString(QKeySequence::NativeText);
    QMessageBox::information(amber::MainWindow, tr("Project Already Empty"),
                             tr("You already have a bare project. If you're trying to activate the timeline, "
                                "you need to create a new sequence (File > New > Sequence, or %1).")
                                 .arg(shortcut));
    return;
  }
  if (can_close_project()) {
    ClearProject();
  }
}

void AmberGlobal::OpenProject() {
  QString fn = QFileDialog::getOpenFileName(amber::MainWindow, tr("Open Project..."), "", project_file_filter);
  if (!fn.isEmpty() && can_close_project()) {
    OpenProjectWorker(fn, false);
  }
}

void AmberGlobal::open_recent(int index) {
  QString recent_url = amber::project_io->recentProjects().at(index);
  if (!QFile::exists(recent_url)) {
    if (QMessageBox::question(
            amber::MainWindow, tr("Missing recent project"),
            tr("The project '%1' no longer exists. Would you like to remove it from the recent projects list?")
                .arg(recent_url),
            QMessageBox::Yes, QMessageBox::No) == QMessageBox::Yes) {
      amber::project_io->recentProjects().removeAt(index);
      panel_project->save_recent_projects();
    }
  } else if (can_close_project()) {
    OpenProjectWorker(recent_url, false);
  }
}

bool AmberGlobal::save_project_as() {
  QString fn = QFileDialog::getSaveFileName(amber::MainWindow, tr("Save Project As..."), "", project_file_filter);
  if (!fn.isEmpty()) {
    if (!fn.endsWith(".ove", Qt::CaseInsensitive)) {
      fn += ".ove";
    }
    update_project_filename(fn);
    panel_project->save_project(false);
    return true;
  }
  return false;
}

bool AmberGlobal::save_project() {
  if (amber::ActiveProjectFilename.isEmpty()) {
    return save_project_as();
  } else {
    panel_project->save_project(false);
    return true;
  }
}

bool AmberGlobal::can_close_project() {
  if (is_modified()) {
    QMessageBox* m = new QMessageBox(
        QMessageBox::Question, tr("Unsaved Project"),
        tr("This project has changed since it was last saved. Would you like to save it before closing?"),
        QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel, amber::MainWindow);
    m->setWindowModality(Qt::WindowModal);
    int r = m->exec();
    delete m;
    if (r == QMessageBox::Yes) {
      return save_project();
    } else if (r == QMessageBox::Cancel) {
      return false;
    }
  }
  return true;
}

void AmberGlobal::open_export_dialog() {
  if (CheckForActiveSequence()) {
    ExportDialog e(amber::MainWindow);
    e.exec();
  }
}

void AmberGlobal::finished_initialize() {
  if (enable_load_project_on_init) {
    // if a project was set as a command line argument, we load it here
    if (QFileInfo::exists(amber::ActiveProjectFilename)) {
      OpenProjectWorker(amber::ActiveProjectFilename, false);
    } else {
      QMessageBox::critical(amber::MainWindow, tr("Missing Project File"),
                            tr("Specified project '%1' does not exist.").arg(amber::ActiveProjectFilename),
                            QMessageBox::Ok);
      update_project_filename(nullptr);
    }

    enable_load_project_on_init = false;

  } else {
    // 2.0 preview: always show the pre-alpha notice on launch (release builds only)
#ifndef QT_DEBUG
    DemoNotice* d = new DemoNotice(amber::MainWindow);
    connect(d, &QDialog::finished, d, &QObject::deleteLater);
    d->open();
#endif
  }
}

void AmberGlobal::save_autorecovery_file() {
  if (changed_since_last_autorecovery) {
    panel_project->save_project(true);

    changed_since_last_autorecovery = false;

    qInfo() << "Auto-recovery project saved";
  }
}

void AmberGlobal::open_preferences() {
  panel_sequence_viewer->pause();
  panel_footage_viewer->pause();

  PreferencesDialog pd(amber::MainWindow);
  pd.exec();
}

void AmberGlobal::set_sequence(SequencePtr s, bool record_history) {
  project_io_->setSequence(s, record_history);

  panel_graph_editor->set_row(nullptr);
  panel_effect_controls->Clear(true);
  panel_sequence_viewer->set_main_sequence();
  panel_timeline->update_sequence();
  panel_timeline->setFocus();
}

void AmberGlobal::go_back_sequence() {
  if (!project_io_->canGoBack()) return;
  project_io_->goBackSequence();

  panel_graph_editor->set_row(nullptr);
  panel_effect_controls->Clear(true);
  panel_sequence_viewer->set_main_sequence();
  panel_timeline->update_sequence();
  panel_timeline->setFocus();
}

bool AmberGlobal::can_go_back() const { return project_io_->canGoBack(); }

const QVector<SequencePtr>& AmberGlobal::sequence_history() const { return project_io_->sequenceHistory(); }

void AmberGlobal::clear_sequence_history() { project_io_->clearSequenceHistory(); }

void AmberGlobal::OpenProjectWorker(QString fn, bool autorecovery) {
  ClearProject();
  update_project_filename(fn);
  LoadProject(fn, autorecovery);
  amber::UndoStack.clear();
}

bool AmberGlobal::CheckForActiveSequence(bool show_msg) {
  if (amber::ActiveSequence == nullptr) {
    if (show_msg) {
      QMessageBox::information(amber::MainWindow, tr("No active sequence"),
                               tr("Please open the sequence to perform this action."), QMessageBox::Ok);
    }

    return false;
  }
  return true;
}

void AmberGlobal::undo() {
  // workaround to prevent crash (and also users should never need to do this)
  if (!panel_timeline->importing) {
    amber::UndoStack.undo();
    update_ui(true);
  }
}

void AmberGlobal::redo() {
  // workaround to prevent crash (and also users should never need to do this)
  if (!panel_timeline->importing) {
    amber::UndoStack.redo();
    update_ui(true);
  }
}

void AmberGlobal::paste() {
  if (amber::ActiveSequence != nullptr) {
    panel_timeline->paste(false);
  }
}

void AmberGlobal::paste_insert() {
  if (amber::ActiveSequence != nullptr) {
    panel_timeline->paste(true);
  }
}

void AmberGlobal::open_about_dialog() {
  AboutDialog a(amber::MainWindow);
  a.exec();
}

void AmberGlobal::open_debug_log() {
  if (amber::DebugDialog != nullptr) amber::DebugDialog->show();
}

void AmberGlobal::open_speed_dialog() {
  if (amber::ActiveSequence != nullptr) {
    QVector<Clip*> selected_clips = amber::ActiveSequence->SelectedClips();

    if (!selected_clips.isEmpty()) {
      SpeedDialog s(amber::MainWindow, selected_clips);
      s.exec();
    }
  }
}

void AmberGlobal::open_autocut_silence_dialog() {
  if (CheckForActiveSequence()) {
    QVector<int> selected_clips = amber::ActiveSequence->SelectedClipIndexes();

    if (selected_clips.isEmpty()) {
      QMessageBox::critical(amber::MainWindow, tr("No clips selected"), tr("Select the clips you wish to auto-cut"),
                            QMessageBox::Ok);
    } else {
      AutoCutSilenceDialog s(amber::MainWindow, selected_clips);
      s.exec();
    }
  }
}

void AmberGlobal::clear_undo_stack() { amber::UndoStack.clear(); }

void AmberGlobal::open_action_search() {
  ActionSearch as(amber::MainWindow);
  as.exec();
}
