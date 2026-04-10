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

#ifndef KEYFRAMEVIEW_H
#define KEYFRAMEVIEW_H

#include <QPainter>
#include <QWidget>

#include "ui/effectui.h"

class Clip;
class Effect;
class EffectRow;
class EffectField;
class TimelineHeader;

class KeyframeView : public QWidget {
  Q_OBJECT
 public:
  KeyframeView(QWidget* parent = nullptr);

  void SetEffects(const QVector<EffectUI*>& open_effects);

  void delete_selected_keyframes();

  TimelineHeader* header;

  long visible_in{0};
  long visible_out{0};
 signals:
  void wheel_event_signal(QWheelEvent*);
 public slots:
  void set_x_scroll(int);
  void set_y_scroll(int);
  void resize_move(double d);

 private:
  QVector<EffectUI*> open_effects_;

  QVector<EffectField*> selected_fields;
  QVector<int> selected_keyframes;
  QVector<int> rowY;
  QVector<EffectRow*> rows;
  QVector<long> old_key_vals;
  void mousePressEvent(QMouseEvent* event) override;
  void mouseMoveEvent(QMouseEvent* event) override;
  void mouseReleaseEvent(QMouseEvent* event) override;
  void paintEvent(QPaintEvent* event) override;
  void wheelEvent(QWheelEvent* e) override;
  bool mousedown{false};
  bool dragging{false};
  bool keys_selected{false};
  bool select_rect{false};
  bool scroll_drag{false};

  bool keyframeIsSelected(EffectField* field, int keyframe);

  long drag_frame_start;
  long last_frame_diff;
  int rect_select_x;
  int rect_select_y;
  int rect_select_w;
  int rect_select_h;
  int rect_select_offset;

  int x_scroll{0};
  int y_scroll{0};

  void update_keys();
  // Returns keyframe index found (-1 if none); sets row_index and field_index.
  int press_find_key(int mouse_x, int mouse_y, int& row_index, int& field_index);
  // After hit-test: extend selection to same-time keyframes in the row.
  void press_extend_same_time_selection(int row_index, int field_index, int keyframe_index);
  // Rect-select update during drag.
  void move_rect_select(QMouseEvent* event);
  // Keyframe move update during drag.
  void move_keys_drag(QMouseEvent* event);
  // Draw keyframes for a single row.
  void paint_row_keyframes(QPainter& p, EffectRow* row, int field_count, int keyframe_y);
 private slots:
  void show_context_menu(const QPoint& pos);
  void menu_set_key_type(QAction*);
};

#endif  // KEYFRAMEVIEW_H
