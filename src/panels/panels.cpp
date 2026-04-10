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

#include "panels.h"

#include "effects/effectloaders.h"
#include "effects/transition.h"
#include "engine/clip.h"
#include "engine/sequence.h"
#include "global/config.h"
#include "global/debug.h"

#include <QCoreApplication>
#include <QScrollBar>

Project* panel_project = nullptr;
EffectControls* panel_effect_controls = nullptr;
Viewer* panel_sequence_viewer = nullptr;
Viewer* panel_footage_viewer = nullptr;
Timeline* panel_timeline = nullptr;
GraphEditor* panel_graph_editor = nullptr;
UndoHistoryPanel* panel_undo_history = nullptr;

void update_ui(bool modified, bool scrubbing) {
  if (!scrubbing && panel_effect_controls != nullptr) {
    if (modified) {
      panel_effect_controls->SetClips();
    }
    panel_effect_controls->update_keyframes();
  }
  if (panel_timeline != nullptr) panel_timeline->repaint_timeline();
  if (panel_sequence_viewer != nullptr) panel_sequence_viewer->update_viewer();
  if (!scrubbing && panel_graph_editor != nullptr) panel_graph_editor->update_panel();
}

QDockWidget* get_focused_panel(bool force_hover) {
  if (amber::CurrentConfig.hover_focus || force_hover) {
    QDockWidget* panels_hover[] = {panel_project, panel_effect_controls, panel_sequence_viewer, panel_footage_viewer,
                                   panel_timeline};
    for (auto* p : panels_hover) {
      if (p->underMouse()) return p;
    }
    if (panel_graph_editor->view_is_under_mouse()) return panel_graph_editor;
  }

  if (panel_project->is_focused()) return panel_project;
  if (panel_effect_controls->keyframe_focus() || panel_effect_controls->is_focused()) return panel_effect_controls;
  if (panel_sequence_viewer->is_focused()) return panel_sequence_viewer;
  if (panel_footage_viewer->is_focused()) return panel_footage_viewer;
  if (panel_timeline->focused()) return panel_timeline;
  if (panel_graph_editor->view_is_focused()) return panel_graph_editor;
  return nullptr;
}

void alloc_panels(QWidget* parent) {
  panel_sequence_viewer = new Viewer(parent);
  panel_sequence_viewer->setObjectName("seq_viewer");
  panel_footage_viewer = new Viewer(parent);
  panel_footage_viewer->setObjectName("footage_viewer");
  panel_footage_viewer->show_videoaudio_buttons(true);
  panel_project = new Project(parent);
  panel_project->setObjectName("proj_root");
  panel_effect_controls = new EffectControls(parent);
  EffectInit::StartLoading();
  panel_effect_controls->setObjectName("fx_controls");
  panel_timeline = new Timeline(parent);
  panel_timeline->setObjectName("timeline");
  panel_graph_editor = new GraphEditor(parent);
  panel_graph_editor->setObjectName("graph_editor");
  panel_undo_history = new UndoHistoryPanel(parent);
  panel_undo_history->hide();
}

void free_panels() {
  delete panel_sequence_viewer;
  panel_sequence_viewer = nullptr;
  delete panel_footage_viewer;
  panel_footage_viewer = nullptr;
  delete panel_project;
  panel_project = nullptr;
  delete panel_effect_controls;
  panel_effect_controls = nullptr;
  delete panel_timeline;
  panel_timeline = nullptr;
  delete panel_undo_history;
  panel_undo_history = nullptr;
}

void scroll_to_frame_internal(QScrollBar* bar, long frame, double zoom, int area_width) {
  if (bar->value() == bar->minimum() || bar->value() == bar->maximum()) {
    return;
  }

  int screen_point = getScreenPointFromFrame(zoom, frame) - bar->value();
  int min_x = area_width * 0.1;
  int max_x = area_width - min_x;
  if (screen_point < min_x) {
    bar->setValue(getScreenPointFromFrame(zoom, frame) - min_x);
  } else if (screen_point > max_x) {
    bar->setValue(getScreenPointFromFrame(zoom, frame) - max_x);
  }
}
