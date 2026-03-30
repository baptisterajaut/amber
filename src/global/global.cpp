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

#include <QMessageBox>
#include <QFileDialog>
#include <QAction>
#include <QApplication>
#include <QStyleFactory>
#include <QDebug>

#include "panels/panels.h"
#include "core/path.h"
#include "global/config.h"
#include "rendering/audio.h"
#include "dialogs/demonotice.h"
#include "dialogs/preferencesdialog.h"
#include "dialogs/exportdialog.h"
#include "dialogs/debugdialog.h"
#include "dialogs/aboutdialog.h"
#include "dialogs/speeddialog.h"
#include "dialogs/actionsearch.h"
#include "dialogs/footagerelinkdialog.h"
#include "dialogs/loaddialog.h"
#include "dialogs/autocutsilencedialog.h"
#include "project/loadthread.h"
#include "project/projectmodel.h"
#include "ui/menuhelper.h"
#include "engine/sequence.h"
#include "ui/mediaiconservice.h"
#include "ui/mainwindow.h"
#include "engine/undo/undostack.h"

std::unique_ptr<OliveGlobal> amber::Global;
QString amber::ActiveProjectFilename;
QString amber::AppName;

OliveGlobal::OliveGlobal() 
  
{
  // sets current app name
  QString version_id;

  // if available, append the current Git hash (defined by `qmake` and the Makefile)
#ifdef GITHASH
  version_id = QString(" | %1").arg(GITHASH);
#endif

  amber::AppName = QString("Amber (%1%2)").arg(APPVERSION, version_id);

  // set the file filter used in all file dialogs pertaining to Olive project files.
  project_file_filter = tr("Amber Project %1").arg("(*.ove)");

  // set default value
  enable_load_project_on_init = false;

  // alloc QTranslator
  translator = std::unique_ptr<QTranslator>(new QTranslator());
}

const QString &OliveGlobal::get_project_file_filter() {
  return project_file_filter;
}

void OliveGlobal::update_project_filename(const QString &s) {
  // set filename to s
  amber::ActiveProjectFilename = s;

  // update main window title to reflect new project filename
  if (amber::MainWindow != nullptr) amber::MainWindow->updateTitle();
}

void OliveGlobal::check_for_autorecovery_file() {
  QString data_dir = get_data_path();
  if (!data_dir.isEmpty()) {
    // detect auto-recovery file
    autorecovery_filename = data_dir + "/autorecovery.ove";
    if (QFile::exists(autorecovery_filename)) {
      if (QMessageBox::question(nullptr,
                                tr("Auto-recovery"),
                                tr("Amber didn't close properly and an autorecovery file "
                                   "was detected. Would you like to open it?"),
                                QMessageBox::Yes,
                                QMessageBox::No) == QMessageBox::Yes) {
        enable_load_project_on_init = false;
        OpenProjectWorker(autorecovery_filename, true);
      }
    }
    autorecovery_timer.setInterval(amber::CurrentConfig.autorecovery_interval * 60000);
    QObject::connect(&autorecovery_timer, &QTimer::timeout, this, &OliveGlobal::save_autorecovery_file);
    if (amber::CurrentConfig.autorecovery_enabled) {
      autorecovery_timer.start();
    }
  }
}

void OliveGlobal::reconfigure_autorecovery() {
  autorecovery_timer.stop();
  if (amber::CurrentConfig.autorecovery_enabled) {
    autorecovery_timer.setInterval(amber::CurrentConfig.autorecovery_interval * 60000);
    autorecovery_timer.start();
  }
}

void OliveGlobal::set_rendering_state(bool rendering) {
  audio_rendering = rendering;
  if (rendering) {
    autorecovery_timer.stop();
  } else if (amber::CurrentConfig.autorecovery_enabled) {
    autorecovery_timer.start();
  }
}

void OliveGlobal::set_modified(bool modified)
{
  if (amber::MainWindow == nullptr) return;
  amber::MainWindow->setWindowModified(modified);
  changed_since_last_autorecovery = modified;
}

bool OliveGlobal::is_modified()
{
  if (amber::MainWindow == nullptr) return false;
  return amber::MainWindow->isWindowModified();
}

void OliveGlobal::load_project_on_launch(const QString& s) {
  amber::ActiveProjectFilename = s;
  enable_load_project_on_init = true;
}

QString OliveGlobal::get_recent_project_list_file() {
  return get_data_dir().filePath("recents");
}

void OliveGlobal::load_translation_from_config() {
  QString language_file = amber::CurrentRuntimeConfig.external_translation_file.isEmpty() ?
        amber::CurrentConfig.language_file :
        amber::CurrentRuntimeConfig.external_translation_file;

  // clear runtime language file so if the user sets a different language, we won't load it next time
  amber::CurrentRuntimeConfig.external_translation_file.clear();

  // remove current translation if there is one
  QApplication::removeTranslator(translator.get());

  if (!language_file.isEmpty()) {

    // translation files are stored relative to app path (see GitHub issue #454)
    QString full_language_path = QDir(get_app_path()).filePath(language_file);

    // load translation file
    if (QFileInfo::exists(full_language_path)
        && translator->load(full_language_path)) {
      QApplication::installTranslator(translator.get());
    } else {
      qWarning() << "Failed to load translation file" << full_language_path << ". No language will be loaded.";
    }
  }
}

void OliveGlobal::SetNativeStyling(QWidget *w)
{
#ifdef Q_OS_WIN
  w->setStyleSheet("");
  w->setPalette(w->style()->standardPalette());
  w->setStyle(QStyleFactory::create("windowsvista"));
#endif
}

void OliveGlobal::LoadProject(const QString &fn, bool autorecovery)
{
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
  connect(lt, &LoadThread::error, this, &OliveGlobal::new_project);
  connect(lt, &LoadThread::report_progress, &ld, &LoadDialog::setValue);
  connect(lt, &LoadThread::found_invalid_footage, this,
          [](QVector<QPair<Media*, Footage*>> invalid) {
            FootageRelinkDialog dlg(amber::MainWindow, invalid);
            dlg.exec();
            if (dlg.relinked_any()) {
              amber::Global->set_modified(true);
            }
          });
  lt->start();

  panel_project->ConnectFilterToModel();
}

void OliveGlobal::ClearProject()
{
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

void OliveGlobal::ImportProject(const QString &fn)
{
  LoadProject(fn, false);
  set_modified(true);
}

void OliveGlobal::new_project() {
  if (amber::project_model.childCount() == 0 && !is_modified()) {
    QString shortcut = amber::MenuHelper.new_sequence_action()->shortcut().toString(QKeySequence::NativeText);
    QMessageBox::information(
        amber::MainWindow,
        tr("Project Already Empty"),
        tr("You already have a bare project. If you're trying to activate the timeline, "
           "you need to create a new sequence (File > New > Sequence, or %1).").arg(shortcut));
    return;
  }
  if (can_close_project()) {
    ClearProject();
  }
}

void OliveGlobal::OpenProject() {
  QString fn = QFileDialog::getOpenFileName(amber::MainWindow, tr("Open Project..."), "", project_file_filter);
  if (!fn.isEmpty() && can_close_project()) {
    OpenProjectWorker(fn, false);
  }
}

void OliveGlobal::open_recent(int index) {
  QString recent_url = recent_projects.at(index);
  if (!QFile::exists(recent_url)) {
    if (QMessageBox::question(
          amber::MainWindow,
          tr("Missing recent project"),
          tr("The project '%1' no longer exists. Would you like to remove it from the recent projects list?").arg(recent_url),
          QMessageBox::Yes, QMessageBox::No) == QMessageBox::Yes) {
      recent_projects.removeAt(index);
      panel_project->save_recent_projects();
    }
  } else if (can_close_project()) {
    OpenProjectWorker(recent_url, false);
  }
}

bool OliveGlobal::save_project_as() {
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

bool OliveGlobal::save_project() {
  if (amber::ActiveProjectFilename.isEmpty()) {
    return save_project_as();
  } else {
    panel_project->save_project(false);
    return true;
  }
}

bool OliveGlobal::can_close_project() {
  if (is_modified()) {
    QMessageBox* m = new QMessageBox(
          QMessageBox::Question,
          tr("Unsaved Project"),
          tr("This project has changed since it was last saved. Would you like to save it before closing?"),
          QMessageBox::Yes|QMessageBox::No|QMessageBox::Cancel,
          amber::MainWindow
          );
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

void OliveGlobal::open_export_dialog() {
  if (CheckForActiveSequence()) {
    ExportDialog e(amber::MainWindow);
    e.exec();
  }
}

void OliveGlobal::finished_initialize() {
  if (enable_load_project_on_init) {

    // if a project was set as a command line argument, we load it here
    if (QFileInfo::exists(amber::ActiveProjectFilename)) {
      OpenProjectWorker(amber::ActiveProjectFilename, false);
    } else {
      QMessageBox::critical(amber::MainWindow,
                            tr("Missing Project File"),
                            tr("Specified project '%1' does not exist.").arg(amber::ActiveProjectFilename),
                            QMessageBox::Ok);
      update_project_filename(nullptr);
    }

    enable_load_project_on_init = false;

  } else {
    // if we are not loading a project on launch and are running a release build, open the demo notice dialog
#ifndef QT_DEBUG
    if (amber::CurrentConfig.show_welcome_dialog) {
      DemoNotice* d = new DemoNotice(amber::MainWindow);
      connect(d, &QDialog::finished, d, &QObject::deleteLater);
      d->open();
    }
#endif
  }

}

void OliveGlobal::save_autorecovery_file() {
  if (changed_since_last_autorecovery) {
    panel_project->save_project(true);

    changed_since_last_autorecovery = false;

    qInfo() << "Auto-recovery project saved";
  }
}

void OliveGlobal::open_preferences() {
  panel_sequence_viewer->pause();
  panel_footage_viewer->pause();

  PreferencesDialog pd(amber::MainWindow);
  pd.exec();
}

void OliveGlobal::set_sequence(SequencePtr s, bool record_history)
{
  // Push current sequence onto history stack only for explicit user navigation (double-click into nested seq)
  if (record_history && amber::ActiveSequence != nullptr && s != nullptr && amber::ActiveSequence != s) {
    sequence_history_.append(amber::ActiveSequence);
  }

  // Clearing sequence (e.g. new project) resets the history
  if (s == nullptr) {
    sequence_history_.clear();
  }

  panel_graph_editor->set_row(nullptr);
  panel_effect_controls->Clear(true);

  amber::ActiveSequence = s;
  panel_sequence_viewer->set_main_sequence();
  panel_timeline->update_sequence();
  panel_timeline->setFocus();
}

void OliveGlobal::go_back_sequence()
{
  if (sequence_history_.isEmpty()) return;
  SequencePtr prev = sequence_history_.takeLast();

  panel_graph_editor->set_row(nullptr);
  panel_effect_controls->Clear(true);

  amber::ActiveSequence = prev;
  panel_sequence_viewer->set_main_sequence();
  panel_timeline->update_sequence();
  panel_timeline->setFocus();
}

bool OliveGlobal::can_go_back() const
{
  return !sequence_history_.isEmpty();
}

const QVector<SequencePtr>& OliveGlobal::sequence_history() const
{
  return sequence_history_;
}

void OliveGlobal::clear_sequence_history()
{
  sequence_history_.clear();
}

void OliveGlobal::OpenProjectWorker(QString fn, bool autorecovery) {
  ClearProject();
  update_project_filename(fn);
  LoadProject(fn, autorecovery);
  amber::UndoStack.clear();
}

bool OliveGlobal::CheckForActiveSequence(bool show_msg)
{
  if (amber::ActiveSequence == nullptr) {

    if (show_msg) {
      QMessageBox::information(amber::MainWindow,
                               tr("No active sequence"),
                               tr("Please open the sequence to perform this action."),
                               QMessageBox::Ok);
    }

    return false;
  }
  return true;
}

void OliveGlobal::undo() {
  // workaround to prevent crash (and also users should never need to do this)
  if (!panel_timeline->importing) {
    amber::UndoStack.undo();
    update_ui(true);
  }
}

void OliveGlobal::redo() {
  // workaround to prevent crash (and also users should never need to do this)
  if (!panel_timeline->importing) {
    amber::UndoStack.redo();
    update_ui(true);
  }
}

void OliveGlobal::paste() {
  if (amber::ActiveSequence != nullptr) {
    panel_timeline->paste(false);
  }
}

void OliveGlobal::paste_insert() {
  if (amber::ActiveSequence != nullptr) {
    panel_timeline->paste(true);
  }
}

void OliveGlobal::open_about_dialog() {
  AboutDialog a(amber::MainWindow);
  a.exec();
}

void OliveGlobal::open_debug_log() {
  if (amber::DebugDialog != nullptr) amber::DebugDialog->show();
}

void OliveGlobal::open_speed_dialog() {
  if (amber::ActiveSequence != nullptr) {

    QVector<Clip*> selected_clips = amber::ActiveSequence->SelectedClips();

    if (!selected_clips.isEmpty()) {
      SpeedDialog s(amber::MainWindow, selected_clips);
      s.exec();
    }
  }
}

void OliveGlobal::open_autocut_silence_dialog() {
  if (CheckForActiveSequence()) {

    QVector<int> selected_clips = amber::ActiveSequence->SelectedClipIndexes();

    if (selected_clips.isEmpty()) {
      QMessageBox::critical(amber::MainWindow,
                            tr("No clips selected"),
                            tr("Select the clips you wish to auto-cut"),
                            QMessageBox::Ok);
    } else {
      AutoCutSilenceDialog s(amber::MainWindow, selected_clips);
      s.exec();
    }

  }
}

void OliveGlobal::clear_undo_stack() {
  amber::UndoStack.clear();
}

void OliveGlobal::open_action_search() {
  ActionSearch as(amber::MainWindow);
  as.exec();
}
