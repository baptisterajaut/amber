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

#ifndef RULERWIDGET_H
#define RULERWIDGET_H

#include <QWidget>

#include "core/guide.h"

class Viewer;
class ViewerContainer;

class RulerWidget : public QWidget {
  Q_OBJECT
 public:
  static constexpr int kRulerThickness = 20;

  RulerWidget(Guide::Orientation orientation, ViewerContainer* container, Viewer* viewer, QWidget* parent = nullptr);

  void set_cursor_pos(int video_pos);
  void clear_cursor_pos();
  int thickness() const;

 signals:
  void guide_created(Guide::Orientation orientation, int position);

 protected:
  void paintEvent(QPaintEvent* event) override;
  void mousePressEvent(QMouseEvent* event) override;
  void mouseMoveEvent(QMouseEvent* event) override;
  void mouseReleaseEvent(QMouseEvent* event) override;
  void leaveEvent(QEvent* event) override;

 private:
  double video_to_widget(int video_pos) const;
  int widget_to_video(double widget_pos) const;
  void paint_tick(QPainter& p, double wp, int vp, bool is_major, bool is_minor);
  void paint_cursor_triangle(QPainter& p, double wp);
  void paint_drag_indicator(QPainter& p, double wp);

  Guide::Orientation orientation_;
  ViewerContainer* container_;
  Viewer* viewer_;

  int cursor_video_pos_{-1};
  bool dragging_{false};
  int drag_video_pos_{-1};
};

#endif  // RULERWIDGET_H
