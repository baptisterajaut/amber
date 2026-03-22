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

#include "timeline.h"

#include <QScrollBar>
#include <QtMath>
#include <QHBoxLayout>
#include <QSplitter>
#include <QInputDialog>

#include "global/global.h"
#include "global/config.h"
#include "panels/panels.h"
#include "ui/timelinewidget.h"
#include "ui/icons.h"
#include "ui/timelineheader.h"
#include "ui/resizablescrollbar.h"
#include "ui/audiomonitor.h"
#include "ui/flowlayout.h"
#include "ui/cursors.h"
#include "rendering/renderfunctions.h"

void Timeline::setup_ui() {
  QWidget* dockWidgetContents = new QWidget();

  QHBoxLayout* horizontalLayout = new QHBoxLayout(dockWidgetContents);
  horizontalLayout->setSpacing(0);
  horizontalLayout->setContentsMargins(0, 0, 0, 0);

  setWidget(dockWidgetContents);

  tool_button_widget = new QWidget();
  tool_button_widget->setObjectName("timeline_toolbar");
  tool_button_widget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

  FlowLayout* tool_buttons_layout = new FlowLayout(tool_button_widget);
  tool_buttons_layout->setSpacing(4);
  tool_buttons_layout->setContentsMargins(0, 0, 0, 0);

  toolArrowButton = new QPushButton();
  toolArrowButton->setIcon(amber::icon::CreateIconFromSVG(QStringLiteral(":/icons/arrow.svg")));
  toolArrowButton->setCheckable(true);
  toolArrowButton->setProperty("tool", TIMELINE_TOOL_POINTER);
  connect(toolArrowButton, &QPushButton::clicked, this, qOverload<>(&Timeline::set_tool));
  tool_buttons_layout->addWidget(toolArrowButton);

  toolEditButton = new QPushButton();
  toolEditButton->setIcon(amber::icon::CreateIconFromSVG(QStringLiteral(":/icons/beam.svg")));
  toolEditButton->setCheckable(true);
  toolEditButton->setProperty("tool", TIMELINE_TOOL_EDIT);
  connect(toolEditButton, &QPushButton::clicked, this, qOverload<>(&Timeline::set_tool));
  tool_buttons_layout->addWidget(toolEditButton);

  toolRippleButton = new QPushButton();
  toolRippleButton->setIcon(amber::icon::CreateIconFromSVG(QStringLiteral(":/icons/ripple.svg")));
  toolRippleButton->setCheckable(true);
  toolRippleButton->setProperty("tool", TIMELINE_TOOL_RIPPLE);
  connect(toolRippleButton, &QPushButton::clicked, this, qOverload<>(&Timeline::set_tool));
  tool_buttons_layout->addWidget(toolRippleButton);

  toolRazorButton = new QPushButton();
  toolRazorButton->setIcon(amber::icon::CreateIconFromSVG(QStringLiteral(":/icons/razor.svg")));
  toolRazorButton->setCheckable(true);
  toolRazorButton->setProperty("tool", TIMELINE_TOOL_RAZOR);
  connect(toolRazorButton, &QPushButton::clicked, this, qOverload<>(&Timeline::set_tool));
  tool_buttons_layout->addWidget(toolRazorButton);

  toolSlipButton = new QPushButton();
  toolSlipButton->setIcon(amber::icon::CreateIconFromSVG(QStringLiteral(":/icons/slip.svg")));
  toolSlipButton->setCheckable(true);
  toolSlipButton->setProperty("tool", TIMELINE_TOOL_SLIP);
  connect(toolSlipButton, &QPushButton::clicked, this, qOverload<>(&Timeline::set_tool));
  tool_buttons_layout->addWidget(toolSlipButton);

  toolSlideButton = new QPushButton();
  toolSlideButton->setIcon(amber::icon::CreateIconFromSVG(QStringLiteral(":/icons/slide.svg")));
  toolSlideButton->setCheckable(true);
  toolSlideButton->setProperty("tool", TIMELINE_TOOL_SLIDE);
  connect(toolSlideButton, &QPushButton::clicked, this, qOverload<>(&Timeline::set_tool));
  tool_buttons_layout->addWidget(toolSlideButton);

  toolHandButton = new QPushButton();
  toolHandButton->setIcon(amber::icon::CreateIconFromSVG(QStringLiteral(":/icons/hand.svg")));
  toolHandButton->setCheckable(true);

  toolHandButton->setProperty("tool", TIMELINE_TOOL_HAND);
  connect(toolHandButton, &QPushButton::clicked, this, qOverload<>(&Timeline::set_tool));
  tool_buttons_layout->addWidget(toolHandButton);
  toolTransitionButton = new QPushButton();
  toolTransitionButton->setIcon(amber::icon::CreateIconFromSVG(QStringLiteral(":/icons/transition-tool.svg")));
  toolTransitionButton->setCheckable(true);
  connect(toolTransitionButton, &QPushButton::clicked, this, &Timeline::transition_tool_click);
  tool_buttons_layout->addWidget(toolTransitionButton);

  snappingButton = new QPushButton();
  snappingButton->setIcon(amber::icon::CreateIconFromSVG(QStringLiteral(":/icons/magnet.svg")));
  snappingButton->setCheckable(true);
  snappingButton->setChecked(true);
  connect(snappingButton, &QPushButton::toggled, this, &Timeline::snapping_clicked);
  tool_buttons_layout->addWidget(snappingButton);

  zoomInButton = new QPushButton();
  zoomInButton->setIcon(amber::icon::CreateIconFromSVG(QStringLiteral(":/icons/zoomin.svg")));
  connect(zoomInButton, &QPushButton::clicked, this, &Timeline::zoom_in);
  tool_buttons_layout->addWidget(zoomInButton);

  zoomOutButton = new QPushButton();
  zoomOutButton->setIcon(amber::icon::CreateIconFromSVG(QStringLiteral(":/icons/zoomout.svg")));
  connect(zoomOutButton, &QPushButton::clicked, this, &Timeline::zoom_out);
  tool_buttons_layout->addWidget(zoomOutButton);

  recordButton = new QPushButton();
  recordButton->setIcon(amber::icon::CreateIconFromSVG(QStringLiteral(":/icons/record.svg")));
  connect(recordButton, &QPushButton::clicked, this, &Timeline::record_btn_click);
  tool_buttons_layout->addWidget(recordButton);

  addButton = new QPushButton();
  addButton->setIcon(amber::icon::CreateIconFromSVG(QStringLiteral(":/icons/add-button.svg")));
  connect(addButton, &QPushButton::clicked, this, &Timeline::add_btn_click);
  tool_buttons_layout->addWidget(addButton);

  horizontalLayout->addWidget(tool_button_widget);

  timeline_area = new QWidget();
  QSizePolicy timeline_area_policy(QSizePolicy::Minimum, QSizePolicy::Minimum);
  timeline_area_policy.setHorizontalStretch(1);
  timeline_area_policy.setVerticalStretch(0);
  timeline_area_policy.setHeightForWidth(timeline_area->sizePolicy().hasHeightForWidth());
  timeline_area->setSizePolicy(timeline_area_policy);

  QVBoxLayout* timeline_area_layout = new QVBoxLayout(timeline_area);
  timeline_area_layout->setSpacing(0);
  timeline_area_layout->setContentsMargins(0, 0, 0, 0);

  headers = new TimelineHeader();
  timeline_area_layout->addWidget(headers);

  editAreas = new QWidget();
  QHBoxLayout* editAreaLayout = new QHBoxLayout(editAreas);
  editAreaLayout->setSpacing(0);
  editAreaLayout->setContentsMargins(0, 0, 0, 0);

  QSplitter* splitter = new QSplitter();
  splitter->setChildrenCollapsible(false);
  splitter->setOrientation(Qt::Vertical);

  QWidget* videoContainer = new QWidget();

  QHBoxLayout* videoContainerLayout = new QHBoxLayout(videoContainer);
  videoContainerLayout->setSpacing(0);
  videoContainerLayout->setContentsMargins(0, 0, 0, 0);

  video_area = new TimelineWidget();
  video_area->setFocusPolicy(Qt::ClickFocus);
  videoContainerLayout->addWidget(video_area);

  videoScrollbar = new QScrollBar();
  videoScrollbar->setMaximum(0);
  videoScrollbar->setSingleStep(20);
  videoScrollbar->setOrientation(Qt::Vertical);
  videoContainerLayout->addWidget(videoScrollbar);

  splitter->addWidget(videoContainer);

  QWidget* audioContainer = new QWidget();
  QHBoxLayout* audioContainerLayout = new QHBoxLayout(audioContainer);
  audioContainerLayout->setSpacing(0);
  audioContainerLayout->setContentsMargins(0, 0, 0, 0);

  audio_area = new TimelineWidget();
  audio_area->setFocusPolicy(Qt::ClickFocus);

  audioContainerLayout->addWidget(audio_area);

  audioScrollbar = new QScrollBar();
  audioScrollbar->setMaximum(0);
  audioScrollbar->setOrientation(Qt::Vertical);

  audioContainerLayout->addWidget(audioScrollbar);

  splitter->addWidget(audioContainer);

  editAreaLayout->addWidget(splitter);

  timeline_area_layout->addWidget(editAreas);

  horizontalScrollBar = new ResizableScrollBar();
  horizontalScrollBar->setMaximum(0);
  horizontalScrollBar->setSingleStep(20);
  horizontalScrollBar->setOrientation(Qt::Horizontal);

  timeline_area_layout->addWidget(horizontalScrollBar);

  horizontalLayout->addWidget(timeline_area);

  audio_monitor = new AudioMonitor();
  audio_monitor->setMinimumSize(QSize(50, 0));

  horizontalLayout->addWidget(audio_monitor);

  setWidget(dockWidgetContents);
}

void Timeline::Retranslate() {
  toolArrowButton->setToolTip(tr("Pointer Tool") + " (V)");
  toolEditButton->setToolTip(tr("Edit Tool") + " (X)");
  toolRippleButton->setToolTip(tr("Ripple Tool") + " (B)");
  toolRazorButton->setToolTip(tr("Razor Tool") + " (C)");
  toolSlipButton->setToolTip(tr("Slip Tool") + " (Y)");
  toolSlideButton->setToolTip(tr("Slide Tool") + " (U)");
  toolHandButton->setToolTip(tr("Hand Tool") + " (H)");
  toolTransitionButton->setToolTip(tr("Transition Tool") + " (T)");
  snappingButton->setToolTip(tr("Snapping") + " (S)");
  zoomInButton->setToolTip(tr("Zoom In") + " (=)");
  zoomOutButton->setToolTip(tr("Zoom Out") + " (-)");
  recordButton->setToolTip(tr("Record audio"));
  addButton->setToolTip(tr("Add title, solid, bars, etc."));

  UpdateTitle();
}

void Timeline::resizeEvent(QResizeEvent *) {
  // adjust maximum scrollbar
  if (amber::ActiveSequence != nullptr) set_sb_max();


  // resize tool button widget to its contents
  QList<QWidget*> tool_button_children = tool_button_widget->findChildren<QWidget*>();

  int horizontal_spacing = static_cast<FlowLayout*>(tool_button_widget->layout())->horizontalSpacing();
  int vertical_spacing = static_cast<FlowLayout*>(tool_button_widget->layout())->verticalSpacing();
  int total_area = tool_button_widget->height();

  int button_count = tool_button_children.size();
  int button_height = tool_button_children.at(0)->sizeHint().height() + vertical_spacing;

  int cols = 0;

  int col_height;

  if (button_height < total_area) {
    do {
      cols++;
      col_height = (qCeil(double(button_count)/double(cols))*button_height)-vertical_spacing;
    } while (col_height > total_area);
  } else {
    cols = button_count;
  }

  tool_button_widget->setFixedWidth((tool_button_children.at(0)->sizeHint().width())*cols + horizontal_spacing*(cols-1) + 1);
}

void Timeline::repaint_timeline() {
  if (!block_repaints) {
    if (amber::ActiveSequence != nullptr
        && !horizontalScrollBar->isSliderDown()
        && !horizontalScrollBar->is_resizing()
        && panel_sequence_viewer->playing
        && !zoom_just_changed) {
      // auto scroll — setValue triggers a recursive repaint_timeline() via setScroll,
      // so the widgets will be updated in that recursive call
      if (amber::CurrentConfig.autoscroll == amber::AUTOSCROLL_PAGE_SCROLL) {
        int playhead_x = getTimelineScreenPointFromFrame(amber::ActiveSequence->playhead);
        if (playhead_x < 0 || playhead_x > (editAreas->width() - videoScrollbar->width())) {
          int old_scroll = horizontalScrollBar->value();
          horizontalScrollBar->setValue(getScreenPointFromFrame(zoom, amber::ActiveSequence->playhead));
          if (horizontalScrollBar->value() != old_scroll) return;
        }
      } else if (amber::CurrentConfig.autoscroll == amber::AUTOSCROLL_SMOOTH_SCROLL) {
        if (center_scroll_to_playhead(horizontalScrollBar, zoom, amber::ActiveSequence->playhead)) {
          return;
        }
      }
    }

    headers->update();
    video_area->update();
    audio_area->update();

    if (amber::ActiveSequence != nullptr
        && !zoom_just_changed) {
      set_sb_max();
    }
  }
}

void Timeline::update_sequence() {
  bool null_sequence = (amber::ActiveSequence == nullptr);

  for (int i=0;i<tool_buttons.count();i++) {
    tool_buttons[i]->setEnabled(!null_sequence);
  }
  snappingButton->setEnabled(!null_sequence);
  zoomInButton->setEnabled(!null_sequence);
  zoomOutButton->setEnabled(!null_sequence);
  recordButton->setEnabled(!null_sequence);
  addButton->setEnabled(!null_sequence);
  headers->setEnabled(!null_sequence);

  UpdateTitle();
}

void Timeline::UpdateTitle() {
  QString title = tr("Timeline: ");
  if (amber::ActiveSequence == nullptr) {
    setWindowTitle(title + tr("(none)"));
  } else {
    setWindowTitle(title + amber::ActiveSequence->name);
    update_ui(false);
  }
}

void Timeline::set_sb_max() {
  headers->set_scrollbar_max(horizontalScrollBar, amber::ActiveSequence->getEndFrame(), editAreas->width() - getScreenPointFromFrame(zoom, 200));
}

void Timeline::set_marker() {
  // determine if any clips are selected, and if so add markers to clips rather than the sequence
  QVector<int> clips_selected;
  bool clip_mode = false;

  for (int i=0;i<amber::ActiveSequence->clips.size();i++) {
    Clip* c = amber::ActiveSequence->clips.at(i).get();
    if (c != nullptr
        && c->IsSelected()) {

      // only add markers if the playhead is inside the clip
      if (amber::ActiveSequence->playhead >= c->timeline_in()
          && amber::ActiveSequence->playhead <= c->timeline_out()) {
        clips_selected.append(i);
      }

      // we are definitely adding markers to clips though
      clip_mode = true;

    }
  }

  // if we've selected clips but none of the clips are within the playhead,
  // nothing to do here
  if (clip_mode && clips_selected.size() == 0) {
    return;
  }

  // pass off to internal set marker function
  set_marker_internal(amber::ActiveSequence.get(), clips_selected);

}

void Timeline::toggle_show_all() {
  if (amber::ActiveSequence != nullptr) {
    showing_all = !showing_all;
    if (showing_all) {
      old_zoom = zoom;
      set_zoom_value(double(timeline_area->width() - 200) / double(amber::ActiveSequence->getEndFrame()));
    } else {
      set_zoom_value(old_zoom);
    }
  }
}

bool Timeline::focused() {
  return (amber::ActiveSequence != nullptr && (headers->hasFocus() || video_area->hasFocus() || audio_area->hasFocus()));
}
