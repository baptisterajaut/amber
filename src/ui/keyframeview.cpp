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

#include "keyframeview.h"

#include <QMouseEvent>
#include <QtMath>

#include "dialogs/keyframepropertiesdialog.h"
#include "effects/effect.h"
#include "effects/keyframe.h"
#include "engine/clip.h"
#include "engine/sequence.h"
#include "engine/undo/undo.h"
#include "engine/undo/undostack.h"
#include "global/config.h"
#include "panels/grapheditor.h"
#include "panels/panels.h"
#include "panels/timeline.h"
#include "panels/viewer.h"
#include "ui/clickablelabel.h"
#include "ui/collapsiblewidget.h"
#include "ui/graphview.h"
#include "ui/keyframedrawing.h"
#include "ui/menu.h"
#include "ui/rectangleselect.h"
#include "ui/resizablescrollbar.h"
#include "ui/timelineheader.h"
#include "ui/viewerwidget.h"

KeyframeView::KeyframeView(QWidget* parent)
    : QWidget(parent)

{
  setFocusPolicy(Qt::ClickFocus);
  setMouseTracking(true);

  setContextMenuPolicy(Qt::CustomContextMenu);
  connect(this, &KeyframeView::customContextMenuRequested, this, &KeyframeView::show_context_menu);
}

void KeyframeView::SetEffects(const QVector<EffectUI*>& open_effects) { open_effects_ = open_effects; }

void KeyframeView::show_context_menu(const QPoint& pos) {
  if (selected_fields.size() > 0) {
    Menu menu(this);

    QAction* linear = menu.addAction(tr("Linear"));
    linear->setData(EFFECT_KEYFRAME_LINEAR);
    QAction* bezier = menu.addAction(tr("Bezier"));
    bezier->setData(EFFECT_KEYFRAME_BEZIER);
    QAction* hold = menu.addAction(tr("Hold"));
    hold->setData(EFFECT_KEYFRAME_HOLD);
    menu.addSeparator();
    menu.addAction("Graph Editor");
    menu.addSeparator();
    QAction* properties = menu.addAction(tr("Properties..."));
    properties->setData(-2);  // sentinel to distinguish from type actions

    connect(&menu, &QMenu::triggered, this, &KeyframeView::menu_set_key_type);
    connect(properties, &QAction::triggered, this, [this]() {
      KeyframePropertiesDialog dlg(this, selected_fields, selected_keyframes,
                                   amber::ActiveSequence ? amber::ActiveSequence->frame_rate : 30.0);
      dlg.exec();
    });
    menu.exec(mapToGlobal(pos));
  }
}

void KeyframeView::menu_set_key_type(QAction* a) {
  if (a->data().isNull()) {
    // load graph editor
    panel_graph_editor->show();
  } else if (a->data().toInt() < 0) {
    // handled elsewhere (e.g. Properties dialog)
    return;
  } else {
    int new_type = a->data().toInt();
    ComboAction* ca = new ComboAction(tr("Change Keyframe Type"));
    for (int i = 0; i < selected_fields.size(); i++) {
      EffectField* f = selected_fields.at(i);
      ca->append(new SetInt(&f->keyframes[selected_keyframes.at(i)].type, new_type));
    }
    amber::UndoStack.push(ca);

    // Sticky keyframe type: update config default
    if (amber::CurrentConfig.sticky_keyframe_type) {
      amber::CurrentConfig.default_keyframe_type = new_type;
    }

    update_ui(false);
  }
}

// Returns true if the keyframe at (field, keyframe_index) is the only field contributing at its time.
static bool keyframe_has_unique_time(EffectRow* row, EffectField* field, int keyframe_index) {
  long t = field->keyframes.at(keyframe_index).time;
  int appearances = 0;
  for (int m = 0; m < row->FieldCount(); m++) {
    for (const auto& kf : row->Field(m)->keyframes) {
      if (kf.time == t) appearances++;
    }
  }
  return appearances != row->FieldCount();
}

void KeyframeView::paint_row_keyframes(QPainter& p, EffectRow* row, int field_count, int keyframe_y) {
  QVector<long> key_times;
  for (int l = 0; l < field_count; l++) {
    EffectField* f = row->Field(l);
    for (int k = 0; k < f->keyframes.size(); k++) {
      if (key_times.contains(f->keyframes.at(k).time)) continue;
      bool keyframe_selected = keyframeIsSelected(f, k);
      long keyframe_frame = adjust_row_keyframe(row, f->keyframes.at(k).time, visible_in);
      int screen_x = getScreenPointFromFrame(panel_effect_controls->zoom, keyframe_frame) - x_scroll;
      if (keyframe_has_unique_time(row, f, k)) {
        QColor cc = get_curve_color(l, field_count);
        draw_keyframe(p, f->keyframes.at(k).type, screen_x, keyframe_y, keyframe_selected, cc.red(), cc.green(),
                      cc.blue());
      } else {
        draw_keyframe(p, f->keyframes.at(k).type, screen_x, keyframe_y, keyframe_selected);
      }
      key_times.append(f->keyframes.at(k).time);
    }
  }
}

void KeyframeView::paintEvent(QPaintEvent*) {
  QPainter p(this);

  rowY.clear();
  rows.clear();

  if (!open_effects_.isEmpty()) {
    visible_in = LONG_MAX;
    visible_out = 0;

    for (auto open_effect : open_effects_) {
      Clip* c = open_effect->GetEffect()->parent_clip;
      visible_in = qMin(visible_in, c->timeline_in());
      visible_out = qMax(visible_out, c->timeline_out());
    }

    for (auto container : open_effects_) {
      Effect* e = container->GetEffect();
      if (!container->IsExpanded()) continue;
      for (int j = 0; j < e->row_count(); j++) {
        EffectRow* row = e->row(j);
        int keyframe_y = container->GetRowY(j, this);
        paint_row_keyframes(p, row, row->FieldCount(), keyframe_y);
        rows.append(row);
        rowY.append(keyframe_y);
      }
    }

    int max_width = getScreenPointFromFrame(panel_effect_controls->zoom, visible_out - visible_in);
    if (max_width < width()) {
      p.fillRect(QRect(max_width, 0, width(), height()), QColor(0, 0, 0, 64));
    }
    panel_effect_controls->horizontalScrollBar->setMaximum(qMax(max_width - width(), 0));
    header->set_visible_in(visible_in);

    int playhead_x =
        getScreenPointFromFrame(panel_effect_controls->zoom, amber::ActiveSequence->playhead - visible_in) - x_scroll;
    p.setPen((dragging && panel_timeline->snapped) ? Qt::white : Qt::red);
    p.drawLine(playhead_x, 0, playhead_x, height());
  }

  if (select_rect) {
    draw_selection_rectangle(p, QRect(rect_select_x, rect_select_y, rect_select_w, rect_select_h));
  }
}

void KeyframeView::wheelEvent(QWheelEvent* e) { emit wheel_event_signal(e); }

bool KeyframeView::keyframeIsSelected(EffectField* field, int keyframe) {
  for (int i = 0; i < selected_fields.size(); i++) {
    if (selected_fields.at(i) == field && selected_keyframes.at(i) == keyframe) {
      return true;
    }
  }
  return false;
}

void KeyframeView::update_keys() {
  //	panel_graph_editor->update_panel();
  update();
}

void KeyframeView::delete_selected_keyframes() { delete_keyframes(selected_fields, selected_keyframes); }

void KeyframeView::set_x_scroll(int s) {
  x_scroll = s;
  update_keys();
}

void KeyframeView::set_y_scroll(int s) {
  y_scroll = s;
  update_keys();
}

void KeyframeView::resize_move(double d) {
  panel_effect_controls->zoom *= d;
  header->update_zoom(panel_effect_controls->zoom);
  update();
}

int KeyframeView::press_find_key(int mouse_x, int mouse_y, int& row_index, int& field_index) {
  long frame_min = getFrameFromScreenPoint(panel_effect_controls->zoom, mouse_x - KEYFRAME_SIZE);
  long frame_max = getFrameFromScreenPoint(panel_effect_controls->zoom, mouse_x + KEYFRAME_SIZE);
  long frame_diff = 0;
  int keyframe_index = -1;

  for (int i = 0; i < rowY.size(); i++) {
    if (mouse_y <= rowY.at(i) - KEYFRAME_SIZE * 2 || mouse_y >= rowY.at(i) + KEYFRAME_SIZE * 2) continue;

    EffectRow* row = rows.at(i);
    row->FocusRow();

    for (int k = 0; k < row->FieldCount(); k++) {
      EffectField* f = row->Field(k);
      for (int j = 0; j < f->keyframes.size(); j++) {
        long eval_time = f->keyframes.at(j).time - row->GetParentEffect()->parent_clip->clip_in() +
                         (row->GetParentEffect()->parent_clip->timeline_in() - visible_in);
        if (eval_time < frame_min || eval_time > frame_max) continue;
        long diff = qAbs(eval_time - drag_frame_start);
        if (keyframe_index == -1 || diff < frame_diff) {
          row_index = i;
          field_index = k;
          keyframe_index = j;
          frame_diff = diff;
        }
      }
    }
    break;
  }
  return keyframe_index;
}

void KeyframeView::press_extend_same_time_selection(int row_index, int field_index, int keyframe_index) {
  long comp_time = rows.at(row_index)->Field(field_index)->keyframes.at(keyframe_index).time;
  for (int i = 0; i < rows.at(row_index)->FieldCount(); i++) {
    if (i == field_index) continue;
    EffectField* f = rows.at(row_index)->Field(i);
    for (int j = 0; j < f->keyframes.size(); j++) {
      if (f->keyframes.at(j).time == comp_time) {
        selected_fields.append(f);
        selected_keyframes.append(j);
      }
    }
  }
}

void KeyframeView::mousePressEvent(QMouseEvent* event) {
  rect_select_x = qRound(event->position().x());
  rect_select_y = qRound(event->position().y());
  rect_select_w = 0;
  rect_select_h = 0;

  if (panel_timeline->tool == TIMELINE_TOOL_HAND || event->buttons() & Qt::MiddleButton) {
    scroll_drag = true;
    return;
  }

  old_key_vals.clear();

  int mouse_x = qRound(event->position().x()) + x_scroll;
  int mouse_y = qRound(event->position().y());
  int row_index = -1;
  int field_index = -1;

  drag_frame_start = getFrameFromScreenPoint(panel_effect_controls->zoom, mouse_x);
  int keyframe_index = press_find_key(mouse_x, mouse_y, row_index, field_index);

  bool already_selected = false;
  keys_selected = false;
  if (keyframe_index > -1) {
    already_selected = keyframeIsSelected(rows.at(row_index)->Field(field_index), keyframe_index);
  } else {
    select_rect = true;
  }

  if (!already_selected) {
    if (!(event->modifiers() & Qt::ShiftModifier)) {
      selected_fields.clear();
      selected_keyframes.clear();
    }
    if (keyframe_index > -1) {
      selected_fields.append(rows.at(row_index)->Field(field_index));
      selected_keyframes.append(keyframe_index);
      press_extend_same_time_selection(row_index, field_index, keyframe_index);
    }
  }

  if (!selected_fields.isEmpty()) {
    for (int i = 0; i < selected_fields.size(); i++) {
      old_key_vals.append(selected_fields.at(i)->keyframes.at(selected_keyframes.at(i)).time);
    }
    keys_selected = true;
  }

  rect_select_offset = selected_fields.size();
  update_keys();

  if (event->button() == Qt::LeftButton) mousedown = true;
}

void KeyframeView::move_rect_select(QMouseEvent* event) {
  selected_fields.resize(rect_select_offset);
  selected_keyframes.resize(rect_select_offset);

  rect_select_w = qRound(event->position().x()) - rect_select_x;
  rect_select_h = qRound(event->position().y()) - rect_select_y;

  int min_row = qMin(rect_select_y, qRound(event->position().y())) - KEYFRAME_SIZE;
  int max_row = qMax(rect_select_y, qRound(event->position().y())) + KEYFRAME_SIZE;

  int mouse_x = qRound(event->position().x()) + x_scroll;
  long current_frame = getFrameFromScreenPoint(panel_effect_controls->zoom, mouse_x);
  long frame_start = getFrameFromScreenPoint(panel_effect_controls->zoom, rect_select_x + x_scroll);
  long min_frame = qMin(frame_start, current_frame) - KEYFRAME_SIZE;
  long max_frame = qMax(frame_start, current_frame) + KEYFRAME_SIZE;

  for (int i = 0; i < rowY.size(); i++) {
    if (rowY.at(i) < min_row || rowY.at(i) > max_row) continue;
    EffectRow* row = rows.at(i);
    for (int k = 0; k < row->FieldCount(); k++) {
      EffectField* field = row->Field(k);
      for (int j = 0; j < field->keyframes.size(); j++) {
        long kf = adjust_row_keyframe(row, field->keyframes.at(j).time, visible_in);
        if (!keyframeIsSelected(field, j) && kf >= min_frame && kf <= max_frame) {
          selected_fields.append(field);
          selected_keyframes.append(j);
        }
      }
    }
  }
  update_keys();
}

void KeyframeView::move_keys_drag(QMouseEvent* event) {
  int mouse_x = qRound(event->position().x()) + x_scroll;
  long current_frame = getFrameFromScreenPoint(panel_effect_controls->zoom, mouse_x);
  long frame_diff = current_frame - drag_frame_start;

  panel_timeline->snapped = false;
  if (panel_timeline->snapping) {
    for (int i = 0; i < selected_keyframes.size(); i++) {
      EffectField* field = selected_fields.at(i);
      Clip* c = field->GetParentRow()->GetParentEffect()->parent_clip;
      long key_time = old_key_vals.at(i) + frame_diff - c->clip_in() + c->timeline_in();
      long key_eval = key_time;
      if (panel_timeline->snap_to_point(amber::ActiveSequence->playhead, &key_eval)) {
        frame_diff += (key_eval - key_time);
        break;
      }
    }
  }

  for (int i = 0; i < selected_fields.size(); i++) {
    EffectField* field = selected_fields.at(i);
    long eval_key = old_key_vals.at(i);
    for (int j = 0; j < field->keyframes.size(); j++) {
      while (!keyframeIsSelected(field, j) && field->keyframes.at(j).time == eval_key + frame_diff) {
        frame_diff += (last_frame_diff > frame_diff) ? 1 : -1;
        panel_timeline->snapped = false;
      }
    }
  }

  for (int i = 0; i < selected_keyframes.size(); i++) {
    selected_fields.at(i)->keyframes[selected_keyframes.at(i)].time = old_key_vals.at(i) + frame_diff;
  }

  last_frame_diff = frame_diff;
  dragging = true;
  update_ui(false);
}

void KeyframeView::mouseMoveEvent(QMouseEvent* event) {
  if (panel_timeline->tool == TIMELINE_TOOL_HAND) {
    setCursor(Qt::OpenHandCursor);
  } else {
    unsetCursor();
  }

  if (scroll_drag) {
    panel_effect_controls->horizontalScrollBar->setValue(panel_effect_controls->horizontalScrollBar->value() +
                                                         rect_select_x - event->position().toPoint().x());
    panel_effect_controls->verticalScrollBar->setValue(panel_effect_controls->verticalScrollBar->value() +
                                                       rect_select_y - event->position().toPoint().y());
    rect_select_x = event->position().toPoint().x();
    rect_select_y = event->position().toPoint().y();
    return;
  }

  if (!mousedown) return;

  int mouse_x = qRound(event->position().x()) + x_scroll;
  long current_frame = getFrameFromScreenPoint(panel_effect_controls->zoom, mouse_x);
  panel_effect_controls->scroll_to_frame(current_frame + visible_in);

  if (select_rect) {
    move_rect_select(event);
  } else if (keys_selected) {
    move_keys_drag(event);
  }
}

void KeyframeView::mouseReleaseEvent(QMouseEvent*) {
  if (dragging) {
    ComboAction* ca = new ComboAction(tr("Move Keyframe(s)"));
    for (int i = 0; i < selected_fields.size(); i++) {
      ca->append(new SetLong(&selected_fields.at(i)->keyframes[selected_keyframes.at(i)].time, old_key_vals.at(i),
                             selected_fields.at(i)->keyframes.at(selected_keyframes.at(i)).time));
    }
    amber::UndoStack.push(ca);
  }

  select_rect = false;
  dragging = false;
  mousedown = false;
  scroll_drag = false;
  panel_timeline->snapped = false;
  update_ui(false);
}
