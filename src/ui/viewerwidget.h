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

#include <rhi/qrhi.h>
#include <QElapsedTimer>
#include <QMatrix4x4>
#include <QMutex>
#include <QRhiWidget>
#include <QThread>
#include <QTimer>
#include <QWaitCondition>

#include "core/fpsmeter.h"
#include "core/guide.h"
#include "core/pixelsampler.h"
#include "core/scrubresolution.h"
#include "effects/effect.h"
#include "engine/clip.h"
#include "project/footage.h"
#include "rendering/renderthread.h"
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

  void initialize(QRhiCommandBuffer* cb) override;
  void render(QRhiCommandBuffer* cb) override;
  void releaseResources() override;
  Viewer* viewer;
  ViewerContainer* container;

  bool waveform{false};
  ClipPtr waveform_clip;
  const FootageStream* waveform_ms;
  double waveform_zoom{1.0};
  int waveform_scroll{0};

  void frame_update();
  // A ruler scrub ended on a mouse release, whose refresh is the next request: makes it the settle render
  void scrub_released();
  RenderThread* get_renderer();
  void set_scroll(double x, double y);

  void start_guide_creation(Guide::Orientation orientation, int video_pos);
  void update_guide_creation(int video_pos);
  void finish_guide_creation();
  void cancel_guide_creation();
  QAction* guide_delete_action_;
  QAction* guide_mirror_action_;

  void startColorPick();

  // Repaints the overlay widget (sibling QWidget over the QRhiWidget).
  // Use this when overlay-only state changes (e.g. voice-over pre-roll countdown)
  // so we don't trigger a full RHI re-render.
  void update_overlay();

  // Playback stopped: empties the frame rate window, so the next play never averages across the pause, and hides
  // the label (no new frame repaints the overlay after a pause)
  void reset_frame_rate();
  // Where render() draws the frame, widget coordinates: the widget itself, or larger past the zoom where the container
  // stops growing it, panned by the scrollbars. Overlays, hit tests and the rulers map through it.
  QRectF frame_rect() const;

 signals:
  void colorPicked(const QColor& color);
  void colorPickCancelled();
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
  void leaveEvent(QEvent* event) override;
  void wheelEvent(QWheelEvent* event) override;

 private:
  friend class ViewerContainer;
  friend class ViewerOverlay;
  ViewerOverlay* overlay_{nullptr};
  void draw_waveform_func(QPainter& p);
  void draw_title_safe_area(QPainter& p);
  void draw_guides(QPainter& p);
  void draw_gizmos(QPainter& p);
  // View > Show Frame Rate: true while the label is due (sequence viewer, playing)
  bool frame_rate_shown() const;
  void draw_frame_rate(QPainter& p);
  // View > Show Pixel Sampler: true while the readout is due (pointer over a frame, no eyedropper)
  bool pixel_sampler_shown() const;
  void draw_pixel_sampler(QPainter& p);
  // Reads the pixel under the last pointer position into the readout and repaints the overlay
  void sample_pixel();
  EffectGizmo* get_gizmo_from_mouse(int x, int y);
  void move_gizmos(QMouseEvent* event, bool done);
  // Body of frame_update(); with_audio = false skips the audio scrub grain
  void request_render(bool with_audio);
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
  // Returns true if the event was fully handled (caller should return).
  bool move_handle_guide_drag(int video_x, int image_y, const QPoint& widget_pos);
  void move_handle_dragging(QMouseEvent* event);
  // Returns true if the event was fully handled by color-pick mode (caller should return).
  bool press_handle_color_pick(QMouseEvent* event);
  // Returns true if a guide was hit (right-click context menu or left-click drag started).
  bool press_handle_guide(int video_x, int image_y, QMouseEvent* event);
  void press_set_gizmo_drag(QMouseEvent* event);
  // True over a gizmo handle (dot/target), which takes the mouse before a guide under it
  bool gizmo_handle_at(const QPoint& widget_pos);
  // Uploads a new frame texture to the RHI pipeline if the frame dimensions changed.
  void render_update_frame_texture(QRhiResourceUpdateBatch* u, const char* frame_data, int fw, int fh);
  bool creating_guide_{false};
  Guide::Orientation creating_guide_orientation_;
  int creating_guide_pos_{0};
  bool color_pick_mode_{false};
  void exitColorPickMode();
  // Colour of the shown frame at a widget position, as stored in the front buffer, and the sequence pixel there.
  // Invalid with no frame or off the frame.
  QColor readPixelAt(const QPointF& pos, QPoint* seq_pos = nullptr);
  // Widget size, zoom, scrolls and sequence size, as render() lays the frame out (empty sequence size with none open)
  amber::ViewerFrameGeometry frame_geometry() const;
  void seek_from_click(int x);
  Effect* gizmos{nullptr};
  int drag_start_x;
  int drag_start_y;
  int gizmo_x_mvmt;
  int gizmo_y_mvmt;
  // Sub-pixel gizmo drag movement not yet applied, carried between mouse events
  double gizmo_x_rem_{0};
  double gizmo_y_rem_{0};
  EffectGizmo* selected_gizmo{nullptr};
  RenderThread* renderer;
  QOffscreenSurface* gl_fallback_surface_{nullptr};  // owned by us, created on GUI thread
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
  QString save_frame_last_dir_;
  // Half resolution while scrubbing: Auto decision state and the settle timer (restarted by each scrub render)
  amber::ScrubResolution scrub_res_;
  QTimer settle_timer_;
  int last_requested_divider_{1};  // divider of the last viewer render requested, reused by the retries and checked by the settle
  // Frame rate overlay: displayed frames, timed when render() takes in a newly published one (serial changed) that
  // shows another sequence frame (playhead changed)
  amber::FpsMeter fps_meter_;
  QElapsedTimer fps_clock_;
  quint64 fps_last_serial_{0};
  long fps_last_playhead_{-1};
  // Pixel sampler: last pointer position over the widget and the pixel read there (invalid colour: nothing to show)
  bool sampler_hover_{false};
  QPointF sampler_pos_;
  QColor sampler_color_;
  QPoint sampler_seq_pos_;
 public slots:
  void queue_repaint();

 private slots:
  void context_destroy();
  // Scrub gesture over: re-renders the same frame at the base divider if the last request was reduced
  void scrub_settled();
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
