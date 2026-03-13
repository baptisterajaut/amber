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
#include <QOpenGLFunctions>
#include <QOpenGLTexture>
#include <QOpenGLWidget>
#include <QThread>
#include <QTimer>
#include <QWaitCondition>

#include "effects/effect.h"
#include "project/footage.h"
#include "rendering/renderthread.h"
#include "timeline/clip.h"
#include "timeline/guide.h"
#include "ui/viewercontainer.h"
#include "ui/viewerwindow.h"

class Viewer;
class QOpenGLFramebufferObject;
struct GLTextureCoords;

class ViewerWidget : public QOpenGLWidget, QOpenGLFunctions {
  Q_OBJECT
 public:
  ViewerWidget(QWidget* parent = nullptr);
  ~ViewerWidget() override;

  void close_window();
  void wait_until_render_is_paused();

  void paintGL() override;
  void initializeGL() override;
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
  void keyPressEvent(QKeyEvent* event) override;
  void mousePressEvent(QMouseEvent* event) override;
  void mouseMoveEvent(QMouseEvent* event) override;
  void mouseReleaseEvent(QMouseEvent* event) override;
  void wheelEvent(QWheelEvent* event) override;

 private:
  void draw_waveform_func();
  void draw_title_safe_area();
  void draw_guides();
  void draw_gizmos();
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
  QOpenGLShaderProgram* passthrough_program_{nullptr};
  RenderThread* renderer;
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

#endif  // VIEWERWIDGET_H
