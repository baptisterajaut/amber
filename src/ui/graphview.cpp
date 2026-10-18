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

#include "graphview.h"

#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QtMath>
#include <cfloat>
#include <cmath>
#include <limits>

#include "core/graphmath.h"
#include "effects/effect.h"
#include "effects/keyframe.h"
#include "engine/clip.h"
#include "engine/sequence.h"
#include "engine/undo/undo.h"
#include "engine/undo/undostack.h"
#include "global/config.h"
#include "global/debug.h"
#include "panels/panels.h"
#include "panels/timeline.h"
#include "panels/viewer.h"
#include "ui/keyframedrawing.h"
#include "ui/menu.h"
#include "ui/rectangleselect.h"

const double kGraphZoomSpeed = 0.05;
const double kGraphMinZoom = 1e-4;  // set_zoom floor: mouse deltas are divided by the zoom
const int kBezierHandleSize = 3;
const int kBezierLineSize = 2;

const int kBezierHandleNone = 1;
const int kBezierHandlePre = 2;
const int kBezierHandlePost = 3;

QColor get_curve_color(int index, int length) {
  QColor c;
  int hue = qRound((double(index) / double(length)) * 255);
  c.setHsv(hue, 255, 255);
  return c;
}

// An undo can remove keyframes behind the selection's back: forget entries that no longer exist.
// Called right before the selection is used.
// CBA: only drops out-of-range indices; an undo removing an earlier keyframe shifts later ones, so the selection then
// points at a different keyframe. Upgrade path: select by keyframe identity instead of index.
static void drop_stale_selection(EffectRow* row, QVector<int>& keys, QVector<int>& key_fields) {
  if (row == nullptr) return;
  const int n = qMin(keys.size(), key_fields.size());
  keys.resize(n);
  key_fields.resize(n);
  for (int i = n - 1; i >= 0; i--) {
    const int f = key_fields.at(i);
    if (f < 0 || f >= row->FieldCount() || keys.at(i) < 0 || keys.at(i) >= row->Field(f)->keyframes.size()) {
      keys.removeAt(i);
      key_fields.removeAt(i);
    }
  }
}

// CBA: one row at a time; multi-property overlay, speed graph and auto-bezier handles if asked

// A value set from the mouse stays inside the field's range
static double clamp_to_field(EffectField* field, double value) {
  if (field->type() != EffectField::EFFECT_FIELD_DOUBLE) return value;
  DoubleField* df = static_cast<DoubleField*>(field);
  return amber::graph::ClampToRange(value, df->GetMinimum(), df->GetMaximum());
}

GraphView::GraphView(QWidget* parent) : QWidget(parent) {
  x_scroll = 0;
  y_scroll = 0;
  mousedown = false;
  x_zoom = 1.0;
  y_zoom = 1.0;
  row = nullptr;
  moved_keys = false;
  last_frame_diff = 0;
  snapped = false;
  snap_is_playhead = false;
  snap_time = 0;
  current_handle = kBezierHandleNone;
  rect_select = false;
  visible_in = 0;
  click_add_proc = false;

  setMouseTracking(true);
  setFocusPolicy(Qt::ClickFocus);
  setContextMenuPolicy(Qt::CustomContextMenu);
  connect(this, &GraphView::customContextMenuRequested, this, &GraphView::show_context_menu);
}

// The Graph Editor's own keys, taken before the main window's shortcuts while the view has focus
static bool is_view_key(const QKeyEvent* e) {
  const Qt::KeyboardModifiers mods = e->modifiers() & (Qt::ShiftModifier | Qt::ControlModifier | Qt::AltModifier);
  if (e->key() == Qt::Key_F9) {
    return mods == Qt::NoModifier || mods == Qt::ShiftModifier || mods == (Qt::ControlModifier | Qt::ShiftModifier);
  }
  // Shift+F is also Freeze Frame in the main window: the Graph Editor takes it while its view has focus
  if (e->key() == Qt::Key_F) return mods == Qt::NoModifier || mods == Qt::ShiftModifier;
  return false;
}

bool GraphView::event(QEvent* e) {
  if (e->type() == QEvent::ShortcutOverride && row != nullptr && is_view_key(static_cast<QKeyEvent*>(e))) {
    e->accept();
    return true;
  }
  return QWidget::event(e);
}

void GraphView::keyPressEvent(QKeyEvent* e) {
  if (row == nullptr || !is_view_key(e)) {
    QWidget::keyPressEvent(e);
    return;
  }
  const bool shift = e->modifiers() & Qt::ShiftModifier;
  const bool ctrl = e->modifiers() & Qt::ControlModifier;
  if (e->key() == Qt::Key_F9) {
    easy_ease(ctrl ? amber::graph::EaseSide::kOut
                   : (shift ? amber::graph::EaseSide::kIn : amber::graph::EaseSide::kBoth));
  } else if (e->key() == Qt::Key_F) {
    if (shift) {
      frame_all();
    } else {
      frame_selected();
    }
  }
}

void GraphView::frame_selected() {
  if (row == nullptr) return;
  drop_stale_selection(row, selected_keys, selected_keys_fields);
  if (selected_keys.isEmpty()) {
    set_view_to_all();
  } else {
    set_view_to_selection();
  }
}

void GraphView::frame_all() { set_view_to_all(); }

void GraphView::show_context_menu(const QPoint& pos) {
  Menu menu(this);

  const bool has_selection = (row != nullptr && selected_keys.size() > 0);
  QAction* ease = menu.addAction(tr("Easy Ease"), this, [this]() { easy_ease(amber::graph::EaseSide::kBoth); });
  ease->setShortcut(QKeySequence(Qt::Key_F9));
  QAction* ease_in = menu.addAction(tr("Easy Ease In"), this, [this]() { easy_ease(amber::graph::EaseSide::kIn); });
  ease_in->setShortcut(QKeySequence(Qt::SHIFT | Qt::Key_F9));
  QAction* ease_out = menu.addAction(tr("Easy Ease Out"), this, [this]() { easy_ease(amber::graph::EaseSide::kOut); });
  ease_out->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_F9));
  ease->setEnabled(has_selection);
  ease_in->setEnabled(has_selection);
  ease_out->setEnabled(has_selection);

  menu.addSeparator();

  QAction* zoom_to_selection = menu.addAction(tr("Zoom to Selection"));
  zoom_to_selection->setShortcut(QKeySequence(Qt::Key_F));
  if (selected_keys.size() == 0 || row == nullptr) {
    zoom_to_selection->setEnabled(false);
  } else {
    connect(zoom_to_selection, &QAction::triggered, this, &GraphView::set_view_to_selection);
  }

  QAction* zoom_to_all = menu.addAction(tr("Zoom to Show All"));
  zoom_to_all->setShortcut(QKeySequence(Qt::SHIFT | Qt::Key_F));
  if (row == nullptr) {
    zoom_to_all->setEnabled(false);
  } else {
    connect(zoom_to_all, &QAction::triggered, this, &GraphView::set_view_to_all);
  }

  menu.addSeparator();

  QAction* reset_action = menu.addAction(tr("Reset View"));
  if (row == nullptr) {
    reset_action->setEnabled(false);
  } else {
    connect(reset_action, &QAction::triggered, this, &GraphView::reset_view);
  }

  menu.exec(mapToGlobal(pos));
}

void GraphView::reset_view() {
  x_zoom = 1.0;
  y_zoom = 1.0;
  set_scroll_x(0);
  set_scroll_y(0);
  emit zoom_changed(x_zoom, y_zoom);
  update();
}

void GraphView::set_view_to_selection() {
  drop_stale_selection(row, selected_keys, selected_keys_fields);
  if (row != nullptr && selected_keys.size() > 0) {
    long min_time = LONG_MAX;
    long max_time = LONG_MIN;
    double min_dbl = DBL_MAX;
    double max_dbl = std::numeric_limits<double>::lowest();  // not DBL_MIN, the smallest positive double
    for (int i = 0; i < selected_keys.size(); i++) {
      const EffectKeyframe& key = row->Field(selected_keys_fields.at(i))->keyframes.at(selected_keys.at(i));
      min_time = qMin(key.time, min_time);
      max_time = qMax(key.time, max_time);
      min_dbl = qMin(key.data.toDouble(), min_dbl);
      max_dbl = qMax(key.data.toDouble(), max_dbl);
    }

    min_time -= row->GetParentEffect()->parent_clip->clip_in();
    max_time -= row->GetParentEffect()->parent_clip->clip_in();

    set_view_to_rect(min_time, min_dbl, max_time, max_dbl);
  }
}

void GraphView::set_view_to_all() {
  if (row != nullptr) {
    bool can_set = false;

    long min_time = LONG_MAX;
    long max_time = LONG_MIN;
    double min_dbl = DBL_MAX;
    double max_dbl = std::numeric_limits<double>::lowest();  // not DBL_MIN, the smallest positive double
    for (int i = 0; i < row->FieldCount(); i++) {
      if (!field_is_shown(i)) continue;  // hidden curves take no room
      for (const auto& key : row->Field(i)->keyframes) {
        min_time = qMin(key.time, min_time);
        max_time = qMax(key.time, max_time);
        min_dbl = qMin(key.data.toDouble(), min_dbl);
        max_dbl = qMax(key.data.toDouble(), max_dbl);
        can_set = true;
      }
    }
    if (can_set) {
      min_time -= row->GetParentEffect()->parent_clip->clip_in();
      max_time -= row->GetParentEffect()->parent_clip->clip_in();

      set_view_to_rect(min_time, min_dbl, max_time, max_dbl);
    }
  }
}

void GraphView::set_view_to_rect(double x1, double y1, double x2, double y2) {
  const amber::graph::FitRect r = amber::graph::FitRange(x1, x2, y1, y2);
  set_zoom(double(width()) / (r.max_time - r.min_time), double(height()) / (r.max_value - r.min_value));

  set_scroll_x(qRound(r.min_time * x_zoom));
  set_scroll_y(qRound(r.min_value * y_zoom));
  update();
}

// Adaptive grid: value lines at 1/2/5 x 10^n, time lines on whole frames of the sequence, both from the zoom
void GraphView::draw_grid(QPainter& p) {
  const QColor bg = palette().color(QPalette::Window);
  const QColor fg = palette().color(QPalette::WindowText);
  auto mix = [&bg, &fg](double t) {
    return QColor::fromRgbF(bg.redF() + (fg.redF() - bg.redF()) * t, bg.greenF() + (fg.greenF() - bg.greenF()) * t,
                            bg.blueF() + (fg.blueF() - bg.blueF()) * t);
  };
  const QColor line_color = mix(0.12);
  const QColor zero_color = mix(0.3);
  const QColor label_color = mix(0.55);

  // Value lines, labelled along the left
  const double v_step = amber::graph::ValueGridStep(y_zoom);
  const int decimals = amber::graph::StepDecimals(v_step);
  const long v_first = long(std::floor(get_value_y(height()) / v_step));
  const long v_last = long(std::ceil(get_value_y(0) / v_step));
  for (long k = v_first; k <= v_last && v_last - v_first < 1000; k++) {
    const double v = double(k) * v_step;
    const int y = get_screen_y(v);
    p.setPen(k == 0 ? zero_color : line_color);
    p.drawLine(0, y, width(), y);
    if (y < height() - fontMetrics().height() - 2) {  // clear of the time labels along the bottom
      p.setPen(label_color);
      p.drawText(4, y - 3, QString::number(v, 'f', decimals));
    }
  }

  // Time lines on sequence frames, labelled along the bottom in the user's timecode format
  const Sequence* seq = panel_sequence_viewer->seq.get();
  const long t_step = amber::graph::TimeGridStep(x_zoom, seq->frame_rate);
  const long t_first = long(std::floor((double(x_scroll) / x_zoom + visible_in) / t_step));
  const long t_last = long(std::ceil((double(x_scroll + width()) / x_zoom + visible_in) / t_step));
  for (long k = t_first; k <= t_last && t_last - t_first < 1000; k++) {
    const long frame = k * t_step;
    const int x = qRound(double(frame - visible_in) * x_zoom - x_scroll);
    p.setPen(frame == 0 ? zero_color : line_color);
    p.drawLine(x, 0, x, height());
    if (frame >= 0) {
      p.setPen(label_color);
      p.drawText(x + 3, height() - 4, frame_to_timecode(frame, amber::CurrentConfig.timecode_view, seq->frame_rate));
    }
  }
}

// Build a path for the curve segment between two keyframes (used for hit-testing).
static QPainterPath build_segment_path(const EffectKeyframe& last_key, const EffectKeyframe& key, int last_key_x,
                                       int last_key_y, int key_x, int key_y, double x_zoom, double y_zoom) {
  QPainterPath path;
  path.moveTo(last_key_x, last_key_y);
  if (last_key.type == EFFECT_KEYFRAME_BEZIER && key.type == EFFECT_KEYFRAME_BEZIER) {
    path.cubicTo(QPointF(last_key_x + last_key.post_handle_x * x_zoom, last_key_y - last_key.post_handle_y * y_zoom),
                 QPointF(key_x + key.pre_handle_x * x_zoom, key_y - key.pre_handle_y * y_zoom), QPointF(key_x, key_y));
  } else if (key.type != EFFECT_KEYFRAME_BEZIER) {
    // last_key is the Bezier one (key is Linear or Hold)
    path.quadTo(QPointF(last_key_x + last_key.post_handle_x * x_zoom, last_key_y - last_key.post_handle_y * y_zoom),
                QPointF(key_x, key_y));
  } else {
    path.quadTo(QPointF(key_x + key.pre_handle_x * x_zoom, key_y - key.pre_handle_y * y_zoom), QPointF(key_x, key_y));
  }
  return path;
}

// Draw a single curve segment from (last_key_x, last_key_y) to (key_x, key_y).
static void draw_curve_segment(QPainter& p, const EffectKeyframe& last_key, const EffectKeyframe& key, int last_key_x,
                               int last_key_y, int key_x, int key_y, double last_post_handle, double pre_handle,
                               double x_zoom, double y_zoom) {
  if (last_key.type == EFFECT_KEYFRAME_HOLD) {
    p.drawLine(last_key_x, last_key_y, key_x, last_key_y);
    p.drawLine(key_x, last_key_y, key_x, key_y);
  } else if (last_key.type == EFFECT_KEYFRAME_BEZIER || key.type == EFFECT_KEYFRAME_BEZIER) {
    QPainterPath bezier_path;
    bezier_path.moveTo(last_key_x, last_key_y);
    if (last_key.type == EFFECT_KEYFRAME_BEZIER && key.type == EFFECT_KEYFRAME_BEZIER) {
      bezier_path.cubicTo(QPointF(last_key_x + last_post_handle * x_zoom, last_key_y - last_key.post_handle_y * y_zoom),
                          QPointF(key_x + pre_handle * x_zoom, key_y - key.pre_handle_y * y_zoom),
                          QPointF(key_x, key_y));
    } else if (key.type != EFFECT_KEYFRAME_BEZIER) {
      // last_key is the Bezier one (key is Linear or Hold)
      bezier_path.quadTo(QPointF(last_key_x + last_post_handle * x_zoom, last_key_y - last_key.post_handle_y * y_zoom),
                         QPointF(key_x, key_y));
    } else {
      bezier_path.quadTo(QPointF(key_x + pre_handle * x_zoom, key_y - key.pre_handle_y * y_zoom),
                         QPointF(key_x, key_y));
    }
    p.drawPath(bezier_path);
  } else {
    p.drawLine(last_key_x, last_key_y, key_x, key_y);
  }
}

QVector<int> sort_keys_from_field(EffectField* field) {
  QVector<int> sorted_keys;
  if (!field) {
    qWarning() << "sort_keys_from_field: field is null";
    return sorted_keys;
  }
  for (int k = 0; k < field->keyframes.size(); k++) {
    bool inserted = false;
    for (int j = 0; j < sorted_keys.size(); j++) {
      if (field->keyframes.at(sorted_keys.at(j)).time > field->keyframes.at(k).time) {
        sorted_keys.insert(j, k);
        inserted = true;
        break;
      }
    }
    if (!inserted) {
      sorted_keys.append(k);
    }
  }
  return sorted_keys;
}

void GraphView::paint_field_curves(QPainter& p, int field_index) {
  EffectField* field = row->Field(field_index);
  if (field->type() != EffectField::EFFECT_FIELD_DOUBLE || !field_visibility.at(field_index)) return;

  QVector<int> sorted_keys = sort_keys_from_field(field);
  QPen line_pen;
  line_pen.setWidth(kBezierLineSize);
  line_pen.setColor(get_curve_color(field_index, row->FieldCount()));

  int last_key_x = 0, last_key_y = 0;
  for (int j = 0; j < sorted_keys.size(); j++) {
    const EffectKeyframe& key = field->keyframes.at(sorted_keys.at(j));
    int key_x = get_screen_x(key.time);
    int key_y = get_screen_y(key.data.toDouble());
    p.setPen(line_pen);
    if (j == 0) {
      p.drawLine(0, key_y, key_x, key_y);
    } else {
      const EffectKeyframe& last_key = field->keyframes.at(sorted_keys.at(j - 1));
      double pre_handle = field->GetValidKeyframeHandlePosition(sorted_keys.at(j), false);
      double last_post_handle = field->GetValidKeyframeHandlePosition(sorted_keys.at(j - 1), true);
      draw_curve_segment(p, last_key, key, last_key_x, last_key_y, key_x, key_y, last_post_handle, pre_handle, x_zoom,
                         y_zoom);
    }
    last_key_x = key_x;
    last_key_y = key_y;
  }
  if (last_key_x < width()) p.drawLine(last_key_x, last_key_y, width(), last_key_y);

  for (int sorted_key : sorted_keys) {
    const EffectKeyframe& key = field->keyframes.at(sorted_key);
    int key_x = get_screen_x(key.time);
    int key_y = get_screen_y(key.data.toDouble());
    if (keyframe_shows_handles(key.type)) {
      p.setPen(Qt::gray);
      QPointF pre_point(key_x + key.pre_handle_x * x_zoom, key_y - key.pre_handle_y * y_zoom);
      p.drawLine(pre_point, QPointF(key_x, key_y));
      p.drawEllipse(pre_point, kBezierHandleSize, kBezierHandleSize);
      QPointF post_point(key_x + key.post_handle_x * x_zoom, key_y - key.post_handle_y * y_zoom);
      p.drawLine(post_point, QPointF(key_x, key_y));
      p.drawEllipse(post_point, kBezierHandleSize, kBezierHandleSize);
    }
    bool selected = false;
    for (int k = 0; k < selected_keys.size(); k++) {
      if (selected_keys.at(k) == sorted_key && selected_keys_fields.at(k) == field_index) {
        selected = true;
        break;
      }
    }
    draw_keyframe(p, key.type, key_x, key_y, selected);
  }
}

void GraphView::paintEvent(QPaintEvent*) {
  QPainter p(this);

  if (panel_sequence_viewer->seq != nullptr) {
    draw_grid(p);

    if (row != nullptr) {
      for (int i = row->FieldCount() - 1; i >= 0; i--) {
        paint_field_curves(p, i);
      }

      // a key snapped to another curve's key: white line there (snapped to the playhead: the playhead turns white)
      const bool dragging_snapped = (snapped && mousedown && moved_keys);
      if (dragging_snapped && !snap_is_playhead) {
        p.setPen(Qt::white);
        p.drawLine(get_screen_x(snap_time), 0, get_screen_x(snap_time), height());
      }

      // draw playhead (only meaningful when a row is selected; visible_in is row-dependent)
      p.setPen((dragging_snapped && snap_is_playhead) ? Qt::white : Qt::red);
      int playhead_x = qRound((double(panel_sequence_viewer->seq->playhead - visible_in) * x_zoom) - x_scroll);
      p.drawLine(playhead_x, 0, playhead_x, height());
    }

    if (rect_select) {
      draw_selection_rectangle(p, QRect(rect_select_x, rect_select_y, rect_select_w, rect_select_h));
      p.setBrush(Qt::NoBrush);
    }
  }

  p.setPen(Qt::white);
  QRect border = rect();
  border.setWidth(border.width() - 1);
  border.setHeight(border.height() - 1);
  p.drawRect(border);
}

static bool point_near(const QPoint& pos, double cx, double cy, int radius) {
  return pos.x() > cx - radius && pos.x() < cx + radius && pos.y() > cy - radius && pos.y() < cy + radius;
}

// Scan all visible double fields for a keyframe body or bezier handle under pos.
// Sets sel_key/sel_key_field; updates current_handle + handle state when a handle is hit.
// Returns true if anything was found.
bool GraphView::press_find_key(const QPoint& pos, int& sel_key, int& sel_key_field) {
  for (int i = 0; i < row->FieldCount(); i++) {
    EffectField* field = row->Field(i);
    if (field->type() != EffectField::EFFECT_FIELD_DOUBLE || !field_visibility.at(i)) continue;

    for (int j = 0; j < field->keyframes.size(); j++) {
      const EffectKeyframe& key = field->keyframes.at(j);
      int key_x = get_screen_x(key.time);
      int key_y = get_screen_y(key.data.toDouble());

      if (point_near(pos, key_x, key_y, KEYFRAME_SIZE)) {
        sel_key = j;
        sel_key_field = i;
        return true;
      }

      if (!keyframe_shows_handles(key.type)) continue;

      QPointF pre_point(key_x + key.pre_handle_x * x_zoom, key_y - key.pre_handle_y * y_zoom);
      QPointF post_point(key_x + key.post_handle_x * x_zoom, key_y - key.post_handle_y * y_zoom);

      if (point_near(pos, pre_point.x(), pre_point.y(), kBezierHandleSize))
        current_handle = kBezierHandlePre;
      else if (point_near(pos, post_point.x(), post_point.y(), kBezierHandleSize))
        current_handle = kBezierHandlePost;

      if (current_handle != kBezierHandleNone) {
        sel_key = j;
        sel_key_field = i;
        handle_index = j;
        handle_field = i;
        old_pre_handle_x = key.pre_handle_x;
        old_pre_handle_y = key.pre_handle_y;
        old_post_handle_x = key.post_handle_x;
        old_post_handle_y = key.post_handle_y;
        return true;
      }
    }
  }
  return false;
}

// Apply selection state after a press: deselect on shift, append, or start rect-select.
void GraphView::press_apply_selection(int sel_key, int sel_key_field, QMouseEvent* event) {
  bool already_selected = false;
  for (int i = 0; i < selected_keys.size(); i++) {
    if (selected_keys.at(i) == sel_key && selected_keys_fields.at(i) == sel_key_field) {
      if ((event->modifiers() & Qt::ShiftModifier) && current_handle == kBezierHandleNone) {
        selected_keys.removeAt(i);
        selected_keys_fields.removeAt(i);
      }
      already_selected = true;
      break;
    }
  }
  if (already_selected) return;

  if (!(event->modifiers() & Qt::ShiftModifier)) {
    selected_keys.clear();
    selected_keys_fields.clear();
  }
  if (sel_key > -1) {
    selected_keys.append(sel_key);
    selected_keys_fields.append(sel_key_field);
  } else {
    rect_select = true;
    rect_select_x = event->position().toPoint().x();
    rect_select_y = event->position().toPoint().y();
    rect_select_w = 0;
    rect_select_h = 0;
    rect_select_offset = selected_keys.size();
  }
}

void GraphView::mousePressEvent(QMouseEvent* event) {
  if (row == nullptr) return;

  mousedown = true;
  last_frame_diff = 0;
  start_x = event->position().toPoint().x();
  start_y = event->position().toPoint().y();

  int sel_key = -1;
  int sel_key_field = -1;
  current_handle = kBezierHandleNone;

  if (click_add && (event->buttons() & Qt::LeftButton)) {
    selected_keys.clear();
    selected_keys_fields.clear();

    EffectKeyframe key;
    key.time = get_value_x(event->position().toPoint().x());
    key.data = clamp_to_field(click_add_field, get_value_y(event->position().toPoint().y()));
    key.type = click_add_type;
    click_add_key = click_add_field->keyframes.size();
    click_add_field->keyframes.append(key);
    update_ui(false);
    click_add_proc = true;
    return;
  }

  press_find_key(event->position().toPoint(), sel_key, sel_key_field);
  press_apply_selection(sel_key, sel_key_field, event);
  selection_update();
}

void GraphView::handle_mouse_pan(QMouseEvent* event) {
  if (!event) {
    qWarning() << "handle_mouse_pan: event is null";
    return;
  }
  set_scroll_x(x_scroll + start_x - event->position().toPoint().x());
  set_scroll_y(y_scroll + event->position().toPoint().y() - start_y);
  start_x = event->position().toPoint().x();
  start_y = event->position().toPoint().y();
  update();
}

void GraphView::handle_mouse_click_add_drag(QMouseEvent* event) {
  if (!event) {
    qWarning() << "handle_mouse_click_add_drag: event is null";
    return;
  }
  click_add_field->keyframes[click_add_key].time = get_value_x(event->position().toPoint().x());
  click_add_field->keyframes[click_add_key].data =
      clamp_to_field(click_add_field, get_value_y(event->position().toPoint().y()));
  update_ui(false);
}

void GraphView::handle_mouse_rect_select(QMouseEvent* event) {
  if (!event) {
    qWarning() << "handle_mouse_rect_select: event is null";
    return;
  }
  rect_select_w = event->position().toPoint().x() - rect_select_x;
  rect_select_h = event->position().toPoint().y() - rect_select_y;

  selected_keys.resize(rect_select_offset);
  selected_keys_fields.resize(rect_select_offset);

  for (int i = 0; i < row->FieldCount(); i++) {
    EffectField* f = row->Field(i);
    if (!field_is_shown(i)) continue;  // a hidden curve's keys are not drawn, so not selectable
    for (int j = 0; j < f->keyframes.size(); j++) {
      bool already_selected = false;
      for (int k = 0; k < selected_keys.size(); k++) {
        if (selected_keys.at(k) == j && selected_keys_fields.at(k) == i) {
          already_selected = true;
          break;
        }
      }

      if (!already_selected) {
        QPoint key_screen_point(get_screen_x(f->keyframes.at(j).time),
                                get_screen_y(f->keyframes.at(j).data.toDouble()));
        QRect select_rect(rect_select_x, rect_select_y, rect_select_w, rect_select_h);
        if (select_rect.contains(key_screen_point)) {
          selected_keys.append(j);
          selected_keys_fields.append(i);
        }
      }
    }
  }
  update();
}

void GraphView::handle_mouse_keyframe_drag(QMouseEvent* event) {
  if (!event) {
    qWarning() << "handle_mouse_keyframe_drag: event is null";
    return;
  }
  // Times of the keys that are not moving, per moving key's field
  QVector<QVector<long>> fixed_times(selected_keys.size());
  for (int i = 0; i < selected_keys.size(); i++) {
    const EffectField* field = row->Field(selected_keys_fields.at(i));
    for (int j = 0; j < field->keyframes.size(); j++) {
      bool moving = false;
      for (int k = 0; k < selected_keys.size() && !moving; k++) {
        moving = (selected_keys.at(k) == j && selected_keys_fields.at(k) == selected_keys_fields.at(i));
      }
      if (!moving) fixed_times[i].append(field->keyframes.at(j).time);
    }
  }
  long frame_diff = qRound(double(event->position().toPoint().x() - start_x) / x_zoom);

  // Snapping (timeline toggle): to the playhead and to the other curves' keys. Not to the keys of the key's own field:
  // landing on one is a collision, refused below.
  snapped = false;
  if (panel_timeline->snapping && panel_sequence_viewer->seq != nullptr) {
    Clip* c = row->GetParentEffect()->parent_clip;
    const long playhead = panel_sequence_viewer->seq->playhead - visible_in + c->clip_in();
    QVector<QVector<long>> targets(selected_keys.size());
    for (int i = 0; i < selected_keys.size(); i++) {
      targets[i].append(playhead);
      for (int f = 0; f < row->FieldCount(); f++) {
        if (f == selected_keys_fields.at(i) || !field_is_shown(f)) continue;
        for (int j = 0; j < row->Field(f)->keyframes.size(); j++) {
          bool moving = false;
          for (int k = 0; k < selected_keys.size() && !moving; k++) {
            moving = (selected_keys.at(k) == j && selected_keys_fields.at(k) == f);
          }
          if (!moving) targets[i].append(row->Field(f)->keyframes.at(j).time);
        }
      }
    }
    snapped = amber::graph::SnapKeyDrag(frame_diff, selected_keys_old_vals, targets,
                                        amber::graph::kSnapRangePx / x_zoom, &snap_time);
    snap_is_playhead = (snapped && snap_time == playhead);
  }

  const long wanted_diff = frame_diff;
  frame_diff = amber::graph::AvoidKeyCollisions(frame_diff, last_frame_diff, selected_keys_old_vals, fixed_times);
  if (frame_diff != wanted_diff) snapped = false;
  last_frame_diff = frame_diff;

  for (int i = 0; i < selected_keys.size(); i++) {
    EffectField* field = row->Field(selected_keys_fields.at(i));
    field->keyframes[selected_keys.at(i)].time = selected_keys_old_vals.at(i) + frame_diff;
    if (event->modifiers() & Qt::ShiftModifier) {
      field->keyframes[selected_keys.at(i)].data = selected_keys_old_doubles.at(i);
    } else {
      // Unrounded: a 0-1 field (opacity) must not jump between whole numbers
      field->keyframes[selected_keys.at(i)].data = clamp_to_field(
          field, selected_keys_old_doubles.at(i) + (double(start_y - event->position().toPoint().y()) / y_zoom));
    }
  }
  moved_keys = true;
  update_ui(false);
}

void GraphView::handle_mouse_bezier_handle_drag(QMouseEvent* event, double x_diff, double y_diff) {
  if (!event) {
    qWarning() << "handle_mouse_bezier_handle_drag: event is null";
    return;
  }
  double new_pre_handle_x = old_pre_handle_x;
  double new_pre_handle_y = old_pre_handle_y;
  double new_post_handle_x = old_post_handle_x;
  double new_post_handle_y = old_post_handle_y;

  if (current_handle == kBezierHandlePre) {
    new_pre_handle_x += x_diff;
    if (!(event->modifiers() & Qt::ShiftModifier)) new_pre_handle_y += y_diff;
    if (!(event->modifiers() & Qt::ControlModifier)) {
      new_post_handle_x = -new_pre_handle_x;
      new_post_handle_y = -new_pre_handle_y;
    }
  } else {
    new_post_handle_x += x_diff;
    if (!(event->modifiers() & Qt::ShiftModifier)) new_post_handle_y += y_diff;
    if (!(event->modifiers() & Qt::ControlModifier)) {
      new_pre_handle_x = -new_post_handle_x;
      new_pre_handle_y = -new_post_handle_y;
    }
  }

  EffectKeyframe& key = row->Field(handle_field)->keyframes[handle_index];
  key.pre_handle_x = qMin(0.0, new_pre_handle_x);
  key.pre_handle_y = new_pre_handle_y;
  key.post_handle_x = qMax(0.0, new_post_handle_x);
  key.post_handle_y = new_post_handle_y;

  moved_keys = true;
  update_ui(false);
}

bool GraphView::is_hovering_keyframe(const QPoint& pos) const {
  for (int i = 0; i < row->FieldCount(); i++) {
    if (!field_is_shown(i)) continue;
    for (const auto& key : row->Field(i)->keyframes) {
      int key_x = get_screen_x(key.time);
      int key_y = get_screen_y(key.data.toDouble());
      QRect test_rect(key_x - KEYFRAME_SIZE, key_y - KEYFRAME_SIZE, KEYFRAME_SIZE + KEYFRAME_SIZE,
                      KEYFRAME_SIZE + KEYFRAME_SIZE);
      if (test_rect.contains(pos)) return true;
      if (!keyframe_shows_handles(key.type)) continue;
      QRect pre_rect(qRound(key_x + key.pre_handle_x * x_zoom - kBezierHandleSize),
                     qRound(key_y - key.pre_handle_y * y_zoom - kBezierHandleSize),
                     kBezierHandleSize + kBezierHandleSize, kBezierHandleSize + kBezierHandleSize);
      QRect post_rect(qRound(key_x + key.post_handle_x * x_zoom - kBezierHandleSize),
                      qRound(key_y - key.post_handle_y * y_zoom - kBezierHandleSize),
                      kBezierHandleSize + kBezierHandleSize, kBezierHandleSize + kBezierHandleSize);

      if (pre_rect.contains(pos) || post_rect.contains(pos)) {
        return true;
      }
    }
  }
  return false;
}

// Returns true if pos is on the curve between last_key and key.
static bool hit_test_segment(const EffectKeyframe& last_key, const EffectKeyframe& key, int last_key_x, int last_key_y,
                             int key_x, int key_y, const QPoint& pos, double x_zoom, double y_zoom) {
  QRect mouse_rect(pos.x() - kBezierLineSize, pos.y() - kBezierLineSize, kBezierLineSize + kBezierLineSize,
                   kBezierLineSize + kBezierLineSize);
  if (last_key.type == EFFECT_KEYFRAME_HOLD) {
    return (pos.y() >= last_key_y - kBezierLineSize && pos.y() <= last_key_y + kBezierLineSize);
  }
  if (last_key.type == EFFECT_KEYFRAME_BEZIER || key.type == EFFECT_KEYFRAME_BEZIER) {
    QPainterPath seg = build_segment_path(last_key, key, last_key_x, last_key_y, key_x, key_y, x_zoom, y_zoom);
    return seg.intersects(mouse_rect);
  }
  QPainterPath linear_path;
  linear_path.moveTo(last_key_x, last_key_y);
  linear_path.lineTo(key_x, key_y);
  return linear_path.intersects(mouse_rect);
}

bool GraphView::test_click_add_on_field(QMouseEvent* event, EffectField* field, int field_index) {
  if (!event) {
    qWarning() << "test_click_add_on_field: event is null";
    return false;
  }
  if (!field) {
    qWarning() << "test_click_add_on_field: field is null";
    return false;
  }
  QVector<int> sorted_keys = sort_keys_from_field(field);
  if (sorted_keys.isEmpty()) return false;

  QPoint pos = event->position().toPoint();

  // Before first keyframe
  if (pos.x() <= get_screen_x(field->keyframes.at(sorted_keys.first()).time)) {
    int y_comp = get_screen_y(field->keyframes.at(sorted_keys.first()).data.toDouble());
    if (pos.y() >= y_comp - kBezierLineSize && pos.y() <= y_comp + kBezierLineSize) {
      click_add_type = field->keyframes.at(sorted_keys.first()).type;
      return true;
    }
    return false;
  }

  // After last keyframe
  if (pos.x() >= get_screen_x(field->keyframes.at(sorted_keys.last()).time)) {
    int y_comp = get_screen_y(field->keyframes.at(sorted_keys.last()).data.toDouble());
    if (pos.y() >= y_comp - kBezierLineSize && pos.y() <= y_comp + kBezierLineSize) {
      click_add_type = field->keyframes.at(sorted_keys.last()).type;
      return true;
    }
    return false;
  }

  // Between keyframes
  for (int j = 1; j < sorted_keys.size(); j++) {
    const EffectKeyframe& last_key = field->keyframes.at(sorted_keys.at(j - 1));
    const EffectKeyframe& key = field->keyframes.at(sorted_keys.at(j));
    int last_key_x = get_screen_x(last_key.time);
    int key_x = get_screen_x(key.time);
    if (pos.x() < last_key_x || pos.x() > key_x) continue;
    click_add_type = last_key.type;
    int last_key_y = get_screen_y(last_key.data.toDouble());
    int key_y = get_screen_y(key.data.toDouble());
    if (hit_test_segment(last_key, key, last_key_x, last_key_y, key_x, key_y, pos, x_zoom, y_zoom)) return true;
  }
  return false;
}

void GraphView::handle_mouse_hover(QMouseEvent* event) {
  if (!event) {
    qWarning() << "handle_mouse_hover: event is null";
    return;
  }
  click_add = false;

  if (!is_hovering_keyframe(event->position().toPoint())) {
    for (int i = 0; i < row->FieldCount(); i++) {
      EffectField* f = row->Field(i);
      if (field_visibility.at(i) && test_click_add_on_field(event, f, i)) {
        click_add = true;
        click_add_field = f;
        setCursor(Qt::CrossCursor);
        break;
      }
    }
  }
}

void GraphView::mouseMoveEvent(QMouseEvent* event) {
  if (!mousedown || !click_add) unsetCursor();
  if (mousedown) {
    if (event->buttons() & Qt::MiddleButton || panel_timeline->tool == TIMELINE_TOOL_HAND) {
      handle_mouse_pan(event);
    } else if (click_add_proc) {
      handle_mouse_click_add_drag(event);
    } else if (rect_select) {
      handle_mouse_rect_select(event);
    } else {
      switch (current_handle) {
        case kBezierHandleNone:
          handle_mouse_keyframe_drag(event);
          break;
        case kBezierHandlePre:
        case kBezierHandlePost: {
          double x_diff = double(event->position().toPoint().x() - start_x) / x_zoom;
          double y_diff = double(start_y - event->position().toPoint().y()) / y_zoom;
          handle_mouse_bezier_handle_drag(event, x_diff, y_diff);
        } break;
      }
    }
  } else if (row != nullptr) {
    handle_mouse_hover(event);
  }
}

void GraphView::mouseReleaseEvent(QMouseEvent*) {
  if (click_add_proc) {
    auto* cmd = new KeyframeAdd(click_add_field, click_add_key);
    cmd->setText(tr("Add Keyframe"));
    amber::UndoStack.push(cmd);
  } else if (moved_keys && selected_keys.size() > 0) {
    ComboAction* ca = new ComboAction(tr("Move Keyframe(s)"));
    switch (current_handle) {
      case kBezierHandleNone:
        for (int i = 0; i < selected_keys.size(); i++) {
          EffectField* f = row->Field(selected_keys_fields.at(i));
          int k = selected_keys.at(i);
          const EffectKeyframe& key = f->keyframes.at(k);
          ca->append(new KeyframeMemberChange(f, k, KeyframeMemberChange::kTime,
                                              qlonglong(selected_keys_old_vals.at(i)), qlonglong(key.time)));
          ca->append(
              new KeyframeMemberChange(f, k, KeyframeMemberChange::kData, selected_keys_old_doubles.at(i), key.data));
        }
        break;
      case kBezierHandlePre:
      case kBezierHandlePost: {
        EffectField* f = row->Field(handle_field);
        const EffectKeyframe& key = f->keyframes.at(handle_index);
        ca->append(new KeyframeMemberChange(f, handle_index, KeyframeMemberChange::kPreHandleX, old_pre_handle_x,
                                            key.pre_handle_x));
        ca->append(new KeyframeMemberChange(f, handle_index, KeyframeMemberChange::kPreHandleY, old_pre_handle_y,
                                            key.pre_handle_y));
        ca->append(new KeyframeMemberChange(f, handle_index, KeyframeMemberChange::kPostHandleX, old_post_handle_x,
                                            key.post_handle_x));
        ca->append(new KeyframeMemberChange(f, handle_index, KeyframeMemberChange::kPostHandleY, old_post_handle_y,
                                            key.post_handle_y));
      } break;
    }
    amber::UndoStack.push(ca);
  }
  moved_keys = false;
  mousedown = false;
  snapped = false;
  click_add = false;
  click_add_proc = false;
  if (rect_select) {
    rect_select = false;
    selection_update();
    update();
  }
}

static void wheel_prepare_deltas(bool zooming, bool shift, bool alt, int& delta_h, int& delta_v) {
  if (zooming) {
    delta_h = delta_h + delta_v;
    delta_v = delta_h;
    if (alt && !shift) delta_h = 0;
    if (shift && !alt) delta_v = 0;
  } else if (shift) {
    std::swap(delta_h, delta_v);
  }
}

void GraphView::wheelEvent(QWheelEvent* event) {
  bool shift = (event->modifiers() & Qt::ShiftModifier);
  bool ctrl = (event->modifiers() & Qt::ControlModifier);
  bool alt = (event->modifiers() & Qt::AltModifier);
  bool zooming = (ctrl != amber::CurrentConfig.scroll_zooms);

  int delta_h = event->angleDelta().x();
  int delta_v = event->angleDelta().y();
  wheel_prepare_deltas(zooming, shift, alt, delta_h, delta_v);

  double new_x_zoom = x_zoom;
  double new_y_zoom = y_zoom;
  bool redraw = false;

  if (!zooming) {
    set_scroll_x(x_scroll - (delta_h / 10));
    set_scroll_y(y_scroll + (delta_v / 10));
    redraw = true;
  }

  if (zooming && delta_v != 0) {
    double zoom_diff = kGraphZoomSpeed * y_zoom;
    new_y_zoom = y_zoom + (zoom_diff * (delta_v / 120.0));
    int true_mouse_y = height() - event->position().y();
    set_scroll_y(qRound((double(y_scroll + true_mouse_y) / y_zoom) * new_y_zoom) - true_mouse_y);
    redraw = true;
  }

  if (zooming && delta_h != 0) {
    double zoom_diff = kGraphZoomSpeed * x_zoom;
    new_x_zoom = x_zoom + (zoom_diff * (delta_h / 120.0));
    set_scroll_x(qRound((double(x_scroll + event->position().x()) / x_zoom) * new_x_zoom) - event->position().x());
    redraw = true;
  }

  if (zooming) set_zoom(new_x_zoom, new_y_zoom);
  if (redraw) update();
}

void GraphView::set_row(EffectRow* r) {
  if (row != r) {
    selected_keys.clear();
    selected_keys_fields.clear();
    selected_keys_old_vals.clear();
    selected_keys_old_doubles.clear();
    emit selection_changed(false, -1);
    row = r;
    if (row != nullptr) {
      field_visibility.resize(row->FieldCount());
      for (int i = 0; i < row->FieldCount(); i++) {
        field_visibility[i] = row->Field(i)->IsEnabled();
      }
      visible_in = row->GetParentEffect()->parent_clip->timeline_in();
      set_view_to_all();
    } else {
      visible_in = 0;
      update();
    }
  }
}

void GraphView::set_visible_in(long i) { visible_in = i; }

void GraphView::set_selected_keyframe_type(int type) {
  drop_stale_selection(row, selected_keys, selected_keys_fields);
  if (selected_keys.size() > 0) {
    ComboAction* ca = new ComboAction(tr("Change Keyframe Type"));
    for (int i = 0; i < selected_keys.size(); i++) {
      EffectField* f = row->Field(selected_keys_fields.at(i));
      int k = selected_keys.at(i);
      ca->append(new KeyframeMemberChange(f, k, KeyframeMemberChange::kType, f->keyframes.at(k).type, type));
    }
    amber::UndoStack.push(ca);
    update_ui(false);
  }
}

bool GraphView::field_is_shown(int field) const {
  return row->Field(field)->type() == EffectField::EFFECT_FIELD_DOUBLE && field_visibility.at(field);
}

void GraphView::set_field_visibility(int field, bool b) {
  field_visibility[field] = b;
  update();
}

void GraphView::delete_selected_keys() {
  if (row != nullptr) {
    drop_stale_selection(row, selected_keys, selected_keys_fields);
    QVector<EffectField*> fields;
    for (int selected_keys_field : selected_keys_fields) {
      fields.append(row->Field(selected_keys_field));
    }
    delete_keyframes(fields, selected_keys);
    // delete_keyframes() cleared selected_keys and our local copy, not the field indices
    if (selected_keys.isEmpty()) selected_keys_fields.clear();
  }
}

void GraphView::select_all() {
  if (row != nullptr) {
    selected_keys.clear();
    selected_keys_fields.clear();
    for (int i = 0; i < row->FieldCount(); i++) {
      EffectField* field = row->Field(i);
      if (!field_is_shown(i)) continue;  // Select All then Delete must not remove a hidden curve's keys
      for (int j = 0; j < field->keyframes.size(); j++) {
        selected_keys.append(j);
        selected_keys_fields.append(i);
      }
    }
    selection_update();
  }
}

int GraphView::selected_key_info(EffectField** field, long* sequence_time, double* value) {
  if (row == nullptr) return 0;
  drop_stale_selection(row, selected_keys, selected_keys_fields);
  if (selected_keys.isEmpty()) return 0;
  EffectField* f = row->Field(selected_keys_fields.at(0));
  const EffectKeyframe& key = f->keyframes.at(selected_keys.at(0));
  Clip* c = row->GetParentEffect()->parent_clip;
  if (field) *field = f;
  if (sequence_time) *sequence_time = key.time - c->clip_in() + c->timeline_in();
  if (value) *value = key.data.toDouble();
  return selected_keys.size();
}

// Snapshot of the selected keys when a Value/Time entry starts: the undo step's old values
void GraphView::begin_entry() {
  if (entry_active) return;
  entry_old_times.clear();
  entry_old_values.clear();
  for (int i = 0; i < selected_keys.size(); i++) {
    const EffectKeyframe& key = row->Field(selected_keys_fields.at(i))->keyframes.at(selected_keys.at(i));
    entry_old_times.append(key.time);
    entry_old_values.append(key.data);
  }
  entry_active = true;
}

void GraphView::set_selected_value(double value, bool final) {
  if (row == nullptr) return;
  if (!entry_active) drop_stale_selection(row, selected_keys, selected_keys_fields);
  if (selected_keys.isEmpty()) return;
  begin_entry();

  QVector<EffectField*> fields;
  for (int i = 0; i < selected_keys.size(); i++) {
    EffectField* f = row->Field(selected_keys_fields.at(i));
    fields.append(f);
    f->keyframes[selected_keys.at(i)].data = clamp_to_field(f, value);
  }
  if (final) {
    ComboAction* ca = keyframe_value_entry(fields, selected_keys, entry_old_values, value);
    if (ca != nullptr) amber::UndoStack.push(ca);
    entry_active = false;
    selection_update();
  }
  update_ui(false);
}

void GraphView::set_selected_time(long sequence_time, bool final) {
  if (row == nullptr) return;
  if (!entry_active) drop_stale_selection(row, selected_keys, selected_keys_fields);
  if (selected_keys.size() != 1) return;
  begin_entry();

  EffectField* f = row->Field(selected_keys_fields.at(0));
  const int k = selected_keys.at(0);
  Clip* c = row->GetParentEffect()->parent_clip;
  const long old_time = entry_old_times.at(0);
  QVector<long> fixed;
  for (int j = 0; j < f->keyframes.size(); j++) {
    if (j != k) fixed.append(f->keyframes.at(j).time);
  }
  // A time already taken by another key of the field steps back toward the key's old time
  const long diff = amber::graph::AvoidKeyCollisions(sequence_time - c->timeline_in() + c->clip_in() - old_time, 0,
                                                     {old_time}, {fixed});
  f->keyframes[k].time = old_time + diff;
  if (final) {
    if (diff != 0) {
      auto* cmd =
          new KeyframeMemberChange(f, k, KeyframeMemberChange::kTime, qlonglong(old_time), qlonglong(old_time + diff));
      cmd->setText(tr("Move Keyframe(s)"));
      amber::UndoStack.push(cmd);
    }
    entry_active = false;
    selection_update();
  }
  update_ui(false);
}

void GraphView::easy_ease(amber::graph::EaseSide side) {
  if (row == nullptr) return;
  drop_stale_selection(row, selected_keys, selected_keys_fields);
  QVector<EffectField*> fields;
  for (int f : selected_keys_fields) fields.append(row->Field(f));
  ComboAction* ca = keyframe_easy_ease(fields, selected_keys, side);
  if (ca == nullptr) return;
  amber::UndoStack.push(ca);
  selection_update();  // the keys are Bezier now: the type buttons follow
  update_ui(false);
}

void GraphView::set_scroll_x(int s) {
  x_scroll = s;
  emit x_scroll_changed(x_scroll);
}

void GraphView::set_scroll_y(int s) {
  y_scroll = s;
  emit y_scroll_changed(y_scroll);
}

void GraphView::set_zoom(double xz, double yz) {
  x_zoom = qMax(kGraphMinZoom, xz);
  y_zoom = qMax(kGraphMinZoom, yz);
  emit zoom_changed(x_zoom, y_zoom);
}

int GraphView::get_screen_x(double d) const {
  if (row != nullptr) {
    d -= row->GetParentEffect()->parent_clip->clip_in();
  }
  return qRound((d * x_zoom) - x_scroll);
}

int GraphView::get_screen_y(double d) const { return qRound(height() + y_scroll - d * y_zoom); }

long GraphView::get_value_x(int i) {
  long frame = qRound((i + x_scroll) / x_zoom);
  if (row != nullptr) {
    frame += row->GetParentEffect()->parent_clip->clip_in();
  }
  return frame;
}

double GraphView::get_value_y(int i) { return double(height() + y_scroll - i) / y_zoom; }

void GraphView::selection_update() {
  drop_stale_selection(row, selected_keys, selected_keys_fields);
  selected_keys_old_vals.clear();
  selected_keys_old_doubles.clear();

  int selected_key_type = -1;

  for (int i = 0; i < selected_keys.size(); i++) {
    const EffectKeyframe& key = row->Field(selected_keys_fields.at(i))->keyframes.at(selected_keys.at(i));
    selected_keys_old_vals.append(key.time);
    selected_keys_old_doubles.append(key.data.toDouble());

    if (selected_key_type == -1) {
      selected_key_type = key.type;
    } else if (selected_key_type != key.type) {
      selected_key_type = -2;
    }
  }

  update();

  emit selection_changed(selected_keys.size() > 0, selected_key_type);
}
