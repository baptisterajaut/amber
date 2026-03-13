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

#include "rulerwidget.h"

#include <QMouseEvent>
#include <QPainter>

#include "panels/viewer.h"
#include "timeline/sequence.h"
#include "global/config.h"
#include "ui/viewercontainer.h"
#include "ui/viewerwidget.h"

RulerWidget::RulerWidget(Guide::Orientation orientation, ViewerContainer* container, Viewer* viewer, QWidget* parent)
    : QWidget(parent), orientation_(orientation), container_(container), viewer_(viewer) {
  if (orientation_ == Guide::Horizontal) {
    setFixedHeight(kRulerThickness);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
  } else {
    setFixedWidth(kRulerThickness);
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
  }
  setMouseTracking(true);
}

void RulerWidget::set_cursor_pos(int video_pos) {
  cursor_video_pos_ = video_pos;
  update();
}

void RulerWidget::clear_cursor_pos() {
  cursor_video_pos_ = -1;
  update();
}

int RulerWidget::thickness() const { return kRulerThickness; }

double RulerWidget::video_to_widget(int video_pos) const {
  if (container_->child == nullptr || viewer_->seq == nullptr) return -1;

  if (orientation_ == Guide::Horizontal) {
    double ppvp = double(container_->child->width()) / double(viewer_->seq->width);
    double container_pos = double(video_pos) * ppvp + container_->child->x();
    return container_pos - x();  // container coords → ruler-local coords
  } else {
    double ppvp = double(container_->child->height()) / double(viewer_->seq->height);
    double container_pos = double(video_pos) * ppvp + container_->child->y();
    return container_pos - y();  // container coords → ruler-local coords
  }
}

int RulerWidget::widget_to_video(double widget_pos) const {
  if (container_->child == nullptr || viewer_->seq == nullptr) return -1;

  if (orientation_ == Guide::Horizontal) {
    double ppvp = double(container_->child->width()) / double(viewer_->seq->width);
    if (ppvp <= 0) return -1;
    double container_pos = widget_pos + x();  // ruler-local → container coords
    return qRound((container_pos - container_->child->x()) / ppvp);
  } else {
    double ppvp = double(container_->child->height()) / double(viewer_->seq->height);
    if (ppvp <= 0) return -1;
    double container_pos = widget_pos + y();  // ruler-local → container coords
    return qRound((container_pos - container_->child->y()) / ppvp);
  }
}

void RulerWidget::paintEvent(QPaintEvent*) {
  QPainter p(this);
  p.fillRect(rect(), QColor(0x3a, 0x3a, 0x3a));

  if (viewer_->seq == nullptr || container_->child == nullptr) return;

  int video_size = (orientation_ == Guide::Horizontal) ? viewer_->seq->width : viewer_->seq->height;
  if (video_size <= 0) return;

  // Compute pixels-per-video-pixel from the child widget geometry
  double ppvp;
  if (orientation_ == Guide::Horizontal) {
    ppvp = double(container_->child->width()) / double(video_size);
  } else {
    ppvp = double(container_->child->height()) / double(video_size);
  }
  if (ppvp <= 0) return;

  // Pick adaptive tick interval: we want major ticks at least ~60 widget-pixels apart
  static const int kCandidates[] = {1, 2, 5, 10, 20, 50, 100, 200, 500, 1000, 2000, 5000};
  int major_interval = kCandidates[11];  // fallback: 5000
  for (int c : kCandidates) {
    if (c * ppvp >= 60.0) {
      major_interval = c;
      break;
    }
  }

  // Minor ticks at 1/5 of major if there's room, tiny at 1/10
  int minor_interval = major_interval / 5;
  if (minor_interval < 1) minor_interval = 1;
  int tiny_interval = major_interval / 10;
  if (tiny_interval < 1) tiny_interval = 1;

  QPen tick_pen(QColor(0xcc, 0xcc, 0xcc));
  p.setPen(tick_pen);

  QFont font = p.font();
  font.setPixelSize(9);
  p.setFont(font);

  // Determine visible range in video pixels
  int vis_start = widget_to_video(0);
  int widget_extent = (orientation_ == Guide::Horizontal) ? width() : height();
  int vis_end = widget_to_video(widget_extent);
  if (vis_start > vis_end) std::swap(vis_start, vis_end);
  vis_start = qMax(0, vis_start - major_interval);
  vis_end = qMin(video_size, vis_end + major_interval);

  // Draw ticks
  // Start from aligned position
  int tick_start = (vis_start / tiny_interval) * tiny_interval;
  for (int vp = tick_start; vp <= vis_end; vp += tiny_interval) {
    double wp = video_to_widget(vp);

    bool is_major = (vp % major_interval == 0);
    bool is_minor = (!is_major && minor_interval > 0 && vp % minor_interval == 0);

    int tick_len;
    if (is_major)
      tick_len = kRulerThickness - 4;
    else if (is_minor)
      tick_len = kRulerThickness / 2;
    else
      tick_len = kRulerThickness / 4;

    if (orientation_ == Guide::Horizontal) {
      int x = qRound(wp);
      p.drawLine(x, kRulerThickness - tick_len, x, kRulerThickness);
      if (is_major) {
        p.drawText(x + 3, 10, QString::number(vp));
      }
    } else {
      int y = qRound(wp);
      p.drawLine(kRulerThickness - tick_len, y, kRulerThickness, y);
      if (is_major) {
        p.save();
        p.translate(10, y + 3);
        p.rotate(90);
        p.drawText(0, 0, QString::number(vp));
        p.restore();
      }
    }
  }

  // Draw cursor tracker triangle
  if (cursor_video_pos_ >= 0) {
    double wp = video_to_widget(cursor_video_pos_);
    p.setPen(Qt::NoPen);
    p.setBrush(Qt::white);

    QPolygon tri;
    if (orientation_ == Guide::Horizontal) {
      int x = qRound(wp);
      tri << QPoint(x - 4, kRulerThickness) << QPoint(x + 4, kRulerThickness) << QPoint(x, kRulerThickness - 6);
    } else {
      int y = qRound(wp);
      tri << QPoint(kRulerThickness, y - 4) << QPoint(kRulerThickness, y + 4) << QPoint(kRulerThickness - 6, y);
    }
    p.drawPolygon(tri);
  }

  // Draw drag indicator
  if (dragging_ && drag_video_pos_ >= 0) {
    double wp = video_to_widget(drag_video_pos_);
    p.setPen(QPen(QColor(0xff, 0xa5, 0x00), 1));  // amber
    if (orientation_ == Guide::Horizontal) {
      int x = qRound(wp);
      p.drawLine(x, 0, x, kRulerThickness);
    } else {
      int y = qRound(wp);
      p.drawLine(0, y, kRulerThickness, y);
    }
  }
}

void RulerWidget::mousePressEvent(QMouseEvent* event) {
  if (event->button() == Qt::LeftButton && viewer_->seq != nullptr && !olive::CurrentConfig.lock_guides) {
    dragging_ = true;
    double pos = (orientation_ == Guide::Horizontal) ? event->position().x() : event->position().y();
    drag_video_pos_ = widget_to_video(pos);
    update();
  }
}

void RulerWidget::mouseMoveEvent(QMouseEvent* event) {
  if (dragging_) {
    double pos = (orientation_ == Guide::Horizontal) ? event->position().x() : event->position().y();
    drag_video_pos_ = widget_to_video(pos);
    update();
  }
}

void RulerWidget::mouseReleaseEvent(QMouseEvent* event) {
  if (dragging_) {
    dragging_ = false;

    if (drag_video_pos_ >= 0) {
      // Perpendicular: top ruler → vertical guide, left ruler → horizontal guide
      Guide::Orientation perpendicular =
          (orientation_ == Guide::Horizontal) ? Guide::Vertical : Guide::Horizontal;
      emit guide_created(perpendicular, drag_video_pos_);
    }

    drag_video_pos_ = -1;
    update();
  }
}

void RulerWidget::leaveEvent(QEvent*) {
  if (!dragging_) {
    clear_cursor_pos();
  }
}
