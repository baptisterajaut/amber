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

#ifndef SCOPEWIDGET_H
#define SCOPEWIDGET_H

#include <QImage>
#include <QWidget>

#include "core/scopes.h"

// Order of the Scopes panel's mode combo; the index is saved in the layout file: append only.
enum class ScopeMode { LumaWaveform = 0, RgbParade = 1, Histogram = 2, Vectorscope = 3 };
constexpr int kScopeModeCount = 4;

// Draws one scope of a sampled frame with QPainter. The analysis runs when a frame or the mode is set, not on
// paint; an empty frame draws the graticule only.
class ScopeWidget : public QWidget {
  Q_OBJECT
 public:
  explicit ScopeWidget(QWidget* parent = nullptr);

  void setMode(ScopeMode mode);
  ScopeMode mode() const { return mode_; }

  // Takes the latest sampled frame and recomputes the current scope.
  void setFrame(amber::scopes::Sample sample);

  QSize sizeHint() const override { return QSize(400, 260); }

 protected:
  void paintEvent(QPaintEvent* event) override;

 private:
  void rebuild();

  ScopeMode mode_ = ScopeMode::LumaWaveform;
  amber::scopes::Sample sample_;

  // Log-scaled counts at the grid's native size: [0] for the waveform and the vectorscope, [0..2] for the parade
  QImage images_[3];
  amber::scopes::Histogram histogram_;
};

#endif  // SCOPEWIDGET_H
