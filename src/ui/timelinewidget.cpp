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

#include "timelinewidget.h"

#include <QPainter>
#include <QColor>
#include <QMouseEvent>
#include <QObject>
#include <QVariant>
#include <QPoint>
#include <QMessageBox>
#include <QtMath>
#include <QScrollBar>
#include <QMimeData>
#include <QToolTip>
#include <QInputDialog>
#include <QStatusBar>

#include "global/global.h"
#include "panels/panels.h"
#include "project/projectelements.h"
#include "rendering/audio.h"
#include "global/config.h"
#include "ui/sourcetable.h"
#include "ui/sourceiconview.h"
#include "engine/undo/undo.h"
#include "engine/undo/undostack.h"
#include "ui/viewerwidget.h"
#include "ui/resizablescrollbar.h"
#include "dialogs/newsequencedialog.h"
#include "mainwindow.h"
#include "ui/rectangleselect.h"
#include "rendering/renderfunctions.h"
#include "ui/cursors.h"
#include "ui/menuhelper.h"
#include "ui/menu.h"
#include "ui/focusfilter.h"
#include "dialogs/clippropertiesdialog.h"
#include "global/debug.h"
#include "effects/effect.h"
#include "effects/internal/solideffect.h"

#define MAX_TEXT_WIDTH 20
#define TRANSITION_BETWEEN_RANGE 40

TimelineWidget::TimelineWidget(QWidget *parent) : QWidget(parent) {
  selection_command = nullptr;
  self_created_sequence = nullptr;
  scroll = 0;

  track_resizing = false;
  setMouseTracking(true);

  setAcceptDrops(true);

  setContextMenuPolicy(Qt::CustomContextMenu);
  connect(this, &TimelineWidget::customContextMenuRequested, this, &TimelineWidget::show_context_menu);

  tooltip_timer.setInterval(500);
  connect(&tooltip_timer, &QTimer::timeout, this, &TimelineWidget::tooltip_timer_timeout);
}

// Defined in timelinewidget_ghosts.cpp
void make_room_for_transition(ComboAction* ca, Clip* c, int type, long transition_start, long transition_end, bool delete_old_transitions, long timeline_in = -1, long timeline_out = -1);
void VerifyTransitionsAfterCreating(ComboAction* ca, Clip* open, Clip* close, long transition_start, long transition_end);
void validate_transitions(Clip* c, int transition_type, long& frame_diff);

bool same_sign(int a, int b) {
  return (a < 0) == (b < 0);
}

void TimelineWidget::dragEnterEvent(QDragEnterEvent *event) {
  bool import_init = false;

  QVector<amber::timeline::MediaImportData> media_list;
  panel_timeline->importing_files = false;

  if (panel_project->IsProjectWidget(event->source())) {
    QModelIndexList items = panel_project->get_current_selected();
    media_list.resize(items.size());
    for (int i=0;i<items.size();i++) {
      media_list[i] = panel_project->item_to_media(items.at(i));
    }
    import_init = true;
  }

  if (event->source() == panel_footage_viewer) {
    if (panel_footage_viewer->seq != amber::ActiveSequence) { // don't allow nesting the same sequence

      media_list.append(amber::timeline::MediaImportData(panel_footage_viewer->media,
                         static_cast<amber::timeline::MediaImportType>(event->mimeData()->text().toInt())));
      import_init = true;

    }
  }

  if (amber::CurrentConfig.enable_drag_files_to_timeline && event->mimeData()->hasUrls()) {
    QList<QUrl> urls = event->mimeData()->urls();
    if (!urls.isEmpty()) {
      QStringList file_list;

      for (const auto & url : urls) {
        file_list.append(url.toLocalFile());
      }

      panel_project->process_file_list(file_list);

      for (auto i : panel_project->last_imported_media) {
        Footage* f = i->to_footage();

        // waits for media to have a duration
        // TODO would be much nicer if this was multithreaded
        f->ready_lock.lock();
        f->ready_lock.unlock();

        if (f->ready) {
          media_list.append(i);
        }
      }

      if (media_list.isEmpty()) {
        amber::UndoStack.undo();
      } else {
        import_init = true;
        panel_timeline->importing_files = true;
      }
    }
  }

  if (import_init) {
    event->acceptProposedAction();

    long entry_point;
    Sequence* seq = amber::ActiveSequence.get();

    if (seq == nullptr) {
      // if no sequence, we're going to create a new one using the clips as a reference
      entry_point = 0;

      self_created_sequence = create_sequence_from_media(media_list);
      seq = self_created_sequence.get();
    } else {
      entry_point = panel_timeline->getTimelineFrameFromScreenPoint(event->position().toPoint().x());
      panel_timeline->drag_frame_start = entry_point + getFrameFromScreenPoint(panel_timeline->zoom, 50);
      // Use the scroll-aware lookup; collapse to V1 (-1) or A1 (0) based on which side the cursor lies on.
      const int t = getTrackFromScreenPoint(event->position().toPoint().y());
      panel_timeline->drag_track_start = (t < 0) ? -1 : 0;
    }

    panel_timeline->create_ghosts_from_media(seq, entry_point, media_list);

    panel_timeline->importing = true;
  }
}

void TimelineWidget::dragMoveEvent(QDragMoveEvent *event) {
  if (panel_timeline->importing) {
    event->acceptProposedAction();

    if (amber::ActiveSequence != nullptr) {
      QPoint pos = event->position().toPoint();
      panel_timeline->scroll_to_frame(panel_timeline->getTimelineFrameFromScreenPoint(event->position().toPoint().x()));
      update_ghosts(pos, event->modifiers() & Qt::ShiftModifier);
      panel_timeline->move_insert = ((event->modifiers() & Qt::ControlModifier) && (panel_timeline->tool == TIMELINE_TOOL_POINTER || panel_timeline->tool == TIMELINE_TOOL_TRACK_SELECT || panel_timeline->importing));
      update_ui(false);
    }
  }
}

void TimelineWidget::wheelEvent(QWheelEvent *event) {

  // TODO: implement pixel scrolling

  bool shift = (event->modifiers() & Qt::ShiftModifier);
  bool ctrl = (event->modifiers() & Qt::ControlModifier);
  bool alt = (event->modifiers() & Qt::AltModifier);

  // "Scroll Zooms" false + Control up  : not zooming
  // "Scroll Zooms" false + Control down:     zooming
  // "Scroll Zooms" true  + Control up  :     zooming
  // "Scroll Zooms" true  + Control down: not zooming
  bool zooming = (amber::CurrentConfig.scroll_zooms != ctrl);

  // Trackpads provide native 2D scrolling — never auto-swap axes for them,
  // only honor explicit Shift toggle.  Mouse wheels only have vertical
  // scroll, so the config swap maps vertical to horizontal panning.
  // Use device type (works on both Wayland and X11/XInput2) with pixelDelta
  // as fallback (populated on Wayland even for generic device types).
  bool is_trackpad = (event->device()
                      && event->device()->type() == QInputDevice::DeviceType::TouchPad)
                     || !event->pixelDelta().isNull();
  bool cfg_swap = is_trackpad ? false : amber::CurrentConfig.invert_timeline_scroll_axes;

  // Allow shift for axis swap, but don't swap on zoom... Unless
  // we need to override Qt's axis swap via Alt
  bool swap_hv = ((shift != cfg_swap) &
                  !zooming) | (alt & !shift & zooming);

  int delta_h = swap_hv ? event->angleDelta().y() : event->angleDelta().x();
  int delta_v = swap_hv ? event->angleDelta().x() : event->angleDelta().y();

  if (zooming) {

    // Zoom only uses vertical scrolling, to avoid glitches on touchpads.
    // Don't do anything if not scrolling vertically.

    if (delta_v != 0) {

      // delta_v == 120 for one click of a mousewheel. Less or more for a
      // touchpad gesture. Calculate speed to compensate.
      // 120 = ratio of 4/3 (1.33), -120 = ratio of 3/4 (.75)

      double zoom_ratio = 1.0 + (abs(delta_v) * 0.33 / 120);

      if (delta_v < 0) {
        zoom_ratio = 1.0 / zoom_ratio;
      }

      panel_timeline->multiply_zoom(zoom_ratio);
    }

  } else {

    // Use the Timeline's main scrollbar for horizontal scrolling, and this
    // widget's scrollbar for vertical scrolling.

    QScrollBar* bar_v = scrollBar;
    QScrollBar* bar_h = panel_timeline->horizontalScrollBar;

    // Match the wheel events to the size of a step as per
    // https://doc.qt.io/qt-5/qwheelevent.html#angleDelta

    int step_h = bar_h->singleStep() * delta_h / -120;
    int step_v = bar_v->singleStep() * delta_v / -120;

    // Apply to appropriate scrollbars

    bar_h->setValue(bar_h->value() + step_h);
    bar_v->setValue(bar_v->value() + step_v);
  }
}

void TimelineWidget::dragLeaveEvent(QDragLeaveEvent* event) {
  event->accept();
  if (panel_timeline->importing) {
    if (panel_timeline->importing_files) {
      amber::UndoStack.undo();
    }
    panel_timeline->importing_files = false;
    panel_timeline->ghosts.clear();
    panel_timeline->importing = false;
    update_ui(false);
  }
  if (self_created_sequence != nullptr) {
    self_created_sequence.reset();
    self_created_sequence = nullptr;
  }
}

void delete_area_under_ghosts(ComboAction* ca) {
  if (!ca) {
    qWarning() << "delete_area_under_ghosts: ca is null";
    return;
  }
  // delete areas before adding
  QVector<Selection> delete_areas;
  for (const auto & g : panel_timeline->ghosts) {
    Selection sel;
    sel.in = g.in;
    sel.out = g.out;
    sel.track = g.track;
    delete_areas.append(sel);
  }
  panel_timeline->delete_areas_and_relink(ca, delete_areas, false);
}

void insert_clips(ComboAction* ca) {
  if (!ca) {
    qWarning() << "insert_clips: ca is null";
    return;
  }
  bool ripple_old_point = true;

  long earliest_old_point = LONG_MAX;
  long latest_old_point = LONG_MIN;

  long earliest_new_point = LONG_MAX;
  long latest_new_point = LONG_MIN;

  QVector<int> ignore_clips;
  for (const auto & g : panel_timeline->ghosts) {
    earliest_old_point = qMin(earliest_old_point, g.old_in);
    latest_old_point = qMax(latest_old_point, g.old_out);
    earliest_new_point = qMin(earliest_new_point, g.in);
    latest_new_point = qMax(latest_new_point, g.out);

    if (g.clip >= 0) {
      ignore_clips.append(g.clip);
    } else {
      // don't try to close old gap if importing
      ripple_old_point = false;
    }
  }

  panel_timeline->split_cache.clear();

  for (int i=0;i<amber::ActiveSequence->clips.size();i++) {
    ClipPtr c = amber::ActiveSequence->clips.at(i);
    if (c != nullptr) {
      // don't split any clips that are moving
      bool found = false;
      for (const auto & ghost : panel_timeline->ghosts) {
        if (ghost.clip == i) {
          found = true;
          break;
        }
      }
      if (!found) {
        if (c->timeline_in() < earliest_new_point && c->timeline_out() > earliest_new_point) {
          panel_timeline->split_clip_and_relink(ca, i, earliest_new_point, true);
        }

        // determine if we should close the gap the old clips left behind
        if (ripple_old_point
            && !((c->timeline_in() < earliest_old_point && c->timeline_out() <= earliest_old_point) || (c->timeline_in() >= latest_old_point && c->timeline_out() > latest_old_point))
            && !ignore_clips.contains(i)) {
          ripple_old_point = false;
        }
      }
    }
  }

  long ripple_length = (latest_new_point - earliest_new_point);

  ripple_clips(ca, amber::ActiveSequence.get(), earliest_new_point, ripple_length, ignore_clips);

  if (ripple_old_point) {
    // works for moving later clips earlier but not earlier to later
    long second_ripple_length = (earliest_old_point - latest_old_point);

    ripple_clips(ca, amber::ActiveSequence.get(), latest_old_point, second_ripple_length, ignore_clips);

    if (earliest_old_point < earliest_new_point) {
      for (auto & g : panel_timeline->ghosts) {
        g.in += second_ripple_length;
        g.out += second_ripple_length;
      }
      for (auto & s : amber::ActiveSequence->selections) {
        s.in += second_ripple_length;
        s.out += second_ripple_length;
      }
    }
  }
}

void TimelineWidget::dropEvent(QDropEvent* event) {
  if (panel_timeline->importing && panel_timeline->ghosts.size() > 0) {
    ComboAction* ca = new ComboAction(tr("Add Clip(s)"));

    Sequence* s = amber::ActiveSequence.get();

    // if we're dropping into nothing, create a new sequences based on the clip being dragged
    if (s == nullptr) {
      QMessageBox mbox(this);

      mbox.setWindowTitle(tr("New Sequence"));
      mbox.setText(tr("No sequence has been created yet. Would you like to make one based on this footage or set "
                      "custom parameters?"));
      mbox.addButton(tr("Use Footage Parameters"), QMessageBox::YesRole);
      QAbstractButton* custom_param_btn = mbox.addButton(tr("Custom Parameters"), QMessageBox::NoRole);
      QAbstractButton* cancel_btn = mbox.addButton(QMessageBox::Cancel);

      mbox.exec();

      s = self_created_sequence.get();
      double old_fr = s->frame_rate;

      if (mbox.clickedButton() == cancel_btn
          || (mbox.clickedButton() == custom_param_btn && NewSequenceDialog(this, nullptr, s).exec() == QDialog::Rejected)) {
        delete ca;
        self_created_sequence = nullptr;
        event->ignore();
        return;
      }

      if (mbox.clickedButton() == custom_param_btn
          && !qFuzzyCompare(old_fr, s->frame_rate)) {
        // If we're here, the user changed the frame rate so all the ghosts will need adjustment
        for (auto & g : panel_timeline->ghosts) {
          g.in = rescale_frame_number(g.in, old_fr, s->frame_rate);
          g.out = rescale_frame_number(g.out, old_fr, s->frame_rate);
          g.clip_in = rescale_frame_number(g.clip_in, old_fr, s->frame_rate);
        }
      }

      panel_project->create_sequence_internal(ca, self_created_sequence, true, nullptr);
      self_created_sequence = nullptr;

    } else if (event->modifiers() & Qt::ControlModifier) {
      insert_clips(ca);
    } else {
      delete_area_under_ghosts(ca);
    }

    panel_timeline->add_clips_from_ghosts(ca, s);

    amber::UndoStack.push(ca);

    setFocus();

    update_ui(true);

    event->acceptProposedAction();
  }
}

void TimelineWidget::mouseDoubleClickEvent(QMouseEvent *event) {
  if (amber::ActiveSequence != nullptr) {
    if (panel_timeline->tool == TIMELINE_TOOL_EDIT) {
      int clip_index = getClipIndexFromCoords(panel_timeline->cursor_frame, panel_timeline->cursor_track);
      if (clip_index >= 0) {
        ClipPtr clip = amber::ActiveSequence->clips.at(clip_index);
        if (clip == nullptr) return;
        if (!(event->modifiers() & Qt::ShiftModifier)) amber::ActiveSequence->selections.clear();
        Selection s;
        s.in = clip->timeline_in();
        s.out = clip->timeline_out();
        s.track = clip->track();
        amber::ActiveSequence->selections.append(s);
        update_ui(false);
      }
    } else if (panel_timeline->tool == TIMELINE_TOOL_POINTER) {
      int clip_index = getClipIndexFromCoords(panel_timeline->cursor_frame, panel_timeline->cursor_track);
      if (clip_index >= 0) {
        ClipPtr c = amber::ActiveSequence->clips.at(clip_index);
        if (c != nullptr && c->media() != nullptr && c->media()->get_type() == MEDIA_TYPE_SEQUENCE) {
          SequencePtr nested = c->media()->to_sequence();
          long ph = amber::ActiveSequence->playhead;
          if (ph >= c->timeline_in() && ph < c->timeline_out()) {
            long mapped = ph + c->clip_in(true) - c->timeline_in(true);
            mapped = rescale_frame_number(mapped, amber::ActiveSequence->frame_rate, nested->frame_rate);
            nested->playhead = mapped;
          }
          amber::Global->set_sequence(nested, true);
        }
      } else {
        // Double-click on empty space: go back to parent sequence
        amber::Global->go_back_sequence();
      }
    }
  }
}

bool current_tool_shows_cursor() {
  return (panel_timeline->tool == TIMELINE_TOOL_EDIT || panel_timeline->tool == TIMELINE_TOOL_RAZOR || panel_timeline->creating);
}

void TimelineWidget::mousePressCreating() {
  int comp = 0;
  switch (panel_timeline->creating_object) {
  case ADD_OBJ_TITLE:
  case ADD_OBJ_SOLID:
  case ADD_OBJ_BARS:
    comp = -1;
    break;
  case ADD_OBJ_TONE:
  case ADD_OBJ_NOISE:
  case ADD_OBJ_AUDIO:
    comp = 1;
    break;
  }

  // if the track the user clicked is correct for the type of object we're adding
  if ((panel_timeline->drag_track_start < 0) == (comp < 0)) {
    Ghost g;
    g.in = g.old_in = g.out = g.old_out = panel_timeline->drag_frame_start;
    g.track = g.old_track = panel_timeline->drag_track_start;
    g.transition = nullptr;
    g.clip = -1;
    g.trim_type = TRIM_OUT;
    panel_timeline->ghosts.append(g);

    panel_timeline->moving_init = true;
    panel_timeline->moving_proc = true;
  }
}

void TimelineWidget::mousePressPointer(int hovered_clip, bool shift, bool alt, int effective_tool) {
  if (track_resizing && effective_tool != TIMELINE_TOOL_MENU) {

    // if the cursor is currently hovering over a track, init track resizing
    panel_timeline->moving_init = true;

  } else {

    // check if we're currently hovering over a clip or not
    if (hovered_clip >= 0) {
      Clip* clip = amber::ActiveSequence->clips.at(hovered_clip).get();

      if (clip->IsSelected()) {

        if (shift) {

          // if the user clicks a selected clip while holding shift, deselect the clip
          panel_timeline->deselect_area(clip->timeline_in(), clip->timeline_out(), clip->track());

          // if the user isn't holding alt, also deselect all of its links as well
          if (!alt) {
            for (int i : clip->linked) {
              ClipPtr link = amber::ActiveSequence->clips.at(i);
              panel_timeline->deselect_area(link->timeline_in(), link->timeline_out(), link->track());
            }
          }

        } else if (panel_timeline->tool == TIMELINE_TOOL_POINTER
                    && panel_timeline->transition_select != kTransitionNone) {

          // if the clip was selected by then the user clicked a transition, de-select the clip and its links
          // and select the transition only

          panel_timeline->deselect_area(clip->timeline_in(), clip->timeline_out(), clip->track());

          for (int i : clip->linked) {
            ClipPtr link = amber::ActiveSequence->clips.at(i);
            panel_timeline->deselect_area(link->timeline_in(), link->timeline_out(), link->track());
          }

          Selection s;
          s.track = clip->track();

          // select the transition only
          if (panel_timeline->transition_select == kTransitionOpening && clip->opening_transition != nullptr) {
            s.in = clip->timeline_in();

            if (clip->opening_transition->secondary_clip != nullptr) {
              s.in -= clip->opening_transition->get_true_length();
            }

            s.out = clip->timeline_in() + clip->opening_transition->get_true_length();
          } else if (panel_timeline->transition_select == kTransitionClosing && clip->closing_transition != nullptr) {
            s.in = clip->timeline_out() - clip->closing_transition->get_true_length();
            s.out = clip->timeline_out();

            if (clip->closing_transition->secondary_clip != nullptr) {
              s.out += clip->closing_transition->get_true_length();
            }
          }
          amber::ActiveSequence->selections.append(s);
        }

        if (amber::CurrentConfig.select_also_seeks) {
          panel_sequence_viewer->seek(panel_timeline->drag_frame_start);
        }
      } else {

        // if the clip is not already selected

        // if shift is NOT down, we change clear all current selections
        if (!shift) {
          amber::ActiveSequence->selections.clear();
        }

        Selection s;

        s.in = clip->timeline_in();
        s.out = clip->timeline_out();
        s.track = clip->track();

        // if user is using the pointer tool, they may be trying to select a transition
        // check if the use is hovering over a transition
        if (panel_timeline->tool == TIMELINE_TOOL_POINTER) {
          if (panel_timeline->transition_select == kTransitionOpening && clip->opening_transition != nullptr) {
            // move the selection to only select the transitoin
            s.out = clip->timeline_in() + clip->opening_transition->get_true_length();

            // if the transition is a "shared" transition, adjust the selection to select both sides
            if (clip->opening_transition->secondary_clip != nullptr) {
              s.in -= clip->opening_transition->get_true_length();
            }
          } else if (panel_timeline->transition_select == kTransitionClosing && clip->closing_transition != nullptr) {
            // move the selection to only select the transitoin
            s.in = clip->timeline_out() - clip->closing_transition->get_true_length();

            // if the transition is a "shared" transition, adjust the selection to select both sides
            if (clip->closing_transition->secondary_clip != nullptr) {
              s.out += clip->closing_transition->get_true_length();
            }
          }
        }

        // add the selection to the array
        amber::ActiveSequence->selections.append(s);

        // if the config is set to also seek with selections, do so now
        if (amber::CurrentConfig.select_also_seeks) {
          panel_sequence_viewer->seek(panel_timeline->drag_frame_start);
        }

        // if alt is not down, select links (provided we're not selecting transitions)
        if (!alt && panel_timeline->transition_select == kTransitionNone) {

          for (int i : clip->linked) {

            Clip* link = amber::ActiveSequence->clips.at(i).get();

            // check if the clip is already selected
            if (!link->IsSelected()) {
              Selection ss;
              ss.in = link->timeline_in();
              ss.out = link->timeline_out();
              ss.track = link->track();
              amber::ActiveSequence->selections.append(ss);
            }

          }

        }
      }

      // authorize the starting of a move action if the mouse moves after this
      if (effective_tool != TIMELINE_TOOL_MENU) {
        panel_timeline->moving_init = true;
      }

    } else {

      // if the user did not click a clip at all, we start a rectangle selection

      if (!shift) {
        amber::ActiveSequence->selections.clear();
      }

      panel_timeline->rect_select_init = true;
    }

    // update everything
    update_ui(false);
  }
}

void TimelineWidget::mousePressEvent(QMouseEvent *event) {
  if (amber::ActiveSequence != nullptr) {

    int effective_tool = panel_timeline->tool;

    // some user actions will override which tool we'll be using
    if (event->button() == Qt::MiddleButton) {
      effective_tool = TIMELINE_TOOL_HAND;
      panel_timeline->creating = false;
    } else if (event->button() == Qt::RightButton) {
      effective_tool = TIMELINE_TOOL_MENU;
      panel_timeline->creating = false;
    }

    // ensure cursor_frame and cursor_track are up to date
    mouseMoveEvent(event);

    // store current cursor positions
    panel_timeline->drag_x_start = event->position().toPoint().x();
    panel_timeline->drag_y_start = event->position().toPoint().y();

    // store current frame/tracks as the values to start dragging from
    panel_timeline->drag_frame_start = panel_timeline->cursor_frame;
    panel_timeline->drag_track_start = panel_timeline->cursor_track;

    // get the clip the user is currently hovering over, priority to trim_target set from mouseMoveEvent
    int hovered_clip = panel_timeline->trim_target == -1 ?
          getClipIndexFromCoords(panel_timeline->cursor_frame, panel_timeline->cursor_track)
        : panel_timeline->trim_target;

    bool shift = (event->modifiers() & Qt::ShiftModifier);
    bool alt = (event->modifiers() & Qt::AltModifier);

    // Normal behavior is to reset selections to zero when clicking, but if Shift is held, we add selections
    // to the existing selections. `selection_offset` is the index to change selections from (and we don't touch
    // any prior to that)
    if (shift) {
      panel_timeline->selection_offset = amber::ActiveSequence->selections.size();
    } else {
      panel_timeline->selection_offset = 0;
    }

    // if the user is creating an object
    if (panel_timeline->creating) {
      mousePressCreating();
    } else {

      // pass through tools to determine what action we'll be starting
      switch (effective_tool) {

      // many tools share pointer-esque behavior
      case TIMELINE_TOOL_POINTER:
      case TIMELINE_TOOL_RIPPLE:
      case TIMELINE_TOOL_SLIP:
      case TIMELINE_TOOL_ROLLING:
      case TIMELINE_TOOL_SLIDE:
      case TIMELINE_TOOL_MENU:
        mousePressPointer(hovered_clip, shift, alt, effective_tool);
        break;
      case TIMELINE_TOOL_TRACK_SELECT:
      {
        // Clear previous selections unless Shift is held
        if (!shift) {
          amber::ActiveSequence->selections.clear();
        }

        long click_frame = panel_timeline->drag_frame_start;

        // Find the end of the sequence (rightmost clip end)
        long sequence_end = 0;
        for (const auto& c : amber::ActiveSequence->clips) {
          if (c != nullptr && c->timeline_out() > sequence_end) {
            sequence_end = c->timeline_out();
          }
        }

        // Only create selections if there's content to the right
        if (sequence_end > click_frame) {
          if (event->modifiers() & Qt::ShiftModifier) {
            // Shift+click: single track only
            Selection s;
            s.in = click_frame;
            s.out = sequence_end;
            s.track = panel_timeline->drag_track_start;
            amber::ActiveSequence->selections.append(s);
          } else {
            // Click: all tracks
            int video_tracks, audio_tracks;
            amber::ActiveSequence->getTrackLimits(&video_tracks, &audio_tracks);

            // Video tracks are negative: -1, -2, ... down to video_tracks
            for (int t = video_tracks; t < 0; t++) {
              Selection s;
              s.in = click_frame;
              s.out = sequence_end;
              s.track = t;
              amber::ActiveSequence->selections.append(s);
            }
            // Audio tracks are positive: 0, 1, ... up to audio_tracks
            for (int t = 0; t <= audio_tracks; t++) {
              Selection s;
              s.in = click_frame;
              s.out = sequence_end;
              s.track = t;
              amber::ActiveSequence->selections.append(s);
            }
          }

          // Enable drag movement (same flow as pointer tool)
          panel_timeline->moving_init = true;
        }

        if (amber::CurrentConfig.select_also_seeks) {
          panel_sequence_viewer->seek(click_frame);
        }

        update_ui(false);
      }
        break;
      case TIMELINE_TOOL_HAND:

        // initiate moving with the hand tool
        panel_timeline->hand_moving = true;

        break;
      case TIMELINE_TOOL_EDIT:

        // if the config is set to seek with the edit tool, do so now
        if (amber::CurrentConfig.edit_tool_also_seeks) {
          panel_sequence_viewer->seek(panel_timeline->drag_frame_start);
        }

        // initiate selecting
        panel_timeline->selecting = true;

        break;
      case TIMELINE_TOOL_RAZOR:
      {

        // initiate razor tool
        panel_timeline->splitting = true;

        // add this track as a track being split by the razor
        panel_timeline->split_tracks.append(panel_timeline->drag_track_start);

        update_ui(false);
      }
        break;
      case TIMELINE_TOOL_TRANSITION:
      {

        // if there is a clip to run the transition tool on, initiate the transition tool
        if (panel_timeline->transition_tool_open_clip > -1
              || panel_timeline->transition_tool_close_clip > -1) {
          panel_timeline->transition_tool_init = true;
        }

      }
        break;
      }
    }
  }
}
bool TimelineWidget::mouseReleaseCreating(ComboAction* ca, bool shift) {
  if (panel_timeline->ghosts.size() == 0) return false;

  const Ghost& g = panel_timeline->ghosts.at(0);

  if (panel_timeline->creating_object == ADD_OBJ_AUDIO) {
    amber::MainWindow->statusBar()->clearMessage();
    panel_sequence_viewer->cue_recording(qMin(g.in, g.out), qMax(g.in, g.out), g.track);
    panel_timeline->creating = false;
    return false;
  }

  if (g.in == g.out) return false;

  bool ctrl = QApplication::keyboardModifiers() & Qt::ControlModifier;

  ClipPtr c = std::make_shared<Clip>(amber::ActiveSequence.get());
  c->set_media(nullptr, 0);
  c->set_timeline_in(qMin(g.in, g.out));
  c->set_timeline_out(qMax(g.in, g.out));
  c->set_clip_in(0);
  c->set_color(192, 192, 64);
  c->set_track(g.track);

  if (ctrl) {
    insert_clips(ca);
  } else {
    Selection s;
    s.in = c->timeline_in();
    s.out = c->timeline_out();
    s.track = c->track();
    QVector<Selection> areas;
    areas.append(s);
    panel_timeline->delete_areas_and_relink(ca, areas, false);
  }

  QVector<ClipPtr> add;
  add.append(c);
  ca->append(new AddClipCommand(amber::ActiveSequence.get(), add));

  if (c->track() < 0 && amber::CurrentConfig.add_default_effects_to_clips) {
    // default video effects (before custom effects)
    c->effects.append(Effect::Create(c.get(), Effect::GetInternalMeta(EFFECT_INTERNAL_TRANSFORM, EFFECT_TYPE_EFFECT)));
  }

  switch (panel_timeline->creating_object) {
  case ADD_OBJ_TITLE:
    c->set_name(tr("Title"));
    c->effects.append(Effect::Create(c.get(), Effect::GetInternalMeta(EFFECT_INTERNAL_RICHTEXT, EFFECT_TYPE_EFFECT)));
    break;
  case ADD_OBJ_SOLID:
    c->set_name(tr("Solid Color"));
    c->effects.append(Effect::Create(c.get(), Effect::GetInternalMeta(EFFECT_INTERNAL_SOLID, EFFECT_TYPE_EFFECT)));
    break;
  case ADD_OBJ_BARS:
  {
    c->set_name(tr("Bars"));
    EffectPtr e = Effect::Create(c.get(), Effect::GetInternalMeta(EFFECT_INTERNAL_SOLID, EFFECT_TYPE_EFFECT));

    // Auto-select bars
    SolidEffect* solid_effect = static_cast<SolidEffect*>(e.get());
    solid_effect->SetType(SolidEffect::SOLID_TYPE_BARS);

    c->effects.append(e);
  }
    break;
  case ADD_OBJ_TONE:
    c->set_name(tr("Tone"));
    c->effects.append(Effect::Create(c.get(), Effect::GetInternalMeta(EFFECT_INTERNAL_TONE, EFFECT_TYPE_EFFECT)));
    break;
  case ADD_OBJ_NOISE:
    c->set_name(tr("Noise"));
    c->effects.append(Effect::Create(c.get(), Effect::GetInternalMeta(EFFECT_INTERNAL_NOISE, EFFECT_TYPE_EFFECT)));
    break;
  }

  if (c->track() >= 0 && amber::CurrentConfig.add_default_effects_to_clips) {
    // default audio effects (after custom effects)
    c->effects.append(Effect::Create(c.get(), Effect::GetInternalMeta(EFFECT_INTERNAL_VOLUME, EFFECT_TYPE_EFFECT)));
    c->effects.append(Effect::Create(c.get(), Effect::GetInternalMeta(EFFECT_INTERNAL_PAN, EFFECT_TYPE_EFFECT)));
  }

  if (!shift) {
    panel_timeline->creating = false;
  }

  return true;
}

bool TimelineWidget::mouseReleaseMoving(ComboAction* ca, bool alt, bool ctrl) {
  // see if any clips actually moved, otherwise we don't need to do any processing
  bool process_moving = false;

  for (const auto & g : panel_timeline->ghosts) {
    if (g.in != g.old_in
        || g.out != g.old_out
        || g.clip_in != g.old_clip_in
        || g.track != g.old_track) {
      process_moving = true;
      break;
    }
  }

  if (!process_moving) return false;

  const Ghost& first_ghost = panel_timeline->ghosts.at(0);

  // start a ripple movement
  if (panel_timeline->tool == TIMELINE_TOOL_RIPPLE) {

    // ripple_length becomes the length/number of frames we trimmed
    // ripple_point is the "axis" around which we move all the clips, any clips after it get moved
    long ripple_length;
    long ripple_point = LONG_MAX;

    if (panel_timeline->trim_type == TRIM_IN) {

      // it's assumed that all the ghosts rippled by the same length, so we just take the difference of the
      // first ghost here
      ripple_length = first_ghost.old_in - first_ghost.in;

      // for in trimming movements we also move the selections forward (unnecessary for out trimming since
      // the selected clips more or less stay in the same place)
      for (auto & selection : amber::ActiveSequence->selections) {
        selection.in += ripple_length;
        selection.out += ripple_length;
      }
    } else {

      // use the out points for length if the user trimmed the out point
      ripple_length = first_ghost.old_out - panel_timeline->ghosts.at(0).out;

    }

    // build a list of "ignore clips" that won't get affected by ripple_clips() below
    QVector<int> ignore_clips;
    for (int i=0;i<panel_timeline->ghosts.size();i++) {
      const Ghost& g = panel_timeline->ghosts.at(i);

      // for the same reason that we pushed selections forward above, for in trimming,
      // we push the ghosts forward here
      if (panel_timeline->trim_type == TRIM_IN) {
        ignore_clips.append(g.clip);
        panel_timeline->ghosts[i].in += ripple_length;
        panel_timeline->ghosts[i].out += ripple_length;
      }

      // find the earliest ripple point
      long comp_point = (panel_timeline->trim_type == TRIM_IN) ? g.old_in : g.old_out;
      ripple_point = qMin(ripple_point, comp_point);
    }

    // if this was out trimming, flip the direction of the ripple
    if (panel_timeline->trim_type == TRIM_OUT) ripple_length = -ripple_length;

    // finally, ripple everything
    ripple_clips(ca, amber::ActiveSequence.get(), ripple_point, ripple_length, ignore_clips);
  }

  if ((panel_timeline->tool == TIMELINE_TOOL_POINTER || panel_timeline->tool == TIMELINE_TOOL_TRACK_SELECT)
      && alt
      && panel_timeline->trim_target == -1) {

    // if the user was holding alt (and not trimming), we duplicate clips rather than move them
    QVector<int> old_clips;
    QVector<ClipPtr> new_clips;
    QVector<Selection> delete_areas;
    for (const auto & g : panel_timeline->ghosts) {
      if (g.old_in != g.in || g.old_out != g.out || g.track != g.old_track || g.clip_in != g.old_clip_in) {

        // create copy of clip
        ClipPtr c = amber::ActiveSequence->clips.at(g.clip)->copy(amber::ActiveSequence.get());

        c->set_timeline_in(g.in);
        c->set_timeline_out(g.out);
        c->set_track(g.track);

        Selection s;
        s.in = g.in;
        s.out = g.out;
        s.track = g.track;
        delete_areas.append(s);

        old_clips.append(g.clip);
        new_clips.append(c);

      }
    }

    if (new_clips.size() > 0) {

      // delete anything under the new clips
      panel_timeline->delete_areas_and_relink(ca, delete_areas, false);

      // relink duplicated clips
      panel_timeline->relink_clips_using_ids(old_clips, new_clips);

      // add them
      ca->append(new AddClipCommand(amber::ActiveSequence.get(), new_clips));

    }

  } else {

    // if we're not holding alt, this will just be a move

    // if the user is holding ctrl, perform an insert rather than an overwrite
    if ((panel_timeline->tool == TIMELINE_TOOL_POINTER || panel_timeline->tool == TIMELINE_TOOL_TRACK_SELECT) && ctrl) {

      insert_clips(ca);

    } else if (panel_timeline->tool == TIMELINE_TOOL_POINTER || panel_timeline->tool == TIMELINE_TOOL_SLIDE || panel_timeline->tool == TIMELINE_TOOL_TRACK_SELECT) {

      // if the user is not holding ctrl, we start standard clip movement

      // delete everything under the new clips
      QVector<Selection> delete_areas;
      for (const auto & g : panel_timeline->ghosts) {
        // step 1 - set clips that are moving to "undeletable" (to avoid step 2 deleting any part of them)
        // set clip to undeletable so it's unaffected by delete_areas_and_relink() below
        amber::ActiveSequence->clips.at(g.clip)->undeletable = true;

        // if the user was moving a transition make sure they're undeletable too
        if (g.transition != nullptr) {
          g.transition->parent_clip->undeletable = true;
          if (g.transition->secondary_clip != nullptr) {
            g.transition->secondary_clip->undeletable = true;
          }
        }

        // set area to delete
        Selection s;
        s.in = g.in;
        s.out = g.out;
        s.track = g.track;
        delete_areas.append(s);
      }

      panel_timeline->delete_areas_and_relink(ca, delete_areas, false);

      // clean up, i.e. make everything not undeletable again
      for (const auto & g : panel_timeline->ghosts) {
        amber::ActiveSequence->clips.at(g.clip)->undeletable = false;

        if (g.transition != nullptr) {
          g.transition->parent_clip->undeletable = false;
          if (g.transition->secondary_clip != nullptr) {
            g.transition->secondary_clip->undeletable = false;
          }
        }
      }
    }

    // finally, perform actual movement of clips
    for (auto & g : panel_timeline->ghosts) {
      Clip* c = amber::ActiveSequence->clips.at(g.clip).get();

      if (g.transition == nullptr) {

        // if this was a clip rather than a transition

        c->move(ca, (g.in - g.old_in), (g.out - g.old_out), (g.clip_in - g.old_clip_in), (g.track - g.old_track), false, true);

      } else {

        // if the user was moving a transition

        bool is_opening_transition = (g.transition == c->opening_transition);
        long new_transition_length = g.out - g.in;
        if (g.transition->secondary_clip != nullptr) new_transition_length >>= 1;
        ca->append(
              new ModifyTransitionCommand(is_opening_transition ? c->opening_transition : c->closing_transition,
                                               new_transition_length)
              );

        long clip_length = c->length();

        if (g.transition->secondary_clip != nullptr) {

          // if this is a shared transition
          if (g.in != g.old_in && g.trim_type == TRIM_NONE) {
            long movement = g.in - g.old_in;

            // check if the transition is going to extend the out point (opening clip)
            long timeline_out_movement = 0;
            if (g.out > g.transition->parent_clip->timeline_out()) {
              timeline_out_movement = g.out - g.transition->parent_clip->timeline_out();
            }

            // check if the transition is going to extend the in point (closing clip)
            long timeline_in_movement = 0;
            if (g.in < g.transition->secondary_clip->timeline_in()) {
              timeline_in_movement = g.in - g.transition->secondary_clip->timeline_in();
            }

            g.transition->parent_clip->move(ca, movement, timeline_out_movement, movement, 0, false, true);
            g.transition->secondary_clip->move(ca, timeline_in_movement, movement, timeline_in_movement, 0, false, true);

            make_room_for_transition(ca, g.transition->parent_clip, kTransitionOpening, g.in, g.out, false);
            make_room_for_transition(ca, g.transition->secondary_clip, kTransitionClosing, g.in, g.out, false);

          }

        } else if (is_opening_transition) {

          if (g.in != g.old_in) {
            // if transition is going to make the clip bigger, make the clip bigger

            // check if the transition is going to extend the out point
            long timeline_out_movement = 0;
            if (g.out > g.transition->parent_clip->timeline_out()) {
              timeline_out_movement = g.out - g.transition->parent_clip->timeline_out();
            }

            c->move(ca, (g.in - g.old_in), timeline_out_movement, (g.clip_in - g.old_clip_in), 0, false, true);
            clip_length -= (g.in - g.old_in);
          }

          make_room_for_transition(ca, c, kTransitionOpening, g.in, g.out, false);

        } else {

          if (g.out != g.old_out) {

            // check if the transition is going to extend the in point
            long timeline_in_movement = 0;
            if (g.in < g.transition->parent_clip->timeline_in()) {
              timeline_in_movement = g.in - g.transition->parent_clip->timeline_in();
            }

            // if transition is going to make the clip bigger, make the clip bigger
            c->move(ca, timeline_in_movement, (g.out - g.old_out), timeline_in_movement, 0, false, true);
            clip_length += (g.out - g.old_out);
          }

          make_room_for_transition(ca, c, kTransitionClosing, g.in, g.out, false);

        }
      }
    }

    // time to verify the transitions of moved clips
    for (int i=0;i<panel_timeline->ghosts.size();i++) {
      const Ghost& g = panel_timeline->ghosts.at(i);

      // only applies to moving clips, transitions are verified above instead
      if (g.transition == nullptr) {
        ClipPtr c = amber::ActiveSequence->clips.at(g.clip);

        long new_clip_length = g.out - g.in;

        // using a for loop between constants to repeat the same steps for the opening and closing transitions
        for (int t=kTransitionOpening;t<=kTransitionClosing;t++) {

          TransitionPtr transition = (t == kTransitionOpening) ? c->opening_transition : c->closing_transition;

          // check the whether the clip has a transition here
          if (transition != nullptr) {

            // if the new clip size exceeds the opening transition's length, resize the transition
            if (new_clip_length < transition->get_true_length()) {
              ca->append(new ModifyTransitionCommand(transition, new_clip_length));
            }

            // check if the transition is a shared transition (it'll never have a secondary clip if it isn't)
            if (transition->secondary_clip != nullptr) {

              // check if the transition's "edge" is going to move
              if ((t == kTransitionOpening && g.in != g.old_in)
                  || (t == kTransitionClosing && g.out != g.old_out)) {

                // if we're here, this clip shares its opening transition as the closing transition of another
                // clip (or vice versa), and the in point is moving, so we may have to account for this

                // the other clip sharing this transition may be moving as well, meaning we don't have to do
                // anything

                bool split = true;

                // loop through ghosts to find out

                // for a shared transition, the secondary_clip will always be the closing transition side and
                // the parent_clip will always be the opening transition side
                Clip* search_clip = (t == kTransitionOpening)
                                              ? transition->secondary_clip : transition->parent_clip;

                for (int j=0;j<panel_timeline->ghosts.size();j++) {
                  const Ghost& other_clip_ghost = panel_timeline->ghosts.at(j);

                  if (amber::ActiveSequence->clips.at(other_clip_ghost.clip).get() == search_clip) {

                    // we found the other clip in the current ghosts/selections

                    // see if it's destination edge will be equal to this ghost's edge (in which case the
                    // transition doesn't need to change)
                    //
                    // also only do this if j is less than i, because it only needs to happen once and chances are
                    // the other clip already

                    bool edges_still_touch;
                    if (t == kTransitionOpening) {
                      edges_still_touch = (other_clip_ghost.out == g.in);
                    } else {
                      edges_still_touch = (other_clip_ghost.in == g.out);
                    }

                    if (edges_still_touch || j < i) {
                      split = false;
                    }

                    break;
                  }
                }

                if (split) {
                  // separate shared transition into one transition for each clip

                  if (t == kTransitionOpening) {

                    // set transition to single-clip mode
                    ca->append(new SetPointer(reinterpret_cast<void**>(&transition->secondary_clip),
                                              nullptr));

                    // create duplicate transition for other clip
                    ca->append(new AddTransitionCommand(nullptr,
                                                        transition->secondary_clip,
                                                        transition,
                                                        nullptr,
                                                        0));

                  } else {

                    // set transition to single-clip mode
                    ca->append(new SetPointer(reinterpret_cast<void**>(&transition->secondary_clip),
                                              nullptr));

                    // that transition will now attach to the other clip, so we duplicate it for this one

                    // create duplicate transition for this clip
                    ca->append(new AddTransitionCommand(nullptr,
                                                        transition->secondary_clip,
                                                        transition,
                                                        nullptr,
                                                        0));

                  }
                }

              }
            }
          }
        }
      }
    }
  }
  return true;
}

bool TimelineWidget::mouseReleaseTransition(ComboAction* ca) {
  const Ghost& g = panel_timeline->ghosts.at(0);

  // if the transition is greater than 0 length (if it is 0, we make nothing)
  if (g.in == g.out) return false;

  // get transition coordinates on the timeline
  long transition_start = qMin(g.in, g.out);
  long transition_end = qMax(g.in, g.out);

  // get clip references from tool's cached data
  Clip* open = (panel_timeline->transition_tool_open_clip > -1)
      ? amber::ActiveSequence->clips.at(panel_timeline->transition_tool_open_clip).get()
      : nullptr;

  Clip* close = (panel_timeline->transition_tool_close_clip > -1)
      ? amber::ActiveSequence->clips.at(panel_timeline->transition_tool_close_clip).get()
      : nullptr;

  // if it's shared, the transition length is halved (one half for each clip will result in the full length)
  long transition_length = transition_end - transition_start;
  if (open != nullptr && close != nullptr) {
    transition_length /= 2;
  }

  VerifyTransitionsAfterCreating(ca, open, close, transition_start, transition_end);

  // finally, add the transition to these clips
  ca->append(new AddTransitionCommand(open,
                                      close,
                                      nullptr,
                                      panel_timeline->transition_tool_meta,
                                      transition_length));

  return true;
}

bool TimelineWidget::mouseReleaseSplitting(ComboAction* ca, bool alt) {
  bool split = false;
  for (int i=0;i<panel_timeline->split_tracks.size();i++) {
    int split_index = getClipIndexFromCoords(panel_timeline->drag_frame_start, panel_timeline->split_tracks.at(i));
    if (split_index > -1 && panel_timeline->split_clip_and_relink(ca, split_index, panel_timeline->drag_frame_start, !alt)) {
      split = true;
    }
  }
  panel_timeline->split_cache.clear();
  return split;
}

void TimelineWidget::mouseReleaseEvent(QMouseEvent *event) {
  QToolTip::hideText();
  if (amber::ActiveSequence != nullptr) {
    bool alt = (event->modifiers() & Qt::AltModifier);
    bool shift = (event->modifiers() & Qt::ShiftModifier);
    bool ctrl = (event->modifiers() & Qt::ControlModifier);

    if (event->button() == Qt::LeftButton) {
      ComboAction* ca = new ComboAction();
      bool push_undo = false;

      if (panel_timeline->creating) {
        push_undo = mouseReleaseCreating(ca, shift);
        if (push_undo) ca->setText(tr("Create Clip"));
      } else if (panel_timeline->moving_proc) {
        push_undo = mouseReleaseMoving(ca, alt, ctrl);
        if (push_undo) ca->setText(tr("Move Clip(s)"));
      } else if (panel_timeline->selecting || panel_timeline->rect_select_proc) {
      } else if (panel_timeline->transition_tool_proc) {
        push_undo = mouseReleaseTransition(ca);
        if (push_undo) ca->setText(tr("Add Transition"));
      } else if (panel_timeline->splitting) {
        push_undo = mouseReleaseSplitting(ca, alt);
        if (push_undo) ca->setText(tr("Split Clip(s)"));
      }

      // remove duplicate selections
      panel_timeline->clean_up_selections(amber::ActiveSequence->selections);

      if (selection_command != nullptr) {
        selection_command->new_data = amber::ActiveSequence->selections;
        ca->append(selection_command);
        selection_command = nullptr;
        push_undo = true;
      }

      if (push_undo) {
        if (ca->text().isEmpty()) ca->setText(tr("Select"));
        amber::UndoStack.push(ca);
      } else {
        delete ca;
      }

      // destroy all ghosts
      panel_timeline->ghosts.clear();

      // clear split tracks
      panel_timeline->split_tracks.clear();

      panel_timeline->selecting = false;
      panel_timeline->moving_proc = false;
      panel_timeline->moving_init = false;
      panel_timeline->splitting = false;
      panel_timeline->snapped = false;
      panel_timeline->rect_select_init = false;
      panel_timeline->rect_select_proc = false;
      panel_timeline->transition_tool_init = false;
      panel_timeline->transition_tool_proc = false;
      pre_clips.clear();
      post_clips.clear();

      update_ui(true);
    }
    panel_timeline->hand_moving = false;
  }
}
void TimelineWidget::mouseMoveSelecting(bool alt) {
  // get number of selections based on tracks in selection area
  int selection_tool_count = 1 + qMax(panel_timeline->cursor_track, panel_timeline->drag_track_start) -
                             qMin(panel_timeline->cursor_track, panel_timeline->drag_track_start);

  // add count to selection offset for the total number of selection objects
  // (offset is usually 0, unless the user is holding shift in which case we add to existing selections)
  int selection_count = selection_tool_count + panel_timeline->selection_offset;

  // resize selection object array to new count
  if (amber::ActiveSequence->selections.size() != selection_count) {
    amber::ActiveSequence->selections.resize(selection_count);
  }

  // loop through tracks in selection area and adjust them accordingly
  int minimum_selection_track = qMin(panel_timeline->cursor_track, panel_timeline->drag_track_start);
  int maximum_selection_track = qMax(panel_timeline->cursor_track, panel_timeline->drag_track_start);
  long selection_in = qMin(panel_timeline->drag_frame_start, panel_timeline->cursor_frame);
  long selection_out = qMax(panel_timeline->drag_frame_start, panel_timeline->cursor_frame);
  for (int i = panel_timeline->selection_offset; i < selection_count; i++) {
    Selection& s = amber::ActiveSequence->selections[i];
    s.track = minimum_selection_track + i - panel_timeline->selection_offset;
    s.in = selection_in;
    s.out = selection_out;
  }

  // If the config is set to select links as well with the edit tool
  if (amber::CurrentConfig.edit_tool_selects_links) {

    // find which clips are selected
    for (int j = 0; j < amber::ActiveSequence->clips.size(); j++) {

      Clip* c = amber::ActiveSequence->clips.at(j).get();

      if (c != nullptr && c->IsSelected(false)) {

        // loop through linked clips
        for (int k : c->linked) {

          ClipPtr link = amber::ActiveSequence->clips.at(k);

          // see if one of the selections is already covering this track
          if (!(link->track() >= minimum_selection_track && link->track() <= maximum_selection_track)) {

            // clip is not in selection area, time to select it
            Selection link_sel;
            link_sel.in = selection_in;
            link_sel.out = selection_out;
            link_sel.track = link->track();
            amber::ActiveSequence->selections.append(link_sel);
          }
        }
      }
    }
  }

  // if the config is set to seek with the edit too, do so now
  if (amber::CurrentConfig.edit_tool_also_seeks) {
    panel_sequence_viewer->seek(qMin(panel_timeline->drag_frame_start, panel_timeline->cursor_frame));
  } else {
    // if not, repaint (seeking will trigger a repaint)
    panel_timeline->repaint_timeline();
  }
}

void TimelineWidget::mouseMoveHandMoving(QMouseEvent* event) {
  if (!event) {
    qWarning() << "mouseMoveHandMoving: event is null";
    return;
  }
  // if we're hand moving, we'll be adding values directly to the scrollbars

  // the scrollbars trigger repaints when they scroll, which is unnecessary here so we block them
  panel_timeline->block_repaints = true;
  panel_timeline->horizontalScrollBar->setValue(panel_timeline->horizontalScrollBar->value() +
                                                panel_timeline->drag_x_start - event->position().toPoint().x());
  scrollBar->setValue(scrollBar->value() + panel_timeline->drag_y_start - event->position().toPoint().y());
  panel_timeline->block_repaints = false;

  // finally repaint
  panel_timeline->repaint_timeline();

  // store current cursor position for next hand move event
  panel_timeline->drag_x_start = event->position().toPoint().x();
  panel_timeline->drag_y_start = event->position().toPoint().y();
}

void TimelineWidget::mouseMoveMovingInit(QMouseEvent* event) {
  if (!event) {
    qWarning() << "mouseMoveMovingInit: event is null";
    return;
  }
  if (track_resizing) {

    // get cursor movement
    int diff = (event->position().toPoint().y() - panel_timeline->drag_y_start);

    // add it to the current track height
    int new_height = panel_timeline->GetTrackHeight(track_target);
    if (track_target < 0) {
      new_height -= diff;
    } else {
      new_height += diff;
    }

    // limit track height to track minimum height constant
    new_height = qMax(new_height, amber::timeline::kTrackMinHeight);

    // set the track height
    panel_timeline->SetTrackHeight(track_target, new_height);

    // store current cursor position for next track resize event
    panel_timeline->drag_y_start = event->position().toPoint().y();

    update();
  } else if (panel_timeline->moving_proc) {

    // we're currently dragging ghosts
    update_ghosts(event->position().toPoint(), event->modifiers() & Qt::ShiftModifier);

  } else {

    // Prepare to start moving clips in some capacity. We create Ghost objects to store movement data before we
    // actually apply it to the clips (in mouseReleaseEvent)

    // loop through clips for any currently selected
    for (int i = 0; i < amber::ActiveSequence->clips.size(); i++) {

      Clip* c = amber::ActiveSequence->clips.at(i).get();

      if (c != nullptr) {
        Ghost g;
        g.transition = nullptr;

        // check if whole clip is added
        bool add = false;

        // check if a transition is selected (prioritize transition selection)
        // (only the pointer tool supports moving transitions)
        if (panel_timeline->tool == TIMELINE_TOOL_POINTER &&
            (c->opening_transition != nullptr || c->closing_transition != nullptr)) {

          // check if any selections contain a whole transition
          for (const auto& s : amber::ActiveSequence->selections) {

            if (s.track == c->track()) {
              if (selection_contains_transition(s, c, kTransitionOpening)) {

                g.transition = c->opening_transition;
                add = true;
                break;

              } else if (selection_contains_transition(s, c, kTransitionClosing)) {

                g.transition = c->closing_transition;
                add = true;
                break;
              }
            }
          }
        }

        // if a transition isn't selected, check if the whole clip is
        if (!add) {
          add = c->IsSelected();
        }

        if (add) {

          if (g.transition != nullptr) {

            // transition may be a dual transition, check if it's already been added elsewhere
            for (const auto& ghost : panel_timeline->ghosts) {
              if (ghost.transition == g.transition) {
                add = false;
                break;
              }
            }
          }

          if (add) {
            g.clip = i;
            g.trim_type = panel_timeline->trim_type;
            panel_timeline->ghosts.append(g);
          }
        }
      }
    }

    if (panel_timeline->tool == TIMELINE_TOOL_SLIDE) {

      // for the slide tool, we add the surrounding clips as ghosts that are getting trimmed the opposite way

      // store original array size since we'll be adding to it
      int ghost_arr_size = panel_timeline->ghosts.size();

      // loop through clips for any that are "touching" the selected clips
      for (int j = 0; j < amber::ActiveSequence->clips.size(); j++) {

        ClipPtr c = amber::ActiveSequence->clips.at(j);
        if (c != nullptr) {

          for (int i = 0; i < ghost_arr_size; i++) {

            Ghost& g = panel_timeline->ghosts[i];
            g.trim_type = TRIM_NONE;  // the selected clips will be moving, not trimming

            ClipPtr ghost_clip = amber::ActiveSequence->clips.at(g.clip);

            if (c->track() == ghost_clip->track()) {

              // see if this clip is currently selected, if so we won't add it as a "touching" clip
              bool found = false;
              for (int k = 0; k < ghost_arr_size; k++) {
                if (panel_timeline->ghosts.at(k).clip == j) {
                  found = true;
                  break;
                }
              }

              if (!found) {  // the clip is not currently selected

                // check if this clip is indeed touching
                bool is_in = (c->timeline_in() == ghost_clip->timeline_out());
                if (is_in || c->timeline_out() == ghost_clip->timeline_in()) {
                  Ghost gh;
                  gh.transition = nullptr;
                  gh.clip = j;
                  gh.trim_type = is_in ? TRIM_IN : TRIM_OUT;
                  panel_timeline->ghosts.append(gh);
                }
              }
            }
          }
        }
      }
    }

    // set up ghost defaults
    init_ghosts();

    // if the ripple tool is selected, prepare to ripple
    if (panel_timeline->tool == TIMELINE_TOOL_RIPPLE) {

      long axis = LONG_MAX;

      // find the earliest point within the selected clips which is the point we'll ripple around
      // also store the currently selected clips so we don't have to do it later
      QVector<ClipPtr> ghost_clips;
      ghost_clips.resize(panel_timeline->ghosts.size());

      for (int i = 0; i < panel_timeline->ghosts.size(); i++) {
        ClipPtr c = amber::ActiveSequence->clips.at(panel_timeline->ghosts.at(i).clip);
        if (panel_timeline->trim_type == TRIM_IN) {
          axis = qMin(axis, c->timeline_in());
        } else {
          axis = qMin(axis, c->timeline_out());
        }

        // store clip reference
        ghost_clips[i] = c;
      }

      // loop through clips and cache which are earlier than the axis and which after after
      for (auto c : amber::ActiveSequence->clips) {
        if (c != nullptr && !ghost_clips.contains(c)) {
          bool clip_is_post = (c->timeline_in() >= axis);

          // construct the list of pre and post clips
          QVector<ClipPtr>& clip_list = (clip_is_post) ? post_clips : pre_clips;

          // check if there's already a clip in this list on this track, and if this clip is closer or not
          bool found = false;
          for (auto& j : clip_list) {

            ClipPtr compare = j;

            if (compare->track() == c->track()) {

              // if the clip is closer, use this one instead of the current one in the list
              if ((!clip_is_post && compare->timeline_out() < c->timeline_out()) ||
                  (clip_is_post && compare->timeline_in() > c->timeline_in())) {
                j = c;
              }

              found = true;
              break;
            }
          }

          // if there is no clip on this track in the list, add it
          if (!found) {
            clip_list.append(c);
          }
        }
      }
    }

    // store selections
    selection_command = new SetSelectionsCommand(amber::ActiveSequence.get());
    selection_command->old_data = amber::ActiveSequence->selections;

    // ready to start moving clips
    panel_timeline->moving_proc = true;
  }

  update_ui(false);
}

void TimelineWidget::mouseMoveSplitting(bool alt) {
  // get the range of tracks currently dragged
  int track_start = qMin(panel_timeline->cursor_track, panel_timeline->drag_track_start);
  int track_end = qMax(panel_timeline->cursor_track, panel_timeline->drag_track_start);
  int track_size = 1 + track_end - track_start;

  // set tracks to be split
  panel_timeline->split_tracks.resize(track_size);
  for (int i = 0; i < track_size; i++) {
    panel_timeline->split_tracks[i] = track_start + i;
  }

  // if alt isn't being held, also add the tracks of the clip's links
  if (!alt) {
    for (int i = 0; i < track_size; i++) {

      // make sure there's a clip in this track
      int clip_index = getClipIndexFromCoords(panel_timeline->drag_frame_start, panel_timeline->split_tracks[i]);

      if (clip_index > -1) {
        ClipPtr clip = amber::ActiveSequence->clips.at(clip_index);
        for (int j = 0; j < clip->linked.size(); j++) {

          ClipPtr link = amber::ActiveSequence->clips.at(clip->linked.at(j));

          // if this clip isn't already in the list of tracks to split
          if (link->track() < track_start || link->track() > track_end) {
            panel_timeline->split_tracks.append(link->track());
          }
        }
      }
    }
  }

  update_ui(false);
}

void TimelineWidget::mouseMoveRectSelect(QMouseEvent* event, bool alt) {
  if (!event) {
    qWarning() << "mouseMoveRectSelect: event is null";
    return;
  }
  // set if the user started dragging at point where there was no clip

  if (panel_timeline->rect_select_proc) {

    // we're currently rectangle selecting

    // set the right/bottom coords to the current mouse position
    // (left/top were set to the starting drag position earlier)
    panel_timeline->rect_select_rect.setRight(event->position().toPoint().x());

    panel_timeline->rect_select_rect.setBottom(event->position().toPoint().y());

    long frame_min = qMin(panel_timeline->drag_frame_start, panel_timeline->cursor_frame);
    long frame_max = qMax(panel_timeline->drag_frame_start, panel_timeline->cursor_frame);

    int track_min = qMin(panel_timeline->drag_track_start, panel_timeline->cursor_track);
    int track_max = qMax(panel_timeline->drag_track_start, panel_timeline->cursor_track);

    // determine which clips are in this rectangular selection
    QVector<ClipPtr> selected_clips;
    for (int i = 0; i < amber::ActiveSequence->clips.size(); i++) {
      ClipPtr clip = amber::ActiveSequence->clips.at(i);
      if (clip != nullptr && clip->track() >= track_min && clip->track() <= track_max &&
          !(clip->timeline_in() < frame_min && clip->timeline_out() < frame_min) &&
          !(clip->timeline_in() > frame_max && clip->timeline_out() > frame_max)) {

        // create a group of the clip (and its links if alt is not pressed)
        QVector<ClipPtr> session_clips;
        session_clips.append(clip);

        if (!alt) {
          for (int j : clip->linked) {
            session_clips.append(amber::ActiveSequence->clips.at(j));
          }
        }

        // for each of these clips, see if clip has already been added -
        // this can easily happen due to adding linked clips
        for (auto c : session_clips) {
          bool found = false;

          for (const auto& selected_clip : selected_clips) {
            if (selected_clip == c) {
              found = true;
              break;
            }
          }

          // if the clip isn't already in the selection add it
          if (!found) {
            selected_clips.append(c);
          }
        }
      }
    }

    // add each of the selected clips to the main sequence's selections
    amber::ActiveSequence->selections.resize(selected_clips.size() + panel_timeline->selection_offset);
    for (int i = 0; i < selected_clips.size(); i++) {
      Selection& s = amber::ActiveSequence->selections[i + panel_timeline->selection_offset];
      ClipPtr clip = selected_clips.at(i);
      s.old_in = s.in = clip->timeline_in();
      s.old_out = s.out = clip->timeline_out();
      s.old_track = s.track = clip->track();
    }

    panel_timeline->repaint_timeline();
  } else {

    // set up rectangle selecting
    panel_timeline->rect_select_rect.setX(event->position().toPoint().x());

    panel_timeline->rect_select_rect.setY(event->position().toPoint().y());

    panel_timeline->rect_select_rect.setWidth(0);
    panel_timeline->rect_select_rect.setHeight(0);

    panel_timeline->rect_select_proc = true;
  }
}

void TimelineWidget::mouseMoveHoverTrimDetection(QMouseEvent* event) {
  if (!event) {
    qWarning() << "mouseMoveHoverTrimDetection: event is null";
    return;
  }
  // hide any tooltip that may be currently showing
  QToolTip::hideText();

  // cache cursor position
  QPoint pos = event->position().toPoint();

  //
  // check to see if the cursor is on a clip edge
  //

  // threshold around a trim point that the cursor can be within and still considered "trimming"
  int lim = 5;
  int mouse_frame_lower = pos.x() - lim;
  int mouse_frame_upper = pos.x() + lim;

  // used to determine whether we the cursor found a trim point or not
  bool found = false;

  // used to determine whether the cursor is within the rect of a clip
  bool cursor_contains_clip = false;

  // used to determine how close the cursor is to a trim point
  // (and more specifically, whether another point is closer or not)
  int closeness = INT_MAX;

  // while we loop through the clips, we cache the maximum/minimum tracks in this sequence
  int min_track = INT_MAX;
  int max_track = INT_MIN;

  // we default to selecting no transition, but set this accordingly if the cursor is on a transition
  panel_timeline->transition_select = kTransitionNone;

  // we also default to no trimming which may be changed later in this function
  panel_timeline->trim_type = TRIM_NONE;

  // set currently trimming clip to -1 (aka null)
  panel_timeline->trim_target = -1;

  // loop through current clips in the sequence
  for (int i = 0; i < amber::ActiveSequence->clips.size(); i++) {
    ClipPtr c = amber::ActiveSequence->clips.at(i);
    if (c != nullptr) {

      // cache track range
      min_track = qMin(min_track, c->track());
      max_track = qMax(max_track, c->track());

      // if this clip is on the same track the mouse is
      if (c->track() == panel_timeline->cursor_track) {

        // if this cursor is inside the boundaries of this clip (hovering over the clip)
        if (panel_timeline->cursor_frame >= c->timeline_in() && panel_timeline->cursor_frame <= c->timeline_out()) {

          // acknowledge that we are hovering over a clip
          cursor_contains_clip = true;

          // start a timer to show a tooltip about this clip
          tooltip_timer.start();
          tooltip_clip = i;

          // check if the cursor is specifically hovering over one of the clip's transitions
          if (c->opening_transition != nullptr &&
              panel_timeline->cursor_frame <= c->timeline_in() + c->opening_transition->get_true_length()) {

            panel_timeline->transition_select = kTransitionOpening;

          } else if (c->closing_transition != nullptr &&
                     panel_timeline->cursor_frame >=
                         c->timeline_out() - c->closing_transition->get_true_length()) {

            panel_timeline->transition_select = kTransitionClosing;
          }
        }

        int visual_in_point = panel_timeline->getTimelineScreenPointFromFrame(c->timeline_in());
        int visual_out_point = panel_timeline->getTimelineScreenPointFromFrame(c->timeline_out());

        // is the cursor hovering around the clip's IN point?
        if (visual_in_point > mouse_frame_lower && visual_in_point < mouse_frame_upper) {

          // test how close this IN point is to the cursor
          int nc = qAbs(visual_in_point + 1 - pos.x());

          // and test whether it's closer than the last in/out point we found
          if (nc < closeness) {

            // if so, this is the point we'll make active for now (unless we find a closer one later)
            panel_timeline->trim_target = i;
            panel_timeline->trim_type = TRIM_IN;
            closeness = nc;
            found = true;
          }
        }

        // is the cursor hovering around the clip's OUT point?
        if (visual_out_point > mouse_frame_lower && visual_out_point < mouse_frame_upper) {

          // test how close this OUT point is to the cursor
          int nc = qAbs(visual_out_point - 1 - pos.x());

          // and test whether it's closer than the last in/out point we found
          if (nc < closeness) {

            // if so, this is the point we'll make active for now (unless we find a closer one later)
            panel_timeline->trim_target = i;
            panel_timeline->trim_type = TRIM_OUT;
            closeness = nc;
            found = true;
          }
        }

        // the pointer can be used to resize/trim transitions, here we test if the
        // cursor is within the trim point of one of the clip's transitions
        if (panel_timeline->tool == TIMELINE_TOOL_POINTER) {

          // if the clip has an opening transition
          if (c->opening_transition != nullptr) {

            // cache the timeline frame where the transition ends
            int transition_point = panel_timeline->getTimelineScreenPointFromFrame(
                c->timeline_in() + c->opening_transition->get_true_length());

            // check if the cursor is hovering around it (within the threshold)
            if (transition_point > mouse_frame_lower && transition_point < mouse_frame_upper) {

              // similar to above, test how close it is and if it's closer, make this active
              int nc = qAbs(transition_point - 1 - pos.x());
              if (nc < closeness) {
                panel_timeline->trim_target = i;
                panel_timeline->trim_type = TRIM_OUT;
                panel_timeline->transition_select = kTransitionOpening;
                closeness = nc;
                found = true;
              }
            }
          }

          // if the clip has a closing transition
          if (c->closing_transition != nullptr) {

            // cache the timeline frame where the transition starts
            int transition_point = panel_timeline->getTimelineScreenPointFromFrame(
                c->timeline_out() - c->closing_transition->get_true_length());

            // check if the cursor is hovering around it (within the threshold)
            if (transition_point > mouse_frame_lower && transition_point < mouse_frame_upper) {

              // similar to above, test how close it is and if it's closer, make this active
              int nc = qAbs(transition_point + 1 - pos.x());
              if (nc < closeness) {
                panel_timeline->trim_target = i;
                panel_timeline->trim_type = TRIM_IN;
                panel_timeline->transition_select = kTransitionClosing;
                closeness = nc;
                found = true;
              }
            }
          }
        }
      }
    }
  }

  // if the cursor is indeed on a clip edge, we set the cursor accordingly
  if (found) {

    if (panel_timeline->trim_type == TRIM_IN) {  // if we're trimming an IN point
      setCursor(panel_timeline->tool == TIMELINE_TOOL_RIPPLE ? amber::cursor::LeftRipple : amber::cursor::LeftTrim);
    } else {  // if we're trimming an OUT point
      setCursor(panel_timeline->tool == TIMELINE_TOOL_RIPPLE ? amber::cursor::RightRipple : amber::cursor::RightTrim);
    }

  } else {
    // we didn't find a trim target, so we must be doing something else
    // (e.g. dragging a clip or resizing the track heights)

    unsetCursor();

    // check to see if we're resizing a track height
    int test_range = 5;
    int mouse_pos = event->position().toPoint().y();
    int hover_track = getTrackFromScreenPoint(mouse_pos);
    int track_y_edge = getScreenPointFromTrack(hover_track);

    if (hover_track >= 0) {
      track_y_edge += panel_timeline->GetTrackHeight(hover_track);
    }

    if (mouse_pos > track_y_edge - test_range && mouse_pos < track_y_edge + test_range) {
      if (cursor_contains_clip || (amber::CurrentConfig.show_track_lines &&
                                   panel_timeline->cursor_track >= min_track &&
                                   panel_timeline->cursor_track <= max_track)) {
        track_resizing = true;
        track_target = hover_track;
        setCursor(Qt::SizeVerCursor);
      }
    }
  }
}

void TimelineWidget::mouseMoveHoverTransition(QMouseEvent* event) {
  if (!event) {
    qWarning() << "mouseMoveHoverTransition: event is null";
    return;
  }
  if (panel_timeline->transition_tool_init) {

    // the transition tool has started

    if (panel_timeline->transition_tool_proc) {

      // ghosts have been set up, so just run update
      update_ghosts(event->position().toPoint(), event->modifiers() & Qt::ShiftModifier);

    } else {

      // transition tool is being used but ghosts haven't been set up yet, set them up now
      int primary_type = kTransitionOpening;
      int primary = panel_timeline->transition_tool_open_clip;
      if (primary == -1) {
        primary_type = kTransitionClosing;
        primary = panel_timeline->transition_tool_close_clip;
      }

      ClipPtr c = amber::ActiveSequence->clips.at(primary);

      Ghost g;

      g.in = g.old_in = g.out = g.old_out =
          (primary_type == kTransitionOpening) ? c->timeline_in() : c->timeline_out();

      g.track = c->track();
      g.clip = primary;
      g.media_stream = primary_type;
      g.trim_type = TRIM_NONE;

      panel_timeline->ghosts.append(g);

      panel_timeline->transition_tool_proc = true;
    }

  } else {

    // transition tool has been selected but is not yet active, so we show screen feedback to the user on
    // possible transitions

    int mouse_clip = getClipIndexFromCoords(panel_timeline->cursor_frame, panel_timeline->cursor_track);

    // set default transition tool references to no clip
    panel_timeline->transition_tool_open_clip = -1;
    panel_timeline->transition_tool_close_clip = -1;

    if (mouse_clip > -1) {

      // cursor is hovering over a clip

      ClipPtr c = amber::ActiveSequence->clips.at(mouse_clip);

      // check if the clip and transition are both the same sign (meaning video/audio are the same)
      if (same_sign(c->track(), panel_timeline->transition_tool_side)) {

        // the range within which the transition tool will assume the user wants to make a shared transition
        // between two clips rather than just one transition on one clip
        long between_range = getFrameFromScreenPoint(panel_timeline->zoom, TRANSITION_BETWEEN_RANGE) + 1;

        // set whether the transition is opening or closing based on whether the cursor is on the left half
        // or right half of the clip
        if (panel_timeline->cursor_frame > (c->timeline_in() + (c->length() / 2))) {
          panel_timeline->transition_tool_close_clip = mouse_clip;

          // if the cursor is within this range, set the post_clip to be the next clip touching
          //
          // getClipIndexFromCoords() will automatically set to -1 if there's no clip there which means the
          // end result will be the same as not setting a clip here at all
          if (panel_timeline->cursor_frame > c->timeline_out() - between_range) {
            panel_timeline->transition_tool_open_clip = getClipIndexFromCoords(c->timeline_out() + 1, c->track());
          }
        } else {
          panel_timeline->transition_tool_open_clip = mouse_clip;

          if (panel_timeline->cursor_frame < c->timeline_in() + between_range) {
            panel_timeline->transition_tool_close_clip = getClipIndexFromCoords(c->timeline_in() - 1, c->track());
          }
        }
      }
    }
  }

  panel_timeline->repaint_timeline();
}

void TimelineWidget::mouseMoveEvent(QMouseEvent* event) {
  // interrupt any potential tooltip about to show
  tooltip_timer.stop();

  if (amber::ActiveSequence != nullptr) {
    bool alt = (event->modifiers() & Qt::AltModifier);

    // store current frame/track corresponding to the cursor
    panel_timeline->cursor_frame =
        panel_timeline->getTimelineFrameFromScreenPoint(event->position().toPoint().x());
    panel_timeline->cursor_track = getTrackFromScreenPoint(event->position().toPoint().y());

    // if holding the mouse button down, let's scroll to that location
    if (event->buttons() != 0 && panel_timeline->tool != TIMELINE_TOOL_HAND) {
      panel_timeline->scroll_to_frame(panel_timeline->cursor_frame);
    }

    // determine if the action should be "inserting" rather than "overwriting"
    // Default behavior is to replace/overwrite clips under any clips we're dropping over them. Inserting will
    // split and move existing clips at the drop point to make space for the drop
    panel_timeline->move_insert = ((event->modifiers() & Qt::ControlModifier) &&
                                   (panel_timeline->tool == TIMELINE_TOOL_POINTER || panel_timeline->tool == TIMELINE_TOOL_TRACK_SELECT || panel_timeline->importing ||
                                    panel_timeline->creating));

    // if we're not currently resizing already, default track resizing to false (we'll set it to true later if
    // the user is still hovering over a track line)
    if (!panel_timeline->moving_init) {
      track_resizing = false;
    }

    // if the current tool uses an on-screen visible cursor, we snap the cursor to the timeline
    if (current_tool_shows_cursor()) {
      panel_timeline->snap_to_timeline(
          &panel_timeline->cursor_frame,

          // only snap to the playhead if the edit tool doesn't force the playhead to
          // follow it (or if we're not selecting since that means the playhead is
          // static at the moment)
          !amber::CurrentConfig.edit_tool_also_seeks || !panel_timeline->selecting,

          true, true);
    }

    if (panel_timeline->selecting) {
      mouseMoveSelecting(alt);
    } else if (panel_timeline->hand_moving) {
      mouseMoveHandMoving(event);
    } else if (panel_timeline->moving_init) {
      mouseMoveMovingInit(event);
    } else if (panel_timeline->splitting) {
      mouseMoveSplitting(alt);
    } else if (panel_timeline->rect_select_init) {
      mouseMoveRectSelect(event, alt);
    } else if (current_tool_shows_cursor()) {
      // we're not currently performing an action (click is not pressed), but redraw because we have an
      // on-screen cursor
      panel_timeline->repaint_timeline();
    } else if (panel_timeline->tool == TIMELINE_TOOL_POINTER ||
               panel_timeline->tool == TIMELINE_TOOL_RIPPLE ||
               panel_timeline->tool == TIMELINE_TOOL_ROLLING) {
      mouseMoveHoverTrimDetection(event);
    } else if (panel_timeline->tool == TIMELINE_TOOL_SLIP) {
      // we're not currently performing any slipping, all we do here is set the cursor if mouse is hovering
      // over a clip
      if (getClipIndexFromCoords(panel_timeline->cursor_frame, panel_timeline->cursor_track) > -1) {
        setCursor(amber::cursor::Slip);
      } else {
        unsetCursor();
      }
    } else if (panel_timeline->tool == TIMELINE_TOOL_TRANSITION) {
      mouseMoveHoverTransition(event);
    }
  }
}

void TimelineWidget::leaveEvent(QEvent*) {
  tooltip_timer.stop();
}
void TimelineWidget::resizeEvent(QResizeEvent *) {
  scrollBar->setPageStep(height());
}

bool TimelineWidget::is_track_visible(int /*track*/) {
  return true;
}

// **************************************
// screen point <-> frame/track functions
// **************************************

int TimelineWidget::getTrackFromScreenPoint(int y) {
  int track_candidate = 0;

  y += scroll;

  y -= panel_timeline->SeamY();

  if (y < 0) {
    track_candidate--;
  }

  int compounded_heights = 0;

  while (true) {
    int track_height = panel_timeline->GetTrackHeight(track_candidate);
    if (amber::CurrentConfig.show_track_lines) track_height++;
    if (y < 0) {
      track_height = -track_height;
    }

    int next_compounded_height = compounded_heights + track_height;


    if (y >= qMin(next_compounded_height, compounded_heights) && y < qMax(next_compounded_height, compounded_heights)) {
      return track_candidate;
    }

    compounded_heights = next_compounded_height;

    if (y < 0) {
      track_candidate--;
    } else {
      track_candidate++;
    }
  }
}

int TimelineWidget::getScreenPointFromTrack(int track) {
  int point = 0;

  int start = (track < 0) ? -1 : 0;
  int interval = (track < 0) ? -1 : 1;

  if (track < 0) track--;

  for (int i=start;i!=track;i+=interval) {
    point += panel_timeline->GetTrackHeight(i);
    if (amber::CurrentConfig.show_track_lines) point++;
  }

  const int seam = panel_timeline->SeamY();
  return (track < 0) ? seam - point - scroll : seam + point - scroll;
}

int TimelineWidget::getClipIndexFromCoords(long frame, int track) {
  for (int i=0;i<amber::ActiveSequence->clips.size();i++) {
    ClipPtr c = amber::ActiveSequence->clips.at(i);
    if (c != nullptr && c->track() == track && frame >= c->timeline_in() && frame < c->timeline_out()) {
      return i;
    }
  }
  return -1;
}

void TimelineWidget::setScroll(int s) {
  scroll = s;
  update();
}
