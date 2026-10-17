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

#ifndef SCOPESPANEL_H
#define SCOPESPANEL_H

#include <QTimer>

#include "ui/panel.h"

class QComboBox;
class ScopeWidget;

// Video scopes of the sequence viewer's frame (what gets exported), computed on the GUI thread from the viewer's
// CPU readback. Costs nothing while hidden or behind another tab.
class ScopesPanel : public Panel {
  Q_OBJECT
 public:
  explicit ScopesPanel(QWidget* parent);
  ~ScopesPanel() override;
  void Retranslate() override;

  // One byte: the mode index
  void LoadLayoutState(const QByteArray& data) override;
  QByteArray SaveLayoutState() override;

 private slots:
  /** The sequence viewer's RenderThread finished a frame: schedule an update unless one is already pending. */
  void onFrameReady();

  /** Samples the viewer's latest frame under its buffer lock and hands it to the scope. */
  void processFrame();

 private:
  QComboBox* mode_combo_;
  ScopeWidget* scope_;

  // Last visibilityChanged(): false while hidden or behind another tab. isVisible() isn't enough: a tabbed dock
  // stays visible, just stacked under the current tab, once it has been shown.
  bool on_screen_ = false;

  // Coalesces ready() bursts: at most one update per 33 ms (~30/s); the render thread is never slowed down
  QTimer throttle_;
};

#endif  // SCOPESPANEL_H
