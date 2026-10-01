#include "appcontextimpl.h"

#include "global/global.h"
#include "global/projectio.h"
#include "panels/effectcontrols.h"
#include "panels/grapheditor.h"
#include "panels/panels.h"
#include "panels/project.h"
#include "panels/timeline.h"
#include "panels/viewer.h"
#include "project/media.h"
#include "ui/audiomonitor.h"
#include "ui/icons.h"
#include "ui/mediaiconservice.h"
#include "ui/viewerwidget.h"

#include <QApplication>
#include <QFileDialog>
#include <QMessageBox>
#include <QThread>
#include "ui/mainwindow.h"

void AppContextImpl::refreshViewer() {
  if (panel_sequence_viewer != nullptr) {
    panel_sequence_viewer->viewer_widget->frame_update();
  }
}

void AppContextImpl::refreshEffectControls() {
  if (panel_effect_controls != nullptr) {
    panel_effect_controls->Reload();
  }
}

void AppContextImpl::updateKeyframes() {
  if (panel_effect_controls != nullptr) {
    panel_effect_controls->update_keyframes();
  }
}

void AppContextImpl::clearEffectControls(bool clear_cache) {
  if (panel_effect_controls != nullptr) {
    panel_effect_controls->Clear(clear_cache);
  }
}

void AppContextImpl::refreshTimeline() {
  if (panel_timeline != nullptr) {
    panel_timeline->repaint_timeline();
  }
}

void AppContextImpl::deselectArea(long in, long out, int track) {
  if (panel_timeline != nullptr) {
    panel_timeline->deselect_area(in, out, track);
  }
}

void AppContextImpl::seekPlayhead(long frame) {
  if (panel_sequence_viewer != nullptr) {
    panel_sequence_viewer->seek(frame, false);
  }
}

void AppContextImpl::pausePlayback() {
  if (panel_sequence_viewer != nullptr) {
    panel_sequence_viewer->pause();
  }
}

void AppContextImpl::setGraphEditorRow(EffectRow* row) {
  if (panel_graph_editor != nullptr) {
    panel_graph_editor->set_row(row);
  }
}

bool AppContextImpl::isPlaying() {
  return (panel_sequence_viewer != nullptr && panel_sequence_viewer->playing) ||
         (panel_footage_viewer != nullptr && panel_footage_viewer->playing);
}

bool AppContextImpl::isEffectSelected(Effect* e) {
  if (panel_effect_controls != nullptr) {
    return panel_effect_controls->IsEffectSelected(e);
  }
  return false;
}

void AppContextImpl::setAudioMonitorValues(const QVector<double>& values) {
  if (panel_timeline != nullptr && panel_timeline->audio_monitor != nullptr) {
    panel_timeline->audio_monitor->set_value(values);
  }
}

QVector<Media*> AppContextImpl::listAllSequences() {
  if (panel_project != nullptr) {
    return panel_project->list_all_project_sequences();
  }
  return {};
}

void AppContextImpl::processFileList(QStringList& files, bool recursive, MediaPtr replace, Media* parent) {
  if (panel_project != nullptr) {
    panel_project->process_file_list(files, recursive, replace, parent);
  }
}

bool AppContextImpl::isToolbarVisible() {
  if (panel_project != nullptr) {
    return panel_project->IsToolbarVisible();
  }
  return false;
}

bool AppContextImpl::isModified() { return amber::Global->is_modified(); }

void AppContextImpl::setModified(bool modified) { amber::Global->set_modified(modified); }

void AppContextImpl::updateUi(bool modified) { update_ui(modified); }

static int exec_message_box(const QString& title, const QString& text, int type) {
  QMessageBox box;
  box.setWindowTitle(title);
  box.setText(text);
  box.setIcon(static_cast<QMessageBox::Icon>(type));
  box.addButton(QMessageBox::Ok);
  return box.exec();
}

int AppContextImpl::showMessage(const QString& title, const QString& text, int type) {
  if (QThread::currentThread() != qApp->thread()) {
    // Off the GUI thread (e.g. an effect constructed by LoadThread): widgets must not be created here and the caller
    // must not block on the user, so show the box later on the GUI thread and return as if OK was clicked.
    QMetaObject::invokeMethod(
        qApp, [title, text, type]() { exec_message_box(title, text, type); }, Qt::QueuedConnection);
    return QMessageBox::Ok;
  }
  return exec_message_box(title, text, type);
}

bool AppContextImpl::showQuestion(const QString& title, const QString& text, bool warning) {
  if (warning) {
    return QMessageBox::warning(amber::MainWindow, title, text, QMessageBox::Yes | QMessageBox::No) == QMessageBox::Yes;
  }
  return QMessageBox::question(nullptr, title, text, QMessageBox::Yes, QMessageBox::No) == QMessageBox::Yes;
}

QString AppContextImpl::showSaveFileDialog(const QString& title, const QString& filter) {
  return QFileDialog::getSaveFileName(amber::MainWindow, title, QString(), filter);
}

QString AppContextImpl::showOpenFileDialog(const QString& title, const QString& filter) {
  return QFileDialog::getOpenFileName(amber::MainWindow, title, QString(), filter);
}

void AppContextImpl::clearViewerMedia() {
  if (panel_sequence_viewer != nullptr) {
    panel_sequence_viewer->set_media(nullptr);
  }
  if (panel_footage_viewer != nullptr) {
    panel_footage_viewer->set_media(nullptr);
  }
}

QWidget* AppContextImpl::getMainWindow() { return amber::MainWindow; }

QWindow* AppContextImpl::getMainWindowHandle() {
  return amber::MainWindow != nullptr ? amber::MainWindow->windowHandle() : nullptr;
}

void AppContextImpl::clearGraphEditorIfRow(EffectRow* row) {
  if (panel_graph_editor != nullptr && panel_graph_editor->get_row() == row) {
    panel_graph_editor->set_row(nullptr);
  }
}

void AppContextImpl::addRecentProject(const QString& filename) { amber::project_io->addRecentProject(filename); }

void AppContextImpl::setMediaIcon(Media* media, int icon_type) {
  if (amber::media_icon_service) amber::media_icon_service->SetMediaIcon(media, icon_type);
}

void AppContextImpl::stopMediaThrobber(Media* media) {
  if (amber::media_icon_service) amber::media_icon_service->StopThrobberFor(media);
}

QIcon AppContextImpl::mediaTypeIcon(int media_type) {
  switch (media_type) {
    case MEDIA_TYPE_SEQUENCE:
      return amber::icon::MediaSequence;
    case MEDIA_TYPE_FOLDER:
      return amber::icon::MediaFolder;
  }
  return QIcon();
}
