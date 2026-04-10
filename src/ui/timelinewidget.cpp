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

#include <QColor>
#include <QInputDialog>
#include <QMessageBox>
#include <QMimeData>
#include <QMouseEvent>
#include <QObject>
#include <QPainter>
#include <QPoint>
#include <QScrollBar>
#include <QStatusBar>
#include <QToolTip>
#include <QVariant>
#include <QtMath>

#include "dialogs/clippropertiesdialog.h"
#include "dialogs/newsequencedialog.h"
#include "effects/effect.h"
#include "effects/internal/solideffect.h"
#include "engine/undo/undo.h"
#include "engine/undo/undostack.h"
#include "global/config.h"
#include "global/debug.h"
#include "global/global.h"
#include "mainwindow.h"
#include "panels/panels.h"
#include "project/projectelements.h"
#include "rendering/audio.h"
#include "rendering/renderfunctions.h"
#include "ui/cursors.h"
#include "ui/focusfilter.h"
#include "ui/menu.h"
#include "ui/menuhelper.h"
#include "ui/rectangleselect.h"
#include "ui/resizablescrollbar.h"
#include "ui/sourceiconview.h"
#include "ui/sourcetable.h"
#include "ui/viewerwidget.h"

#define MAX_TEXT_WIDTH 20
#define TRANSITION_BETWEEN_RANGE 40

TimelineWidget::TimelineWidget(QWidget* parent) : QWidget(parent) {
  selection_command = nullptr;
  self_created_sequence = nullptr;
  scroll = 0;

  bottom_align = false;
  track_resizing = false;
  setMouseTracking(true);

  setAcceptDrops(true);

  setContextMenuPolicy(Qt::CustomContextMenu);
  connect(this, &TimelineWidget::customContextMenuRequested, this, &TimelineWidget::show_context_menu);

  tooltip_timer.setInterval(500);
  connect(&tooltip_timer, &QTimer::timeout, this, &TimelineWidget::tooltip_timer_timeout);
}

// Defined in timelinewidget_ghosts.cpp
void make_room_for_transition(ComboAction* ca, Clip* c, int type, long transition_start, long transition_end,
                              bool delete_old_transitions, long timeline_in = -1, long timeline_out = -1);
void VerifyTransitionsAfterCreating(ComboAction* ca, Clip* open, Clip* close, long transition_start,
                                    long transition_end);
void validate_transitions(Clip* c, int transition_type, long& frame_diff);

bool same_sign(int a, int b) { return (a < 0) == (b < 0); }

void TimelineWidget::dragEnterEvent(QDragEnterEvent* event) {
  bool import_init = false;

  QVector<amber::timeline::MediaImportData> media_list;
  panel_timeline->importing_files = false;

  if (panel_project->IsProjectWidget(event->source())) {
    QModelIndexList items = panel_project->get_current_selected();
    media_list.resize(items.size());
    for (int i = 0; i < items.size(); i++) {
      media_list[i] = panel_project->item_to_media(items.at(i));
    }
    import_init = true;
  }

  if (event->source() == panel_footage_viewer) {
    if (panel_footage_viewer->seq != amber::ActiveSequence) {  // don't allow nesting the same sequence

      media_list.append(amber::timeline::MediaImportData(
          panel_footage_viewer->media,
          static_cast<amber::timeline::MediaImportType>(event->mimeData()->text().toInt())));
      import_init = true;
    }
  }

  if (amber::CurrentConfig.enable_drag_files_to_timeline && event->mimeData()->hasUrls()) {
    QList<QUrl> urls = event->mimeData()->urls();
    if (!urls.isEmpty()) {
      QStringList file_list;

      for (const auto& url : urls) {
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
      panel_timeline->drag_track_start = (bottom_align) ? -1 : 0;
    }

    panel_timeline->create_ghosts_from_media(seq, entry_point, media_list);

    panel_timeline->importing = true;
  }
}

void TimelineWidget::dragMoveEvent(QDragMoveEvent* event) {
  if (panel_timeline->importing) {
    event->acceptProposedAction();

    if (amber::ActiveSequence != nullptr) {
      QPoint pos = event->position().toPoint();
      panel_timeline->scroll_to_frame(panel_timeline->getTimelineFrameFromScreenPoint(event->position().toPoint().x()));
      update_ghosts(pos, event->modifiers() & Qt::ShiftModifier);
      panel_timeline->move_insert = ((event->modifiers() & Qt::ControlModifier) &&
                                     (panel_timeline->tool == TIMELINE_TOOL_POINTER || panel_timeline->importing));
      update_ui(false);
    }
  }
}

void TimelineWidget::wheelEvent(QWheelEvent* event) {
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
  bool is_trackpad = (event->device() && event->device()->type() == QInputDevice::DeviceType::TouchPad) ||
                     !event->pixelDelta().isNull();
  bool cfg_swap = is_trackpad ? false : amber::CurrentConfig.invert_timeline_scroll_axes;

  // Allow shift for axis swap, but don't swap on zoom... Unless
  // we need to override Qt's axis swap via Alt
  bool swap_hv = ((shift != cfg_swap) & !zooming) | (alt & !shift & zooming);

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
  for (const auto& g : panel_timeline->ghosts) {
    Selection sel;
    sel.in = g.in;
    sel.out = g.out;
    sel.track = g.track;
    delete_areas.append(sel);
  }
  panel_timeline->delete_areas_and_relink(ca, delete_areas, false);
}

// Helper: check whether a stationary clip overlaps the old ghost range (used to decide gap closure)
static bool clip_overlaps_old_range(ClipPtr c, long earliest_old_point, long latest_old_point) {
  bool entirely_before = c->timeline_in() < earliest_old_point && c->timeline_out() <= earliest_old_point;
  bool entirely_after = c->timeline_in() >= latest_old_point && c->timeline_out() > latest_old_point;
  return !entirely_before && !entirely_after;
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
  for (const auto& g : panel_timeline->ghosts) {
    earliest_old_point = qMin(earliest_old_point, g.old_in);
    latest_old_point = qMax(latest_old_point, g.old_out);
    earliest_new_point = qMin(earliest_new_point, g.in);
    latest_new_point = qMax(latest_new_point, g.out);
    if (g.clip >= 0) {
      ignore_clips.append(g.clip);
    } else {
      ripple_old_point = false;  // don't close old gap if importing
    }
  }

  panel_timeline->split_cache.clear();

  for (int i = 0; i < amber::ActiveSequence->clips.size(); i++) {
    ClipPtr c = amber::ActiveSequence->clips.at(i);
    if (c == nullptr) continue;

    bool is_moving = false;
    for (const auto& ghost : panel_timeline->ghosts) {
      if (ghost.clip == i) {
        is_moving = true;
        break;
      }
    }
    if (is_moving) continue;

    if (c->timeline_in() < earliest_new_point && c->timeline_out() > earliest_new_point) {
      panel_timeline->split_clip_and_relink(ca, i, earliest_new_point, true);
    }

    if (ripple_old_point && clip_overlaps_old_range(c, earliest_old_point, latest_old_point) &&
        !ignore_clips.contains(i)) {
      ripple_old_point = false;
    }
  }

  long ripple_length = latest_new_point - earliest_new_point;
  ripple_clips(ca, amber::ActiveSequence.get(), earliest_new_point, ripple_length, ignore_clips);

  if (ripple_old_point) {
    long second_ripple_length = earliest_old_point - latest_old_point;
    ripple_clips(ca, amber::ActiveSequence.get(), latest_old_point, second_ripple_length, ignore_clips);
    if (earliest_old_point < earliest_new_point) {
      for (auto& g : panel_timeline->ghosts) {
        g.in += second_ripple_length;
        g.out += second_ripple_length;
      }
      for (auto& s : amber::ActiveSequence->selections) {
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
      mbox.setText(
          tr("No sequence has been created yet. Would you like to make one based on this footage or set "
             "custom parameters?"));
      mbox.addButton(tr("Use Footage Parameters"), QMessageBox::YesRole);
      QAbstractButton* custom_param_btn = mbox.addButton(tr("Custom Parameters"), QMessageBox::NoRole);
      QAbstractButton* cancel_btn = mbox.addButton(QMessageBox::Cancel);

      mbox.exec();

      s = self_created_sequence.get();
      double old_fr = s->frame_rate;

      if (mbox.clickedButton() == cancel_btn || (mbox.clickedButton() == custom_param_btn &&
                                                 NewSequenceDialog(this, nullptr, s).exec() == QDialog::Rejected)) {
        delete ca;
        self_created_sequence = nullptr;
        event->ignore();
        return;
      }

      if (mbox.clickedButton() == custom_param_btn && !qFuzzyCompare(old_fr, s->frame_rate)) {
        // If we're here, the user changed the frame rate so all the ghosts will need adjustment
        for (auto& g : panel_timeline->ghosts) {
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

void TimelineWidget::mouseDoubleClickEvent(QMouseEvent* event) {
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
  return (panel_timeline->tool == TIMELINE_TOOL_EDIT || panel_timeline->tool == TIMELINE_TOOL_RAZOR ||
          panel_timeline->creating);
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

// Helper: handle pointer click on an already-selected clip
static void mousePressPointerSelectedClip(Clip* clip, bool shift, bool alt) {
  if (shift) {
    panel_timeline->deselect_area(clip->timeline_in(), clip->timeline_out(), clip->track());
    if (!alt) {
      for (int i : clip->linked) {
        ClipPtr link = amber::ActiveSequence->clips.at(i);
        panel_timeline->deselect_area(link->timeline_in(), link->timeline_out(), link->track());
      }
    }
  } else if (panel_timeline->tool == TIMELINE_TOOL_POINTER && panel_timeline->transition_select != kTransitionNone) {
    // deselect clip + links, then select only the transition
    panel_timeline->deselect_area(clip->timeline_in(), clip->timeline_out(), clip->track());
    for (int i : clip->linked) {
      ClipPtr link = amber::ActiveSequence->clips.at(i);
      panel_timeline->deselect_area(link->timeline_in(), link->timeline_out(), link->track());
    }
    Selection s;
    s.track = clip->track();
    if (panel_timeline->transition_select == kTransitionOpening && clip->opening_transition != nullptr) {
      s.in = clip->timeline_in();
      if (clip->opening_transition->secondary_clip != nullptr) s.in -= clip->opening_transition->get_true_length();
      s.out = clip->timeline_in() + clip->opening_transition->get_true_length();
    } else if (panel_timeline->transition_select == kTransitionClosing && clip->closing_transition != nullptr) {
      s.in = clip->timeline_out() - clip->closing_transition->get_true_length();
      s.out = clip->timeline_out();
      if (clip->closing_transition->secondary_clip != nullptr) s.out += clip->closing_transition->get_true_length();
    }
    amber::ActiveSequence->selections.append(s);
  }
  if (amber::CurrentConfig.select_also_seeks) {
    panel_sequence_viewer->seek(panel_timeline->drag_frame_start);
  }
}

// Helper: handle pointer click on a clip that is not yet selected
static void mousePressPointerUnselectedClip(Clip* clip, bool shift, bool alt) {
  if (!shift) amber::ActiveSequence->selections.clear();

  Selection s;
  s.in = clip->timeline_in();
  s.out = clip->timeline_out();
  s.track = clip->track();

  if (panel_timeline->tool == TIMELINE_TOOL_POINTER) {
    if (panel_timeline->transition_select == kTransitionOpening && clip->opening_transition != nullptr) {
      s.out = clip->timeline_in() + clip->opening_transition->get_true_length();
      if (clip->opening_transition->secondary_clip != nullptr) s.in -= clip->opening_transition->get_true_length();
    } else if (panel_timeline->transition_select == kTransitionClosing && clip->closing_transition != nullptr) {
      s.in = clip->timeline_out() - clip->closing_transition->get_true_length();
      if (clip->closing_transition->secondary_clip != nullptr) s.out += clip->closing_transition->get_true_length();
    }
  }

  amber::ActiveSequence->selections.append(s);
  if (amber::CurrentConfig.select_also_seeks) {
    panel_sequence_viewer->seek(panel_timeline->drag_frame_start);
  }

  if (!alt && panel_timeline->transition_select == kTransitionNone) {
    for (int i : clip->linked) {
      Clip* link = amber::ActiveSequence->clips.at(i).get();
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

void TimelineWidget::mousePressPointer(int hovered_clip, bool shift, bool alt, int effective_tool) {
  if (track_resizing && effective_tool != TIMELINE_TOOL_MENU) {
    panel_timeline->moving_init = true;
    return;
  }

  if (hovered_clip >= 0) {
    Clip* clip = amber::ActiveSequence->clips.at(hovered_clip).get();
    if (clip->IsSelected()) {
      mousePressPointerSelectedClip(clip, shift, alt);
    } else {
      mousePressPointerUnselectedClip(clip, shift, alt);
    }
    if (effective_tool != TIMELINE_TOOL_MENU) {
      panel_timeline->moving_init = true;
    }
  } else {
    if (!shift) amber::ActiveSequence->selections.clear();
    panel_timeline->rect_select_init = true;
  }

  update_ui(false);
}

// Helper: dispatch press action for a given tool
void TimelineWidget::mousePressDispatchTool(int effective_tool, int hovered_clip, bool shift, bool alt) {
  switch (effective_tool) {
    case TIMELINE_TOOL_POINTER:
    case TIMELINE_TOOL_RIPPLE:
    case TIMELINE_TOOL_SLIP:
    case TIMELINE_TOOL_ROLLING:
    case TIMELINE_TOOL_SLIDE:
    case TIMELINE_TOOL_MENU:
      mousePressPointer(hovered_clip, shift, alt, effective_tool);
      break;
    case TIMELINE_TOOL_HAND:
      panel_timeline->hand_moving = true;
      break;
    case TIMELINE_TOOL_EDIT:
      if (amber::CurrentConfig.edit_tool_also_seeks) {
        panel_sequence_viewer->seek(panel_timeline->drag_frame_start);
      }
      panel_timeline->selecting = true;
      break;
    case TIMELINE_TOOL_RAZOR:
      panel_timeline->splitting = true;
      panel_timeline->split_tracks.append(panel_timeline->drag_track_start);
      update_ui(false);
      break;
    case TIMELINE_TOOL_TRANSITION:
      if (panel_timeline->transition_tool_open_clip > -1 || panel_timeline->transition_tool_close_clip > -1) {
        panel_timeline->transition_tool_init = true;
      }
      break;
    default:
      break;
  }
}

void TimelineWidget::mousePressEvent(QMouseEvent* event) {
  if (amber::ActiveSequence == nullptr) return;

  int effective_tool = panel_timeline->tool;

  if (event->button() == Qt::MiddleButton) {
    effective_tool = TIMELINE_TOOL_HAND;
    panel_timeline->creating = false;
  } else if (event->button() == Qt::RightButton) {
    effective_tool = TIMELINE_TOOL_MENU;
    panel_timeline->creating = false;
  }

  mouseMoveEvent(event);

  panel_timeline->drag_x_start = event->position().toPoint().x();
  panel_timeline->drag_y_start = event->position().toPoint().y();
  panel_timeline->drag_frame_start = panel_timeline->cursor_frame;
  panel_timeline->drag_track_start = panel_timeline->cursor_track;

  int hovered_clip = panel_timeline->trim_target == -1
                         ? getClipIndexFromCoords(panel_timeline->cursor_frame, panel_timeline->cursor_track)
                         : panel_timeline->trim_target;

  bool shift = (event->modifiers() & Qt::ShiftModifier);
  bool alt = (event->modifiers() & Qt::AltModifier);

  panel_timeline->selection_offset = shift ? amber::ActiveSequence->selections.size() : 0;

  if (panel_timeline->creating) {
    mousePressCreating();
  } else {
    mousePressDispatchTool(effective_tool, hovered_clip, shift, alt);
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
    case ADD_OBJ_BARS: {
      c->set_name(tr("Bars"));
      EffectPtr e = Effect::Create(c.get(), Effect::GetInternalMeta(EFFECT_INTERNAL_SOLID, EFFECT_TYPE_EFFECT));

      // Auto-select bars
      SolidEffect* solid_effect = static_cast<SolidEffect*>(e.get());
      solid_effect->SetType(SolidEffect::SOLID_TYPE_BARS);

      c->effects.append(e);
    } break;
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

// Helper: apply ripple tool adjustment when trimming
static void mouseReleaseMoveRipple(ComboAction* ca) {
  const Ghost& first_ghost = panel_timeline->ghosts.at(0);
  long ripple_length;
  long ripple_point = LONG_MAX;

  if (panel_timeline->trim_type == TRIM_IN) {
    ripple_length = first_ghost.old_in - first_ghost.in;
    for (auto& selection : amber::ActiveSequence->selections) {
      selection.in += ripple_length;
      selection.out += ripple_length;
    }
  } else {
    ripple_length = first_ghost.old_out - panel_timeline->ghosts.at(0).out;
  }

  QVector<int> ignore_clips;
  for (int i = 0; i < panel_timeline->ghosts.size(); i++) {
    const Ghost& g = panel_timeline->ghosts.at(i);
    if (panel_timeline->trim_type == TRIM_IN) {
      ignore_clips.append(g.clip);
      panel_timeline->ghosts[i].in += ripple_length;
      panel_timeline->ghosts[i].out += ripple_length;
    }
    long comp_point = (panel_timeline->trim_type == TRIM_IN) ? g.old_in : g.old_out;
    ripple_point = qMin(ripple_point, comp_point);
  }

  if (panel_timeline->trim_type == TRIM_OUT) ripple_length = -ripple_length;
  ripple_clips(ca, amber::ActiveSequence.get(), ripple_point, ripple_length, ignore_clips);
}

// Helper: alt+drag — duplicate clips to their new positions
static void mouseReleaseMoveAltDuplicate(ComboAction* ca) {
  QVector<int> old_clips;
  QVector<ClipPtr> new_clips;
  QVector<Selection> delete_areas;

  for (const auto& g : panel_timeline->ghosts) {
    if (g.old_in == g.in && g.old_out == g.out && g.track == g.old_track && g.clip_in == g.old_clip_in) continue;
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

  if (!new_clips.isEmpty()) {
    panel_timeline->delete_areas_and_relink(ca, delete_areas, false);
    panel_timeline->relink_clips_using_ids(old_clips, new_clips);
    ca->append(new AddClipCommand(amber::ActiveSequence.get(), new_clips));
  }
}

// Helper: overwrite move — mark ghosts undeletable, delete underlying area, then restore
static void mouseReleaseMoveDeleteUnder(ComboAction* ca) {
  QVector<Selection> delete_areas;
  for (const auto& g : panel_timeline->ghosts) {
    amber::ActiveSequence->clips.at(g.clip)->undeletable = true;
    if (g.transition != nullptr) {
      g.transition->parent_clip->undeletable = true;
      if (g.transition->secondary_clip != nullptr) g.transition->secondary_clip->undeletable = true;
    }
    Selection s;
    s.in = g.in;
    s.out = g.out;
    s.track = g.track;
    delete_areas.append(s);
  }
  panel_timeline->delete_areas_and_relink(ca, delete_areas, false);
  for (const auto& g : panel_timeline->ghosts) {
    amber::ActiveSequence->clips.at(g.clip)->undeletable = false;
    if (g.transition != nullptr) {
      g.transition->parent_clip->undeletable = false;
      if (g.transition->secondary_clip != nullptr) g.transition->secondary_clip->undeletable = false;
    }
  }
}

// Helper: apply actual clip/transition movement for one ghost
static void mouseReleaseMoveApplyOneGhost(ComboAction* ca, const Ghost& g, Clip* c) {
  if (g.transition == nullptr) {
    c->move(ca, (g.in - g.old_in), (g.out - g.old_out), (g.clip_in - g.old_clip_in), (g.track - g.old_track), false,
            true);
    return;
  }

  bool is_opening_transition = (g.transition == c->opening_transition);
  long new_transition_length = g.out - g.in;
  if (g.transition->secondary_clip != nullptr) new_transition_length >>= 1;
  ca->append(new ModifyTransitionCommand(is_opening_transition ? c->opening_transition : c->closing_transition,
                                         new_transition_length));

  long clip_length = c->length();

  if (g.transition->secondary_clip != nullptr) {
    if (g.in != g.old_in && g.trim_type == TRIM_NONE) {
      long movement = g.in - g.old_in;
      long timeline_out_movement = 0;
      if (g.out > g.transition->parent_clip->timeline_out())
        timeline_out_movement = g.out - g.transition->parent_clip->timeline_out();
      long timeline_in_movement = 0;
      if (g.in < g.transition->secondary_clip->timeline_in())
        timeline_in_movement = g.in - g.transition->secondary_clip->timeline_in();
      g.transition->parent_clip->move(ca, movement, timeline_out_movement, movement, 0, false, true);
      g.transition->secondary_clip->move(ca, timeline_in_movement, movement, timeline_in_movement, 0, false, true);
      make_room_for_transition(ca, g.transition->parent_clip, kTransitionOpening, g.in, g.out, false);
      make_room_for_transition(ca, g.transition->secondary_clip, kTransitionClosing, g.in, g.out, false);
    }
  } else if (is_opening_transition) {
    if (g.in != g.old_in) {
      long timeline_out_movement = 0;
      if (g.out > g.transition->parent_clip->timeline_out())
        timeline_out_movement = g.out - g.transition->parent_clip->timeline_out();
      c->move(ca, (g.in - g.old_in), timeline_out_movement, (g.clip_in - g.old_clip_in), 0, false, true);
      clip_length -= (g.in - g.old_in);
    }
    make_room_for_transition(ca, c, kTransitionOpening, g.in, g.out, false);
  } else {
    if (g.out != g.old_out) {
      long timeline_in_movement = 0;
      if (g.in < g.transition->parent_clip->timeline_in())
        timeline_in_movement = g.in - g.transition->parent_clip->timeline_in();
      c->move(ca, timeline_in_movement, (g.out - g.old_out), timeline_in_movement, 0, false, true);
      clip_length += (g.out - g.old_out);
    }
    make_room_for_transition(ca, c, kTransitionClosing, g.in, g.out, false);
  }
}

// Helper: verify/split shared transitions after clips have been moved
static void mouseReleaseMoveVerifyTransitions(ComboAction* ca) {
  for (int i = 0; i < panel_timeline->ghosts.size(); i++) {
    const Ghost& g = panel_timeline->ghosts.at(i);
    if (g.transition != nullptr) continue;

    ClipPtr c = amber::ActiveSequence->clips.at(g.clip);
    long new_clip_length = g.out - g.in;

    for (int t = kTransitionOpening; t <= kTransitionClosing; t++) {
      TransitionPtr transition = (t == kTransitionOpening) ? c->opening_transition : c->closing_transition;
      if (transition == nullptr) continue;

      if (new_clip_length < transition->get_true_length())
        ca->append(new ModifyTransitionCommand(transition, new_clip_length));

      if (transition->secondary_clip == nullptr) continue;

      bool edge_moved =
          (t == kTransitionOpening && g.in != g.old_in) || (t == kTransitionClosing && g.out != g.old_out);
      if (!edge_moved) continue;

      Clip* search_clip = (t == kTransitionOpening) ? transition->secondary_clip : transition->parent_clip;

      bool split = true;
      for (int j = 0; j < panel_timeline->ghosts.size(); j++) {
        const Ghost& other = panel_timeline->ghosts.at(j);
        if (amber::ActiveSequence->clips.at(other.clip).get() != search_clip) continue;
        bool edges_still_touch = (t == kTransitionOpening) ? (other.out == g.in) : (other.in == g.out);
        if (edges_still_touch || j < i) split = false;
        break;
      }

      if (split) {
        ca->append(new SetPointer(reinterpret_cast<void**>(&transition->secondary_clip), nullptr));
        ca->append(new AddTransitionCommand(nullptr, transition->secondary_clip, transition, nullptr, 0));
      }
    }
  }
}

bool TimelineWidget::mouseReleaseMoving(ComboAction* ca, bool alt, bool ctrl) {
  bool process_moving = false;
  for (const auto& g : panel_timeline->ghosts) {
    if (g.in != g.old_in || g.out != g.old_out || g.clip_in != g.old_clip_in || g.track != g.old_track) {
      process_moving = true;
      break;
    }
  }
  if (!process_moving) return false;

  if (panel_timeline->tool == TIMELINE_TOOL_RIPPLE) {
    mouseReleaseMoveRipple(ca);
  }

  if (panel_timeline->tool == TIMELINE_TOOL_POINTER && alt && panel_timeline->trim_target == -1) {
    mouseReleaseMoveAltDuplicate(ca);
    return true;
  }

  // overwrite or insert move
  if (panel_timeline->tool == TIMELINE_TOOL_POINTER && ctrl) {
    insert_clips(ca);
  } else if (panel_timeline->tool == TIMELINE_TOOL_POINTER || panel_timeline->tool == TIMELINE_TOOL_SLIDE) {
    mouseReleaseMoveDeleteUnder(ca);
  }

  for (auto& g : panel_timeline->ghosts) {
    mouseReleaseMoveApplyOneGhost(ca, g, amber::ActiveSequence->clips.at(g.clip).get());
  }

  mouseReleaseMoveVerifyTransitions(ca);
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
  ca->append(new AddTransitionCommand(open, close, nullptr, panel_timeline->transition_tool_meta, transition_length));

  return true;
}

bool TimelineWidget::mouseReleaseSplitting(ComboAction* ca, bool alt) {
  bool split = false;
  for (int i = 0; i < panel_timeline->split_tracks.size(); i++) {
    int split_index = getClipIndexFromCoords(panel_timeline->drag_frame_start, panel_timeline->split_tracks.at(i));
    if (split_index > -1 &&
        panel_timeline->split_clip_and_relink(ca, split_index, panel_timeline->drag_frame_start, !alt)) {
      split = true;
    }
  }
  panel_timeline->split_cache.clear();
  return split;
}

// Helper: reset all drag/action state after a mouse release
void TimelineWidget::mouseReleaseResetState() {
  panel_timeline->ghosts.clear();
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
}

void TimelineWidget::mouseReleaseEvent(QMouseEvent* event) {
  QToolTip::hideText();
  if (amber::ActiveSequence == nullptr) return;

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
    } else if (panel_timeline->transition_tool_proc) {
      push_undo = mouseReleaseTransition(ca);
      if (push_undo) ca->setText(tr("Add Transition"));
    } else if (panel_timeline->splitting) {
      push_undo = mouseReleaseSplitting(ca, alt);
      if (push_undo) ca->setText(tr("Split Clip(s)"));
    }
    // selecting/rect_select_proc: no undo action, just fall through

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

    mouseReleaseResetState();
    update_ui(true);
  }

  panel_timeline->hand_moving = false;
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

// Helper: build ghost list from current clip selection (pointer/ripple/etc. tools)
void TimelineWidget::mouseMoveMovingInitBuildGhosts() {
  for (int i = 0; i < amber::ActiveSequence->clips.size(); i++) {
    Clip* c = amber::ActiveSequence->clips.at(i).get();
    if (c == nullptr) continue;

    Ghost g;
    g.transition = nullptr;
    bool add = false;

    // pointer tool: check if a transition is selected (takes priority over clip selection)
    if (panel_timeline->tool == TIMELINE_TOOL_POINTER &&
        (c->opening_transition != nullptr || c->closing_transition != nullptr)) {
      for (const auto& s : amber::ActiveSequence->selections) {
        if (s.track != c->track()) continue;
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

    if (!add) add = c->IsSelected();
    if (!add) continue;

    // avoid adding dual transitions twice
    if (g.transition != nullptr) {
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

  // slide tool: add adjacent clips as trimming ghosts
  if (panel_timeline->tool == TIMELINE_TOOL_SLIDE) {
    int ghost_arr_size = panel_timeline->ghosts.size();
    for (int j = 0; j < amber::ActiveSequence->clips.size(); j++) {
      ClipPtr c = amber::ActiveSequence->clips.at(j);
      if (c == nullptr) continue;
      for (int i = 0; i < ghost_arr_size; i++) {
        Ghost& g = panel_timeline->ghosts[i];
        g.trim_type = TRIM_NONE;
        ClipPtr ghost_clip = amber::ActiveSequence->clips.at(g.clip);
        if (c->track() != ghost_clip->track()) continue;
        bool found = false;
        for (int k = 0; k < ghost_arr_size; k++) {
          if (panel_timeline->ghosts.at(k).clip == j) {
            found = true;
            break;
          }
        }
        if (!found) {
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

// Helper: cache pre/post clip lists for ripple tool validation
void TimelineWidget::mouseMoveMovingInitRipplePrep() {
  long axis = LONG_MAX;
  QVector<ClipPtr> ghost_clips;
  ghost_clips.resize(panel_timeline->ghosts.size());

  for (int i = 0; i < panel_timeline->ghosts.size(); i++) {
    ClipPtr c = amber::ActiveSequence->clips.at(panel_timeline->ghosts.at(i).clip);
    axis = qMin(axis, (panel_timeline->trim_type == TRIM_IN) ? c->timeline_in() : c->timeline_out());
    ghost_clips[i] = c;
  }

  for (auto c : amber::ActiveSequence->clips) {
    if (c == nullptr || ghost_clips.contains(c)) continue;
    bool clip_is_post = (c->timeline_in() >= axis);
    QVector<ClipPtr>& clip_list = clip_is_post ? post_clips : pre_clips;
    bool found = false;
    for (auto& j : clip_list) {
      if (j->track() == c->track()) {
        if ((!clip_is_post && j->timeline_out() < c->timeline_out()) ||
            (clip_is_post && j->timeline_in() > c->timeline_in())) {
          j = c;
        }
        found = true;
        break;
      }
    }
    if (!found) clip_list.append(c);
  }
}

void TimelineWidget::mouseMoveMovingInit(QMouseEvent* event) {
  if (!event) {
    qWarning() << "mouseMoveMovingInit: event is null";
    return;
  }

  if (track_resizing) {
    int diff = event->position().toPoint().y() - panel_timeline->drag_y_start;
    int new_height = panel_timeline->GetTrackHeight(track_target);
    new_height = bottom_align ? new_height - diff : new_height + diff;
    new_height = qMax(new_height, amber::timeline::kTrackMinHeight);
    panel_timeline->SetTrackHeight(track_target, new_height);
    panel_timeline->drag_y_start = event->position().toPoint().y();
    update();
  } else if (panel_timeline->moving_proc) {
    update_ghosts(event->position().toPoint(), event->modifiers() & Qt::ShiftModifier);
  } else {
    mouseMoveMovingInitBuildGhosts();
    init_ghosts();
    if (panel_timeline->tool == TIMELINE_TOOL_RIPPLE) {
      mouseMoveMovingInitRipplePrep();
    }
    selection_command = new SetSelectionsCommand(amber::ActiveSequence.get());
    selection_command->old_data = amber::ActiveSequence->selections;
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

// Helper: collect clips inside the rectangle selection area, including linked clips unless alt is held
static QVector<ClipPtr> rectSelectCollectClips(long frame_min, long frame_max, int track_min, int track_max, bool alt) {
  QVector<ClipPtr> selected_clips;
  for (const auto& clip : amber::ActiveSequence->clips) {
    if (clip == nullptr) continue;
    if (clip->track() < track_min || clip->track() > track_max) continue;
    if (clip->timeline_in() < frame_min && clip->timeline_out() < frame_min) continue;
    if (clip->timeline_in() > frame_max && clip->timeline_out() > frame_max) continue;

    QVector<ClipPtr> session_clips;
    session_clips.append(clip);
    if (!alt) {
      for (int j : clip->linked) session_clips.append(amber::ActiveSequence->clips.at(j));
    }

    for (auto c : session_clips) {
      if (!selected_clips.contains(c)) selected_clips.append(c);
    }
  }
  return selected_clips;
}

void TimelineWidget::mouseMoveRectSelect(QMouseEvent* event, bool alt) {
  if (!event) {
    qWarning() << "mouseMoveRectSelect: event is null";
    return;
  }

  if (!panel_timeline->rect_select_proc) {
    // initialise rectangle selection
    int y = event->position().toPoint().y();
    panel_timeline->rect_select_rect.setX(event->position().toPoint().x());
    panel_timeline->rect_select_rect.setY(bottom_align ? y - height() : y);
    panel_timeline->rect_select_rect.setWidth(0);
    panel_timeline->rect_select_rect.setHeight(0);
    panel_timeline->rect_select_proc = true;
    return;
  }

  // update rectangle bounds
  int y = event->position().toPoint().y();
  panel_timeline->rect_select_rect.setRight(event->position().toPoint().x());
  panel_timeline->rect_select_rect.setBottom(bottom_align ? y - height() : y);

  long frame_min = qMin(panel_timeline->drag_frame_start, panel_timeline->cursor_frame);
  long frame_max = qMax(panel_timeline->drag_frame_start, panel_timeline->cursor_frame);
  int track_min = qMin(panel_timeline->drag_track_start, panel_timeline->cursor_track);
  int track_max = qMax(panel_timeline->drag_track_start, panel_timeline->cursor_track);

  QVector<ClipPtr> selected_clips = rectSelectCollectClips(frame_min, frame_max, track_min, track_max, alt);

  amber::ActiveSequence->selections.resize(selected_clips.size() + panel_timeline->selection_offset);
  for (int i = 0; i < selected_clips.size(); i++) {
    Selection& s = amber::ActiveSequence->selections[i + panel_timeline->selection_offset];
    ClipPtr clip = selected_clips.at(i);
    s.old_in = s.in = clip->timeline_in();
    s.old_out = s.out = clip->timeline_out();
    s.old_track = s.track = clip->track();
  }

  panel_timeline->repaint_timeline();
}

// Helper: check if cursor is near a transition trim edge (pointer tool only); updates
// trim_target/type/transition_select
static void hoverCheckTransitionTrimPoints(int clip_index, ClipPtr c, int mouse_frame_lower, int mouse_frame_upper,
                                           const QPoint& pos, int& closeness, bool& found) {
  if (c->opening_transition != nullptr) {
    int tp =
        panel_timeline->getTimelineScreenPointFromFrame(c->timeline_in() + c->opening_transition->get_true_length());
    if (tp > mouse_frame_lower && tp < mouse_frame_upper) {
      int nc = qAbs(tp - 1 - pos.x());
      if (nc < closeness) {
        panel_timeline->trim_target = clip_index;
        panel_timeline->trim_type = TRIM_OUT;
        panel_timeline->transition_select = kTransitionOpening;
        closeness = nc;
        found = true;
      }
    }
  }
  if (c->closing_transition != nullptr) {
    int tp =
        panel_timeline->getTimelineScreenPointFromFrame(c->timeline_out() - c->closing_transition->get_true_length());
    if (tp > mouse_frame_lower && tp < mouse_frame_upper) {
      int nc = qAbs(tp + 1 - pos.x());
      if (nc < closeness) {
        panel_timeline->trim_target = clip_index;
        panel_timeline->trim_type = TRIM_IN;
        panel_timeline->transition_select = kTransitionClosing;
        closeness = nc;
        found = true;
      }
    }
  }
}

// Helper: detect and set track-resize cursor when no trim target was found
void TimelineWidget::hoverCheckTrackResize(const QMouseEvent* event, bool cursor_contains_clip, int min_track,
                                           int max_track) {
  unsetCursor();
  int test_range = 5;
  int mouse_pos = event->position().toPoint().y();
  int hover_track = getTrackFromScreenPoint(mouse_pos);
  int track_y_edge = getScreenPointFromTrack(hover_track);
  if (!bottom_align) track_y_edge += panel_timeline->GetTrackHeight(hover_track);
  if (mouse_pos > track_y_edge - test_range && mouse_pos < track_y_edge + test_range) {
    bool in_range =
        cursor_contains_clip || (amber::CurrentConfig.show_track_lines && panel_timeline->cursor_track >= min_track &&
                                 panel_timeline->cursor_track <= max_track);
    if (in_range) {
      track_resizing = true;
      track_target = hover_track;
      setCursor(Qt::SizeVerCursor);
    }
  }
}

void TimelineWidget::mouseMoveHoverTrimDetection(QMouseEvent* event) {
  if (!event) {
    qWarning() << "mouseMoveHoverTrimDetection: event is null";
    return;
  }
  QToolTip::hideText();
  QPoint pos = event->position().toPoint();

  int lim = 5;
  int mouse_frame_lower = pos.x() - lim;
  int mouse_frame_upper = pos.x() + lim;

  bool found = false;
  bool cursor_contains_clip = false;
  int closeness = INT_MAX;
  int min_track = INT_MAX;
  int max_track = INT_MIN;

  panel_timeline->transition_select = kTransitionNone;
  panel_timeline->trim_type = TRIM_NONE;
  panel_timeline->trim_target = -1;

  for (int i = 0; i < amber::ActiveSequence->clips.size(); i++) {
    ClipPtr c = amber::ActiveSequence->clips.at(i);
    if (c == nullptr) continue;

    min_track = qMin(min_track, c->track());
    max_track = qMax(max_track, c->track());

    if (c->track() != panel_timeline->cursor_track) continue;

    if (panel_timeline->cursor_frame >= c->timeline_in() && panel_timeline->cursor_frame <= c->timeline_out()) {
      cursor_contains_clip = true;
      tooltip_timer.start();
      tooltip_clip = i;
      if (c->opening_transition != nullptr &&
          panel_timeline->cursor_frame <= c->timeline_in() + c->opening_transition->get_true_length()) {
        panel_timeline->transition_select = kTransitionOpening;
      } else if (c->closing_transition != nullptr &&
                 panel_timeline->cursor_frame >= c->timeline_out() - c->closing_transition->get_true_length()) {
        panel_timeline->transition_select = kTransitionClosing;
      }
    }

    int visual_in = panel_timeline->getTimelineScreenPointFromFrame(c->timeline_in());
    int visual_out = panel_timeline->getTimelineScreenPointFromFrame(c->timeline_out());

    if (visual_in > mouse_frame_lower && visual_in < mouse_frame_upper) {
      int nc = qAbs(visual_in + 1 - pos.x());
      if (nc < closeness) {
        panel_timeline->trim_target = i;
        panel_timeline->trim_type = TRIM_IN;
        closeness = nc;
        found = true;
      }
    }

    if (visual_out > mouse_frame_lower && visual_out < mouse_frame_upper) {
      int nc = qAbs(visual_out - 1 - pos.x());
      if (nc < closeness) {
        panel_timeline->trim_target = i;
        panel_timeline->trim_type = TRIM_OUT;
        closeness = nc;
        found = true;
      }
    }

    if (panel_timeline->tool == TIMELINE_TOOL_POINTER) {
      hoverCheckTransitionTrimPoints(i, c, mouse_frame_lower, mouse_frame_upper, pos, closeness, found);
    }
  }

  if (found) {
    if (panel_timeline->trim_type == TRIM_IN) {
      setCursor(panel_timeline->tool == TIMELINE_TOOL_RIPPLE ? amber::cursor::LeftRipple : amber::cursor::LeftTrim);
    } else {
      setCursor(panel_timeline->tool == TIMELINE_TOOL_RIPPLE ? amber::cursor::RightRipple : amber::cursor::RightTrim);
    }
  } else {
    hoverCheckTrackResize(event, cursor_contains_clip, min_track, max_track);
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

      g.in = g.old_in = g.out = g.old_out = (primary_type == kTransitionOpening) ? c->timeline_in() : c->timeline_out();

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
    panel_timeline->cursor_frame = panel_timeline->getTimelineFrameFromScreenPoint(event->position().toPoint().x());
    panel_timeline->cursor_track = getTrackFromScreenPoint(event->position().toPoint().y());

    // if holding the mouse button down, let's scroll to that location
    if (event->buttons() != 0 && panel_timeline->tool != TIMELINE_TOOL_HAND) {
      panel_timeline->scroll_to_frame(panel_timeline->cursor_frame);
    }

    // determine if the action should be "inserting" rather than "overwriting"
    // Default behavior is to replace/overwrite clips under any clips we're dropping over them. Inserting will
    // split and move existing clips at the drop point to make space for the drop
    panel_timeline->move_insert =
        ((event->modifiers() & Qt::ControlModifier) &&
         (panel_timeline->tool == TIMELINE_TOOL_POINTER || panel_timeline->importing || panel_timeline->creating));

    // if we're not currently resizing already, default track resizing to false (we'll set it to true later if
    // the user is still hovering over a track line)
    if (!panel_timeline->moving_init) {
      track_resizing = false;
    }

    // if the current tool uses an on-screen visible cursor, we snap the cursor to the timeline
    if (current_tool_shows_cursor()) {
      panel_timeline->snap_to_timeline(&panel_timeline->cursor_frame,

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
      panel_timeline->repaint_timeline();
    } else {
      switch (panel_timeline->tool) {
        case TIMELINE_TOOL_POINTER:
        case TIMELINE_TOOL_RIPPLE:
        case TIMELINE_TOOL_ROLLING:
          mouseMoveHoverTrimDetection(event);
          break;
        case TIMELINE_TOOL_SLIP:
          if (getClipIndexFromCoords(panel_timeline->cursor_frame, panel_timeline->cursor_track) > -1) {
            setCursor(amber::cursor::Slip);
          } else {
            unsetCursor();
          }
          break;
        case TIMELINE_TOOL_TRANSITION:
          mouseMoveHoverTransition(event);
          break;
        default:
          break;
      }
    }
  }
}

void TimelineWidget::leaveEvent(QEvent*) { tooltip_timer.stop(); }
void TimelineWidget::resizeEvent(QResizeEvent*) { scrollBar->setPageStep(height()); }

bool TimelineWidget::is_track_visible(int track) { return (bottom_align == (track < 0)); }

// **************************************
// screen point <-> frame/track functions
// **************************************

int TimelineWidget::getTrackFromScreenPoint(int y) {
  int track_candidate = 0;

  y += scroll;

  if (bottom_align) {
    y -= height();
  }

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

  for (int i = start; i != track; i += interval) {
    point += panel_timeline->GetTrackHeight(i);
    if (amber::CurrentConfig.show_track_lines) point++;
  }

  if (bottom_align) {
    return height() - point - scroll;
  } else {
    return point - scroll;
  }
}

int TimelineWidget::getClipIndexFromCoords(long frame, int track) {
  for (int i = 0; i < amber::ActiveSequence->clips.size(); i++) {
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
