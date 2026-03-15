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

#include "viewercontainer.h"

#include <QApplication>
#include <QResizeEvent>
#include <QScrollBar>
#include <QVBoxLayout>
#include <QWidget>

#include "global/config.h"
#include "global/debug.h"
#include "panels/viewer.h"
#include "rulerwidget.h"
#include "timeline/sequence.h"
#include "undo/undo_guide.h"
#include "undo/undostack.h"
#include "viewerwidget.h"

// enforces aspect ratio
ViewerContainer::ViewerContainer(QWidget *parent)
    : QWidget(parent)

{
  horizontal_scrollbar = new QScrollBar(Qt::Horizontal, this);
  vertical_scrollbar = new QScrollBar(Qt::Vertical, this);

  horizontal_scrollbar->setVisible(false);
  vertical_scrollbar->setVisible(false);

  horizontal_scrollbar->setSingleStep(20);
  vertical_scrollbar->setSingleStep(20);

  child = new ViewerWidget(this);
  child->container = this;

  // Overlay as sibling of ViewerWidget (not child) — a child QWidget over
  // a QRhiWidget breaks Vulkan compositing in Amber's widget hierarchy.
  child->overlay_ = new ViewerOverlay(child, this);
  child->overlay_->raise();

  connect(horizontal_scrollbar, &QScrollBar::valueChanged, this, &ViewerContainer::scroll_changed);
  connect(vertical_scrollbar, &QScrollBar::valueChanged, this, &ViewerContainer::scroll_changed);

  // Rulers are created later in adjust() when viewer is set
}

ViewerContainer::~ViewerContainer() = default;

void ViewerContainer::dragScrollPress(const QPoint &p) {
  drag_start_x = p.x();
  drag_start_y = p.y();

  horiz_start = horizontal_scrollbar->value();
  vert_start = vertical_scrollbar->value();
}

void ViewerContainer::dragScrollMove(const QPoint &p) {
  int this_x = p.x();
  int this_y = p.y();

  horizontal_scrollbar->setValue(horiz_start + (drag_start_x - this_x));
  vertical_scrollbar->setValue(vert_start + (drag_start_y - this_y));
}

void ViewerContainer::parseWheelEvent(QWheelEvent *event) {
  if (event->modifiers() & Qt::AltModifier) {
    QApplication::sendEvent(horizontal_scrollbar, event);
  } else {
    QApplication::sendEvent(vertical_scrollbar, event);
  }
}

void ViewerContainer::adjust() {
  if (viewer->seq != nullptr) {
    // Lazy-create rulers (viewer is set after construction)
    if (horizontal_ruler == nullptr) {
      horizontal_ruler = new RulerWidget(Guide::Horizontal, this, viewer, this);
      vertical_ruler = new RulerWidget(Guide::Vertical, this, viewer, this);

      connect(horizontal_ruler, &RulerWidget::guide_created, this, [this](Guide::Orientation o, int pos) {
        if (viewer->seq != nullptr) {
          olive::UndoStack.push(new AddGuideAction(viewer->seq.get(), {o, pos}));
          child->update();
        }
      });
      connect(vertical_ruler, &RulerWidget::guide_created, this, [this](Guide::Orientation o, int pos) {
        if (viewer->seq != nullptr) {
          olive::UndoStack.push(new AddGuideAction(viewer->seq.get(), {o, pos}));
          child->update();
        }
      });

      horizontal_ruler->setVisible(olive::CurrentConfig.show_guides);
      vertical_ruler->setVisible(olive::CurrentConfig.show_guides);
    }

    // Sync ruler visibility with config on every adjust
    horizontal_ruler->setVisible(olive::CurrentConfig.show_guides);
    vertical_ruler->setVisible(olive::CurrentConfig.show_guides);

    bool rulers_visible = olive::CurrentConfig.show_guides;
    int ruler_h = rulers_visible ? RulerWidget::kRulerThickness : 0;

    if (child->waveform) {
      child->move(0, 0);
      child->resize(size());
    } else {
      horizontal_scrollbar->setVisible(false);
      vertical_scrollbar->setVisible(false);

      // Available area for the viewer, accounting for rulers
      int avail_x = ruler_h;
      int avail_y = ruler_h;
      int avail_w = width() - ruler_h;
      int avail_h = height() - ruler_h;

      int zoomed_width = qRound(double(viewer->seq->width) * zoom);
      int zoomed_height = qRound(double(viewer->seq->height) * zoom);

      if (fit || zoomed_width > avail_w || zoomed_height > avail_h) {
        double aspect_ratio = double(viewer->seq->width) / double(viewer->seq->height);

        int widget_x = avail_x;
        int widget_y = avail_y;
        int widget_width = avail_w;
        int widget_height = avail_h;

        if (!fit) {
          widget_width -= vertical_scrollbar->sizeHint().width();
          widget_height -= horizontal_scrollbar->sizeHint().height();
        }

        double widget_ar = double(widget_width) / double(widget_height);
        bool widget_is_wider_than_sequence = widget_ar > aspect_ratio;

        if (widget_is_wider_than_sequence) {
          widget_width = widget_height * aspect_ratio;
          widget_x = avail_x + (avail_w / 2) - (widget_width / 2);
        } else {
          widget_height = widget_width / aspect_ratio;
          widget_y = avail_y + (avail_h / 2) - (widget_height / 2);
        }

        child->move(widget_x, widget_y);
        child->resize(widget_width, widget_height);

        if (fit) {
          zoom = double(widget_width) / double(viewer->seq->width);
        } else if (zoomed_width > avail_w || zoomed_height > avail_h) {
          horizontal_scrollbar->setVisible(true);
          vertical_scrollbar->setVisible(true);

          horizontal_scrollbar->setMaximum(zoomed_width - avail_w);
          vertical_scrollbar->setMaximum(zoomed_height - avail_h);

          horizontal_scrollbar->setValue(horizontal_scrollbar->maximum() / 2);
          vertical_scrollbar->setValue(vertical_scrollbar->maximum() / 2);

          adjust_scrollbars();
        }
      } else {
        int zoomed_x = avail_x;
        int zoomed_y = avail_y;

        if (zoomed_width < avail_w) zoomed_x = avail_x + (avail_w >> 1) - (zoomed_width >> 1);
        if (zoomed_height < avail_h) zoomed_y = avail_y + (avail_h >> 1) - (zoomed_height >> 1);

        child->move(zoomed_x, zoomed_y);
        child->resize(zoomed_width, zoomed_height);
      }
    }

    // Position overlay to match viewer widget (overlay is a sibling)
    if (child->overlay_) {
      child->overlay_->setGeometry(child->geometry());
      child->overlay_->raise();
    }

    // Position rulers to align with the viewer area
    if (rulers_visible) {
      horizontal_ruler->setGeometry(ruler_h, 0, width() - ruler_h, ruler_h);
      vertical_ruler->setGeometry(0, ruler_h, ruler_h, height() - ruler_h);
      horizontal_ruler->update();
      vertical_ruler->update();
    }
  }
}

void ViewerContainer::set_rulers_visible(bool visible) {
  if (horizontal_ruler != nullptr) {
    horizontal_ruler->setVisible(visible);
    vertical_ruler->setVisible(visible);
  }
  adjust();
}

void ViewerContainer::adjust_scrollbars() {
  horizontal_scrollbar->move(0, height() - horizontal_scrollbar->height());
  horizontal_scrollbar->setFixedWidth(qMax(0, width() - vertical_scrollbar->width()));
  horizontal_scrollbar->setPageStep(width());

  vertical_scrollbar->move(width() - vertical_scrollbar->width(), 0);
  vertical_scrollbar->setFixedHeight(qMax(0, height() - horizontal_scrollbar->height()));
  vertical_scrollbar->setPageStep(height());
}

void ViewerContainer::resizeEvent(QResizeEvent *event) {
  event->accept();
  adjust();
}

void ViewerContainer::scroll_changed() {
  child->set_scroll(double(horizontal_scrollbar->value()) / double(horizontal_scrollbar->maximum()),
                    double(vertical_scrollbar->value()) / double(vertical_scrollbar->maximum()));
}
