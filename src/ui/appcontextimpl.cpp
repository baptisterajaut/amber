#include "appcontextimpl.h"

#include "global/global.h"
#include "panels/effectcontrols.h"
#include "panels/grapheditor.h"
#include "panels/panels.h"
#include "panels/project.h"
#include "panels/timeline.h"
#include "panels/viewer.h"
#include "ui/audiomonitor.h"
#include "ui/viewerwidget.h"

#include <QMessageBox>

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
    panel_sequence_viewer->seek(frame);
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

bool AppContextImpl::isModified() {
  return amber::Global->is_modified();
}

void AppContextImpl::setModified(bool modified) {
  amber::Global->set_modified(modified);
}

void AppContextImpl::updateUi(bool modified) {
  update_ui(modified);
}

int AppContextImpl::showMessage(const QString& title, const QString& text, int type) {
  QMessageBox box;
  box.setWindowTitle(title);
  box.setText(text);
  box.setIcon(static_cast<QMessageBox::Icon>(type));
  box.addButton(QMessageBox::Ok);
  return box.exec();
}
