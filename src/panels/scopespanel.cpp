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

#include "scopespanel.h"

#include <QComboBox>
#include <QHBoxLayout>
#include <QMutexLocker>
#include <QVBoxLayout>

#include "panels.h"
#include "rendering/renderthread.h"
#include "ui/scopewidget.h"
#include "ui/viewerwidget.h"

ScopesPanel::ScopesPanel(QWidget* parent) : Panel(parent) {
  setObjectName("scopes");

  QWidget* central = new QWidget(this);
  QVBoxLayout* layout = new QVBoxLayout(central);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(2);

  QHBoxLayout* top_row = new QHBoxLayout();
  top_row->setContentsMargins(2, 2, 2, 0);
  mode_combo_ = new QComboBox(central);
  for (int i = 0; i < kScopeModeCount; i++) mode_combo_->addItem(QString());  // texts set in Retranslate()
  top_row->addWidget(mode_combo_);
  top_row->addStretch();
  layout->addLayout(top_row);

  scope_ = new ScopeWidget(central);
  layout->addWidget(scope_, 1);
  setWidget(central);

  throttle_.setSingleShot(true);
  throttle_.setInterval(33);
  connect(&throttle_, &QTimer::timeout, this, &ScopesPanel::processFrame);

  connect(mode_combo_, &QComboBox::currentIndexChanged, this,
          [this](int index) { scope_->setMode(static_cast<ScopeMode>(index)); });

  // Shown, or brought to the front of its tabs: frames rendered meanwhile were skipped, show the current one now
  connect(this, &QDockWidget::visibilityChanged, this, [this](bool visible) {
    on_screen_ = visible;
    if (visible) processFrame();
  });

  // The sequence viewer's RenderThread lives as long as its ViewerWidget, which outlives this panel (free_panels()
  // deletes us first), so one connection is enough. ready() is emitted from the render thread: queue it here.
  if (panel_sequence_viewer != nullptr && panel_sequence_viewer->viewer_widget != nullptr) {
    connect(panel_sequence_viewer->viewer_widget->get_renderer(), &RenderThread::ready, this,
            &ScopesPanel::onFrameReady, Qt::QueuedConnection);
  }
}

ScopesPanel::~ScopesPanel() { throttle_.stop(); }

void ScopesPanel::Retranslate() {
  setWindowTitle(tr("Scopes"));
  mode_combo_->setItemText(int(ScopeMode::LumaWaveform), tr("Luma Waveform"));
  mode_combo_->setItemText(int(ScopeMode::RgbParade), tr("RGB Parade"));
  mode_combo_->setItemText(int(ScopeMode::Histogram), tr("Histogram"));
  mode_combo_->setItemText(int(ScopeMode::Vectorscope), tr("Vectorscope"));
}

void ScopesPanel::LoadLayoutState(const QByteArray& data) {
  if (data.isEmpty()) return;
  const int mode = static_cast<uchar>(data.at(0));
  if (mode < kScopeModeCount) mode_combo_->setCurrentIndex(mode);
}

QByteArray ScopesPanel::SaveLayoutState() { return QByteArray(1, char(mode_combo_->currentIndex())); }

void ScopesPanel::onFrameReady() {
  if (!on_screen_) return;
  if (!throttle_.isActive()) throttle_.start();
}

void ScopesPanel::processFrame() {
  if (!on_screen_) return;  // hidden while the throttle was armed
  amber::scopes::Sample sample;
  // Never cache the RenderThread: fetch it through the viewer panel each time
  RenderThread* renderer = nullptr;
  if (panel_sequence_viewer != nullptr && panel_sequence_viewer->viewer_widget != nullptr &&
      panel_sequence_viewer->seq != nullptr) {
    renderer = panel_sequence_viewer->viewer_widget->get_renderer();
  }
  if (renderer != nullptr) {
    // Same lock ViewerWidget::render() holds for its upload; held only for the point-sampling copy
    const int idx = renderer->front_buffer_index();
    QMutexLocker lock(renderer->get_texture_mutex(idx));
    const char* data = renderer->get_frame_data(idx);
    const int w = renderer->get_frame_width(idx);
    const int h = renderer->get_frame_height(idx);
    if (data != nullptr && w > 0 && h > 0) {
      sample = amber::scopes::point_sample(reinterpret_cast<const uchar*>(data), w, h, w * 4);
    }
  }
  scope_->setFrame(std::move(sample));
}
