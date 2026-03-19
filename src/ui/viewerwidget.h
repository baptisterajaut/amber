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

#ifndef VIEWERWIDGET_H
#define VIEWERWIDGET_H

#include <QMatrix4x4>
#include <QMutex>
#include <QRhiWidget>
#include <QThread>
#include <QTimer>
#include <QWaitCondition>
#include <rhi/qrhi.h>

#include "effects/effect.h"
#include "project/footage.h"
#include "rendering/renderthread.h"
#include "engine/clip.h"
#include "core/guide.h"
#include "ui/viewercontainer.h"
#include "ui/viewerwindow.h"

class Viewer;
class ViewerOverlay;
struct GLTextureCoords;

class ViewerWidget : public QRhiWidget {
  Q_OBJECT
 public:
  ViewerWidget(QWidget* parent = nullptr);
  ~ViewerWidget() override;

  void close_window();
  void wait_until_render_is_paused();

  void initialize(QRhiCommandBuffer *cb) override;
  void render(QRhiCommandBuffer *cb) override;
  void releaseResources() override;
  Viewer* viewer;
  ViewerContainer* container;

  bool waveform{false};
  ClipPtr waveform_clip;
  const FootageStream* waveform_ms;
  double waveform_zoom{1.0};
  int waveform_scroll{0};

  void frame_update();
  RenderThread* get_renderer();
  void set_scroll(double x, double y);

  void start_guide_creation(Guide::Orientation orientation, int video_pos);
  void update_guide_creation(int video_pos);
  void finish_guide_creation();
  void cancel_guide_creation();
  QAction* guide_delete_action_;
  QAction* guide_mirror_action_;
 public slots:
  void set_waveform_scroll(int s);
  void set_fullscreen(int screen = 0);

 protected:
  bool event(QEvent* e) override;
  void resizeEvent(QResizeEvent* event) override;
  void keyPressEvent(QKeyEvent* event) override;
  void mousePressEvent(QMouseEvent* event) override;
  void mouseMoveEvent(QMouseEvent* event) override;
  void mouseReleaseEvent(QMouseEvent* event) override;
  void wheelEvent(QWheelEvent* event) override;

 private:
  friend class ViewerContainer;
  friend class ViewerOverlay;
  ViewerOverlay* overlay_{nullptr};
  void draw_waveform_func(QPainter& p);
  void draw_title_safe_area(QPainter& p);
  void draw_guides(QPainter& p);
  void draw_gizmos(QPainter& p);
  EffectGizmo* get_gizmo_from_mouse(int x, int y);
  void move_gizmos(QMouseEvent* event, bool done);
  int find_guide_at(int video_x, int video_y, bool* hit_mirror = nullptr) const;
  void show_guide_context_menu(int guide_index, const QPoint& global_pos, bool on_mirror = false);
  bool dragging{false};
  int dragging_guide_index_{-1};
  int dragging_guide_old_pos_{0};
  bool dragging_mirror_side_{false};
  int hovered_guide_index_{-1};
  bool hovered_mirror_side_{false};
  void guide_action_delete();
  void guide_action_mirror();
  bool creating_guide_{false};
  Guide::Orientation creating_guide_orientation_;
  int creating_guide_pos_{0};
  void seek_from_click(int x);
  Effect* gizmos{nullptr};
  int drag_start_x;
  int drag_start_y;
  int gizmo_x_mvmt;
  int gizmo_y_mvmt;
  EffectGizmo* selected_gizmo{nullptr};
  RenderThread* renderer;
  QRhi* rhi_{nullptr};
  bool rhi_initialized_{false};

  // RHI pipeline
  QRhiBuffer* vbuf_{nullptr};
  QRhiBuffer* vert_ubuf_{nullptr};
  QRhiBuffer* frag_ubuf_{nullptr};
  QRhiSampler* sampler_{nullptr};
  QRhiTexture* frame_tex_{nullptr};
  QRhiShaderResourceBindings* srb_{nullptr};
  QRhiGraphicsPipeline* pipeline_{nullptr};
  QSize rt_pixel_size_;
  int rt_sample_count_{0};
  int cached_tex_w_{0};
  int cached_tex_h_{0};
  ViewerWindow* window;
  double x_scroll{0};
  double y_scroll{0};
 public slots:
  void queue_repaint();

 private slots:
  void context_destroy();
  void retry();
  void show_context_menu();
  void save_frame();
  void fullscreen_menu_action(QAction* action);
  void set_fit_zoom();
  void set_custom_zoom();
  void set_menu_zoom(QAction* action);
};

class ViewerOverlay : public QWidget {
  Q_OBJECT
 public:
  ViewerOverlay(ViewerWidget* vw, QWidget* parent);

 protected:
  void paintEvent(QPaintEvent* event) override;

 private:
  ViewerWidget* vw_;
};

#endif  // VIEWERWIDGET_H
