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

#ifndef VIEWERWINDOW_H
#define VIEWERWINDOW_H

#include <QByteArray>
#include <QRhiWidget>
#include <QTimer>
#include <rhi/qrhi.h>

class QMenu;
class QShortcut;
class QLabel;

class ViewerWindow : public QRhiWidget {
  Q_OBJECT
 public:
  ViewerWindow(QWidget* parent);
  ~ViewerWindow() override;
  void set_frame(const char* data, int w, int h);

 protected:
  void closeEvent(QCloseEvent*) override;
  void showEvent(QShowEvent*) override;
  void keyPressEvent(QKeyEvent*) override;
  void mousePressEvent(QMouseEvent*) override;
  void mouseMoveEvent(QMouseEvent*) override;
  void resizeEvent(QResizeEvent*) override;

  void initialize(QRhiCommandBuffer* cb) override;
  void render(QRhiCommandBuffer* cb) override;
  void releaseResources() override;

 private:
  // CPU frame data (copied from RenderThread)
  QByteArray frame_data_;
  int frame_w_{0};
  int frame_h_{0};
  double ar{1.0};

  // RHI pipeline
  QRhi* rhi_{nullptr};
  bool rhi_initialized_{false};
  QRhiBuffer* vbuf_{nullptr};
  QRhiBuffer* vert_ubuf_{nullptr};
  QRhiBuffer* frag_ubuf_{nullptr};
  QRhiSampler* sampler_{nullptr};
  QRhiTexture* frame_tex_{nullptr};
  QRhiShaderResourceBindings* srb_{nullptr};
  QRhiGraphicsPipeline* pipeline_{nullptr};
  int cached_tex_w_{0};
  int cached_tex_h_{0};

  // shortcuts
  void shortcut_copier(QVector<QShortcut*>& shortcuts, QMenu* menu);
  QVector<QShortcut*> shortcuts_;

  // exit full screen message
  QLabel* fullscreen_msg_label_;
  QTimer fullscreen_msg_timer;
  void position_fullscreen_msg();
 private slots:
  void fullscreen_msg_timeout();
};

#endif  // VIEWERWINDOW_H
