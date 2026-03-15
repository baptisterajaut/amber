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

#include "viewerwidget.h"

extern "C" {
#include <libavformat/avformat.h>
}

#include <QApplication>
#include <QAudioSink>
#include <QDrag>
#include <QFileDialog>
#include <QInputDialog>
#include <QMenu>
#include <QMessageBox>
#include <QMimeData>
#include <QMouseEvent>
#include <QFile>
#include <QPainter>
#include <QPolygon>
#include <QRegularExpression>
#include <QScreen>
#include <QtMath>

#include "global/config.h"
#include "global/debug.h"
#include "global/math.h"
#include "mainwindow.h"
#include "panels/panels.h"
#include "project/media.h"
#include "project/projectelements.h"
#include "rendering/audio.h"
#include "rendering/cacher.h"
#include "rendering/renderfunctions.h"
#include "rendering/renderthread.h"
#include "ui/collapsiblewidget.h"
#include "ui/menu.h"
#include "ui/timelinewidget.h"
#include "ui/viewercontainer.h"
#include "ui/viewerwindow.h"
#include "undo/comboaction.h"
#include "undo/undo.h"
#include "undo/undo_guide.h"
#include "undo/undostack.h"

ViewerWidget::ViewerWidget(QWidget* parent)
    : QRhiWidget(parent)

{
  switch (olive::CurrentRuntimeConfig.rhi_backend) {
    case RhiBackend::Vulkan: setApi(Api::Vulkan); break;
    case RhiBackend::Metal: setApi(Api::Metal); break;
    case RhiBackend::D3D12: setApi(Api::Direct3D12); break;
    case RhiBackend::D3D11: setApi(Api::Direct3D11); break;
    default: setApi(Api::OpenGL); break;
  }
  setMinimumSize(1, 1);  // Prevent 0-height from QDockWidget — QRhiWidget never recovers from size 0 on Vulkan
  setMouseTracking(true);
  setFocusPolicy(Qt::ClickFocus);

  setContextMenuPolicy(Qt::CustomContextMenu);
  connect(this, &ViewerWidget::customContextMenuRequested, this, &ViewerWidget::show_context_menu);

  renderer = new RenderThread();
  renderer->start(QThread::HighestPriority);
  connect(renderer, &RenderThread::ready, this, &ViewerWidget::queue_repaint);
  connect(renderer, &RenderThread::finished, renderer, &RenderThread::deleteLater);

  // Guide shortcut actions — shortcuts stored on QAction for display + preferences editability.
  // WidgetShortcut context without addAction() prevents Qt from registering them in QShortcutMap,
  // avoiding ambiguity with global Del/M. Actual key interception via event(ShortcutOverride).
  guide_delete_action_ = new QAction(this);
  guide_delete_action_->setProperty("id", "guidedelete");
  guide_delete_action_->setProperty("default", "Del");
  guide_delete_action_->setShortcut(QKeySequence("Del"));
  guide_delete_action_->setShortcutContext(Qt::WidgetShortcut);

  guide_mirror_action_ = new QAction(this);
  guide_mirror_action_->setProperty("id", "guidemirror");
  guide_mirror_action_->setProperty("default", "M");
  guide_mirror_action_->setShortcut(QKeySequence("M"));
  guide_mirror_action_->setShortcutContext(Qt::WidgetShortcut);

  window = new ViewerWindow(this);

  // Overlay created by ViewerContainer as sibling — child QWidget over
  // QRhiWidget breaks Vulkan compositing in Amber's widget hierarchy.
}

ViewerWidget::~ViewerWidget() {
  renderer->cancel();
  delete renderer;
}

void ViewerWidget::set_waveform_scroll(int s) {
  if (waveform) {
    waveform_scroll = s;
    update();
  }
}

void ViewerWidget::set_fullscreen(int screen) {
  if (screen >= 0 && screen < QGuiApplication::screens().size()) {
    QScreen* selected_screen = QGuiApplication::screens().at(screen);
    window->showFullScreen();
    window->setGeometry(selected_screen->geometry());
  } else {
    qCritical() << "Failed to find requested screen" << screen << "to set fullscreen to";
  }
}

void ViewerWidget::show_context_menu() {
  Menu menu(this);

  QAction* save_frame_as_image = menu.addAction(tr("Save Frame as Image..."));
  connect(save_frame_as_image, &QAction::triggered, this, &ViewerWidget::save_frame);

  Menu* fullscreen_menu = new Menu(tr("Show Fullscreen"));
  menu.addMenu(fullscreen_menu);
  QList<QScreen*> screens = QGuiApplication::screens();
  if (window->isVisible()) {
    fullscreen_menu->addAction(tr("Disable"));
  }
  for (int i = 0; i < screens.size(); i++) {
    QAction* screen_action =
        fullscreen_menu->addAction(tr("Screen %1: %2x%3")
                                       .arg(QString::number(i), QString::number(screens.at(i)->size().width()),
                                            QString::number(screens.at(i)->size().height())));
    screen_action->setData(i);
  }
  connect(fullscreen_menu, &QMenu::triggered, this, &ViewerWidget::fullscreen_menu_action);

  Menu zoom_menu(tr("Zoom"));
  QAction* fit_zoom = zoom_menu.addAction(tr("Fit"));
  connect(fit_zoom, &QAction::triggered, this, &ViewerWidget::set_fit_zoom);
  zoom_menu.addAction("10%")->setData(0.1);
  zoom_menu.addAction("25%")->setData(0.25);
  zoom_menu.addAction("50%")->setData(0.5);
  zoom_menu.addAction("75%")->setData(0.75);
  zoom_menu.addAction("100%")->setData(1.0);
  zoom_menu.addAction("150%")->setData(1.5);
  zoom_menu.addAction("200%")->setData(2.0);
  zoom_menu.addAction("400%")->setData(4.0);
  QAction* custom_zoom = zoom_menu.addAction(tr("Custom"));
  connect(custom_zoom, &QAction::triggered, this, &ViewerWidget::set_custom_zoom);
  connect(&zoom_menu, &QMenu::triggered, this, &ViewerWidget::set_menu_zoom);
  menu.addMenu(&zoom_menu);

  if (!viewer->is_main_sequence()) {
    menu.addAction(tr("Close Media"), viewer, &Viewer::close_media);
  }

  menu.exec(QCursor::pos());
}

void ViewerWidget::save_frame() {
  QFileDialog fd(this);
  fd.setAcceptMode(QFileDialog::AcceptSave);
  fd.setFileMode(QFileDialog::AnyFile);
  fd.setWindowTitle(tr("Save Frame"));
  fd.setNameFilter(
      "Portable Network Graphic (*.png);;JPEG (*.jpg);;Windows Bitmap (*.bmp);;Portable Pixmap (*.ppm);;X11 Bitmap "
      "(*.xbm);;X11 Pixmap (*.xpm)");

  if (fd.exec()) {
    QString fn = fd.selectedFiles().at(0);
    QString selected_ext = fd.selectedNameFilter().mid(
        fd.selectedNameFilter().indexOf(QRegularExpression("\\*\\.[a-z][a-z][a-z]")) + 1, 4);
    if (!fn.endsWith(selected_ext, Qt::CaseInsensitive)) {
      fn += selected_ext;
    }

    renderer->start_render(viewer->seq.get(), 1, fn);
  }
}

void ViewerWidget::queue_repaint() {
  update();
  if (overlay_) overlay_->update();
}

void ViewerWidget::fullscreen_menu_action(QAction* action) {
  if (action->data().isNull()) {
    window->hide();
  } else {
    set_fullscreen(action->data().toInt());
  }
}

void ViewerWidget::set_fit_zoom() {
  container->fit = true;
  container->adjust();
}

void ViewerWidget::set_custom_zoom() {
  bool ok;
  double d = QInputDialog::getDouble(this, tr("Viewer Zoom"), tr("Set Custom Zoom Value:"), container->zoom * 100, 0,
                                     2147483647, 2, &ok);
  if (ok) {
    container->fit = false;
    container->zoom = d * 0.01;
    container->adjust();
  }
}

void ViewerWidget::set_menu_zoom(QAction* action) {
  const QVariant& data = action->data();
  if (!data.isNull()) {
    container->fit = false;
    container->zoom = data.toDouble();
    container->adjust();
  }
}

void ViewerWidget::retry() { update(); }

void ViewerWidget::initialize(QRhiCommandBuffer *cb) {
  if (rhi_ != rhi()) {
    releaseResources();
  }

  rhi_ = rhi();

  if (!rhi_initialized_) {
    QFile vsFile(QStringLiteral(":/shaders/common.vert.qsb"));
    if (!vsFile.open(QIODevice::ReadOnly)) {
      qCritical() << "Failed to load vertex shader";
      return;
    }
    QShader vs = QShader::fromSerialized(vsFile.readAll());

    QFile fsFile(QStringLiteral(":/shaders/passthrough.frag.qsb"));
    if (!fsFile.open(QIODevice::ReadOnly)) {
      qCritical() << "Failed to load fragment shader";
      return;
    }
    QShader fs = QShader::fromSerialized(fsFile.readAll());

    vbuf_ = rhi_->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::VertexBuffer, 4 * 4 * sizeof(float));
    vbuf_->create();

    vert_ubuf_ = rhi_->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, 64);
    vert_ubuf_->create();

    frag_ubuf_ = rhi_->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, 16);
    frag_ubuf_->create();

    sampler_ = rhi_->newSampler(QRhiSampler::Linear, QRhiSampler::Linear, QRhiSampler::None,
                                 QRhiSampler::ClampToEdge, QRhiSampler::ClampToEdge);
    sampler_->create();

    // Placeholder 1x1 texture — replaced by actual frame data in render()
    frame_tex_ = rhi_->newTexture(QRhiTexture::RGBA8, QSize(1, 1));
    frame_tex_->create();

    srb_ = rhi_->newShaderResourceBindings();
    srb_->setBindings({
        QRhiShaderResourceBinding::uniformBuffer(0, QRhiShaderResourceBinding::VertexStage, vert_ubuf_),
        QRhiShaderResourceBinding::uniformBuffer(1, QRhiShaderResourceBinding::FragmentStage, frag_ubuf_),
        QRhiShaderResourceBinding::sampledTexture(2, QRhiShaderResourceBinding::FragmentStage, frame_tex_, sampler_),
    });
    srb_->create();

    pipeline_ = rhi_->newGraphicsPipeline();
    pipeline_->setShaderStages({
        {QRhiShaderStage::Vertex, vs},
        {QRhiShaderStage::Fragment, fs},
    });

    QRhiVertexInputLayout inputLayout;
    inputLayout.setBindings({{4 * sizeof(float)}});
    inputLayout.setAttributes({
        {0, 0, QRhiVertexInputAttribute::Float2, 0},
        {0, 1, QRhiVertexInputAttribute::Float2, 2 * sizeof(float)},
    });
    pipeline_->setVertexInputLayout(inputLayout);
    pipeline_->setTopology(QRhiGraphicsPipeline::TriangleStrip);

    QRhiGraphicsPipeline::TargetBlend blend;
    blend.enable = true;
    blend.srcColor = QRhiGraphicsPipeline::SrcAlpha;
    blend.dstColor = QRhiGraphicsPipeline::OneMinusSrcAlpha;
    blend.srcAlpha = QRhiGraphicsPipeline::Zero;
    blend.dstAlpha = QRhiGraphicsPipeline::One;
    pipeline_->setTargetBlends({blend});

    pipeline_->setShaderResourceBindings(srb_);
    pipeline_->setRenderPassDescriptor(renderTarget()->renderPassDescriptor());
    pipeline_->create();

    rhi_initialized_ = true;
  }
}

void ViewerWidget::releaseResources() {
  delete pipeline_;
  pipeline_ = nullptr;
  delete srb_;
  srb_ = nullptr;
  delete frame_tex_;
  frame_tex_ = nullptr;
  delete sampler_;
  sampler_ = nullptr;
  delete frag_ubuf_;
  frag_ubuf_ = nullptr;
  delete vert_ubuf_;
  vert_ubuf_ = nullptr;
  delete vbuf_;
  vbuf_ = nullptr;

  cached_tex_w_ = 0;
  cached_tex_h_ = 0;

  rhi_initialized_ = false;
}

void ViewerWidget::resizeEvent(QResizeEvent* event) {
  QRhiWidget::resizeEvent(event);
  if (overlay_) overlay_->setGeometry(geometry());  // overlay is sibling in ViewerContainer
}

void ViewerWidget::frame_update() {
  if (viewer->seq != nullptr) {
    if (waveform) {
      update();
    } else {
      bool scrubbing = !viewer->playing;
      renderer->start_render(viewer->seq.get(), viewer->get_playback_speed(),
                             nullptr, nullptr, 0, 0, false, scrubbing);
    }

    olive::rendering::compose_audio(viewer, viewer->seq.get(), viewer->get_playback_speed(), false);
  }
}

RenderThread* ViewerWidget::get_renderer() { return renderer; }

void ViewerWidget::set_scroll(double x, double y) {
  x_scroll = x;
  y_scroll = y;
  update();
  if (overlay_) overlay_->update();
}

void ViewerWidget::seek_from_click(int x) { viewer->seek(getFrameFromScreenPoint(waveform_zoom, x + waveform_scroll)); }

void ViewerWidget::context_destroy() {
  if (viewer->seq != nullptr) {
    close_active_clips(viewer->seq.get());
  }
  renderer->delete_ctx();
}

EffectGizmo* ViewerWidget::get_gizmo_from_mouse(int x, int y) {
  if (gizmos != nullptr) {
    double multiplier = double(viewer->seq->width) / double(width());
    QPoint mouse_pos(qRound(x * multiplier), qRound((height() - y) * multiplier));
    int dot_size = 2 * qRound(GIZMO_DOT_SIZE * multiplier);
    int target_size = 2 * qRound(GIZMO_TARGET_SIZE * multiplier);
    for (int i = 0; i < gizmos->gizmo_count(); i++) {
      EffectGizmo* g = gizmos->gizmo(i);

      switch (g->get_type()) {
        case GIZMO_TYPE_DOT:
          if (mouse_pos.x() > g->screen_pos[0].x() - dot_size && mouse_pos.y() > g->screen_pos[0].y() - dot_size &&
              mouse_pos.x() < g->screen_pos[0].x() + dot_size && mouse_pos.y() < g->screen_pos[0].y() + dot_size) {
            return g;
          }
          break;
        case GIZMO_TYPE_POLY:
          if (QPolygon(g->screen_pos).containsPoint(mouse_pos, Qt::OddEvenFill)) {
            return g;
          }
          break;
        case GIZMO_TYPE_TARGET:
          if (mouse_pos.x() > g->screen_pos[0].x() - target_size &&
              mouse_pos.y() > g->screen_pos[0].y() - target_size &&
              mouse_pos.x() < g->screen_pos[0].x() + target_size &&
              mouse_pos.y() < g->screen_pos[0].y() + target_size) {
            return g;
          }
          break;
      }
    }
  }
  return nullptr;
}

void ViewerWidget::move_gizmos(QMouseEvent* event, bool done) {
  if (selected_gizmo != nullptr) {
    double multiplier = double(viewer->seq->width) / double(width());

    int x_movement = qRound((event->position().toPoint().x() - drag_start_x) * multiplier);
    int y_movement = qRound((event->position().toPoint().y() - drag_start_y) * multiplier);

    gizmos->gizmo_move(selected_gizmo, x_movement, y_movement,
                       get_timecode(gizmos->parent_clip, gizmos->parent_clip->sequence->playhead), done);

    gizmo_x_mvmt += x_movement;
    gizmo_y_mvmt += y_movement;

    drag_start_x = event->position().toPoint().x();
    drag_start_y = event->position().toPoint().y();
  }
}

bool ViewerWidget::event(QEvent* e) {
  if (e->type() == QEvent::ShortcutOverride && hovered_guide_index_ >= 0 && !olive::CurrentConfig.lock_guides) {
    auto* ke = static_cast<QKeyEvent*>(e);
    QKeySequence pressed(ke->key() | ke->modifiers());
    if (pressed == guide_delete_action_->shortcut() || pressed == guide_mirror_action_->shortcut()) {
      e->accept();
      return true;
    }
  }
  return QRhiWidget::event(e);
}

void ViewerWidget::keyPressEvent(QKeyEvent* event) {
  if (hovered_guide_index_ >= 0 && viewer->seq != nullptr && !olive::CurrentConfig.lock_guides) {
    QKeySequence pressed(event->key() | event->modifiers());
    if (pressed == guide_delete_action_->shortcut()) {
      guide_action_delete();
      return;
    }
    if (pressed == guide_mirror_action_->shortcut()) {
      guide_action_mirror();
      return;
    }
  }
  QRhiWidget::keyPressEvent(event);
}

void ViewerWidget::mousePressEvent(QMouseEvent* event) {
  if (waveform) {
    seek_from_click(qRound(event->position().x()));
  } else if (event->buttons() & Qt::MiddleButton || panel_timeline->tool == TIMELINE_TOOL_HAND) {
    container->dragScrollPress(event->position().toPoint() * container->zoom);
  } else if (viewer->seq != nullptr && olive::CurrentConfig.show_guides && !olive::CurrentConfig.lock_guides) {
    double multiplier = double(viewer->seq->width) / double(width());
    int video_x = qRound(event->position().x() * multiplier);
    int image_y = qRound(event->position().y() * multiplier);

    if (event->buttons() & Qt::RightButton) {
      bool hit_mirror = false;
      int idx = find_guide_at(video_x, image_y, &hit_mirror);
      if (idx >= 0) {
        show_guide_context_menu(idx, event->globalPosition().toPoint(), hit_mirror);
        setContextMenuPolicy(Qt::PreventContextMenu);
        QTimer::singleShot(0, this, [this]() { setContextMenuPolicy(Qt::CustomContextMenu); });
        return;
      }
    } else if (event->buttons() & Qt::LeftButton) {
      bool hit_mirror = false;
      int idx = find_guide_at(video_x, image_y, &hit_mirror);
      if (idx >= 0) {
        dragging_guide_index_ = idx;
        dragging_guide_old_pos_ = viewer->seq->guides[idx].position;
        dragging_mirror_side_ = hit_mirror;
        dragging = true;
        return;
      }
      // Fall through to gizmo handling
      drag_start_x = event->position().toPoint().x();
      drag_start_y = event->position().toPoint().y();

      gizmo_x_mvmt = 0;
      gizmo_y_mvmt = 0;

      selected_gizmo = get_gizmo_from_mouse(event->position().toPoint().x(), event->position().toPoint().y());
    }
  } else if (event->buttons() & Qt::LeftButton) {
    drag_start_x = event->position().toPoint().x();
    drag_start_y = event->position().toPoint().y();

    gizmo_x_mvmt = 0;
    gizmo_y_mvmt = 0;

    selected_gizmo = get_gizmo_from_mouse(event->position().toPoint().x(), event->position().toPoint().y());
  }
  dragging = true;
}

void ViewerWidget::mouseMoveEvent(QMouseEvent* event) {
  unsetCursor();
  if (panel_timeline->tool == TIMELINE_TOOL_HAND) {
    setCursor(Qt::OpenHandCursor);
  }

  if (viewer->seq != nullptr && width() > 0) {
    double multiplier = double(viewer->seq->width) / double(width());
    int video_x = qRound(event->position().x() * multiplier);
    int image_y = qRound(event->position().y() * multiplier);

    if (dragging_guide_index_ >= 0) {
      Guide& g = viewer->seq->guides[dragging_guide_index_];
      int raw_pos = (g.orientation == Guide::Horizontal) ? image_y : video_x;
      if (dragging_mirror_side_) {
        int dim = (g.orientation == Guide::Horizontal) ? viewer->seq->height : viewer->seq->width;
        g.position = dim - raw_pos;
      } else {
        g.position = raw_pos;
      }
      update();
      return;
    }

    if (creating_guide_) {
      creating_guide_pos_ = (creating_guide_orientation_ == Guide::Horizontal) ? image_y : video_x;
      update();
      return;
    }

    // Hover cursor for guides
    if (!dragging && olive::CurrentConfig.show_guides && !olive::CurrentConfig.lock_guides) {
      bool hit_mirror = false;
      int idx = find_guide_at(video_x, image_y, &hit_mirror);
      if (idx >= 0) {
        hovered_guide_index_ = idx;
        hovered_mirror_side_ = hit_mirror;

        if (!hasFocus()) {
          QWidget* fw = QApplication::focusWidget();
          if (fw == nullptr || !fw->inherits("QLineEdit")) setFocus();
        }
        const Guide& g = viewer->seq->guides[idx];
        setCursor(g.orientation == Guide::Horizontal ? Qt::SizeVerCursor : Qt::SizeHorCursor);
        return;
      }
    }
    hovered_guide_index_ = -1;
  }

  if (dragging) {
    if (waveform) {
      seek_from_click(qRound(event->position().x()));
    } else if (event->buttons() & Qt::MiddleButton || panel_timeline->tool == TIMELINE_TOOL_HAND) {
      container->dragScrollMove(event->position().toPoint() * container->zoom);
    } else if (event->buttons() & Qt::LeftButton) {
      if (gizmos == nullptr) {
        viewer->initiate_drag(olive::timeline::kImportBoth);
        dragging = false;
      } else {
        move_gizmos(event, false);
      }
    }
  } else {
    EffectGizmo* g = get_gizmo_from_mouse(event->position().toPoint().x(), event->position().toPoint().y());
    if (g != nullptr) {
      if (g->get_cursor() > -1) {
        setCursor(static_cast<enum Qt::CursorShape>(g->get_cursor()));
      }
    }
  }
}

void ViewerWidget::mouseReleaseEvent(QMouseEvent* event) {
  if (dragging_guide_index_ >= 0) {
    if (viewer->seq != nullptr) {
      Guide& g = viewer->seq->guides[dragging_guide_index_];
      int max_val = (g.orientation == Guide::Horizontal) ? viewer->seq->height : viewer->seq->width;
      if (g.position < 0 || g.position > max_val) {
        // Dragged off-screen → restore then push delete action
        g.position = dragging_guide_old_pos_;
        olive::UndoStack.push(new DeleteGuideAction(viewer->seq.get(), dragging_guide_index_));
      } else if (g.position != dragging_guide_old_pos_) {
        // Moved to a new position → restore then push move action
        int new_pos = g.position;
        g.position = dragging_guide_old_pos_;
        olive::UndoStack.push(
            new MoveGuideAction(viewer->seq.get(), dragging_guide_index_, dragging_guide_old_pos_, new_pos));
      }
    }
    dragging_guide_index_ = -1;
    dragging_mirror_side_ = false;
    dragging = false;
    update();
    return;
  }

  if (creating_guide_) {
    finish_guide_creation();
    dragging = false;
    return;
  }

  if (dragging && gizmos != nullptr && event->button() == Qt::LeftButton &&
      panel_timeline->tool != TIMELINE_TOOL_HAND) {
    move_gizmos(event, true);
  }
  dragging = false;
}

void ViewerWidget::wheelEvent(QWheelEvent* event) { container->parseWheelEvent(event); }

void ViewerWidget::close_window() { window->hide(); }

void ViewerWidget::wait_until_render_is_paused() { renderer->wait_until_paused(); }

void ViewerWidget::draw_waveform_func(QPainter& p) {
  if (viewer->seq->using_workarea) {
    int in_x = getScreenPointFromFrame(waveform_zoom, viewer->seq->workarea_in) - waveform_scroll;
    int out_x = getScreenPointFromFrame(waveform_zoom, viewer->seq->workarea_out) - waveform_scroll;

    p.fillRect(QRect(in_x, 0, out_x - in_x, height()), QColor(255, 255, 255, 64));
    p.setPen(Qt::white);
    p.drawLine(in_x, 0, in_x, height());
    p.drawLine(out_x, 0, out_x, height());
  }
  QRect wr = rect();
  wr.setX(wr.x() - waveform_scroll);

  p.setPen(Qt::green);
  draw_waveform(waveform_clip, waveform_ms, waveform_clip->timeline_out(), &p, wr, waveform_scroll,
                width() + waveform_scroll, waveform_zoom);
  p.setPen(Qt::red);
  int playhead_x = getScreenPointFromFrame(waveform_zoom, viewer->seq->playhead) - waveform_scroll;
  p.drawLine(playhead_x, 0, playhead_x, height());
}

void ViewerWidget::draw_title_safe_area(QPainter& p) {
  int w = width();
  int h = height();

  // Compute effective area (respecting custom aspect ratio)
  double viewportAr = double(w) / double(h);
  double areaW = w;
  double areaH = h;
  double offsetX = 0;
  double offsetY = 0;

  if (olive::CurrentConfig.use_custom_title_safe_ratio && olive::CurrentConfig.custom_title_safe_ratio > 0) {
    if (olive::CurrentConfig.custom_title_safe_ratio > viewportAr) {
      areaH = w / olive::CurrentConfig.custom_title_safe_ratio;
      offsetY = (h - areaH) * 0.5;
    } else {
      areaW = h * olive::CurrentConfig.custom_title_safe_ratio;
      offsetX = (w - areaW) * 0.5;
    }
  }

  double cx = offsetX + areaW * 0.5;
  double cy = offsetY + areaH * 0.5;

  QPen pen(QColor(168, 168, 168));
  pen.setWidth(1);
  p.setPen(pen);

  // action safe (90%)
  double a = 0.45;
  p.drawRect(QRectF(cx - areaW * a, cy - areaH * a, areaW * a * 2, areaH * a * 2));

  // title safe (80%)
  double t = 0.40;
  p.drawRect(QRectF(cx - areaW * t, cy - areaH * t, areaW * t * 2, areaH * t * 2));

  // center markers
  double tick = areaW * 0.075;
  double tickV = areaH * 0.075;
  p.drawLine(QPointF(cx - areaW * a, cy), QPointF(cx - areaW * a + tick, cy));
  p.drawLine(QPointF(cx + areaW * a, cy), QPointF(cx + areaW * a - tick, cy));
  p.drawLine(QPointF(cx, cy - areaH * a), QPointF(cx, cy - areaH * a + tickV));
  p.drawLine(QPointF(cx, cy + areaH * a), QPointF(cx, cy + areaH * a - tickV));

  // center cross
  double cross = qMin(areaW, areaH) * 0.05;
  p.drawLine(QPointF(cx - cross, cy), QPointF(cx + cross, cy));
  p.drawLine(QPointF(cx, cy - cross), QPointF(cx, cy + cross));
}

void ViewerWidget::draw_guides(QPainter& p) {
  if (viewer->seq == nullptr || !olive::CurrentConfig.show_guides) return;

  // Compute image→widget coordinate transform
  // container->zoom maps image-space pixels to widget-space pixels directly
  double scale = container->zoom;
  double tx = (viewer->seq->width - (width() / container->zoom)) * x_scroll;
  double ty = (viewer->seq->height - (height() / container->zoom)) * (1.0 - y_scroll);

  auto toWidget = [&](double ix, double iy) -> QPointF {
    // image-space (0=top-left, Y down) → widget-space
    return QPointF((ix - tx) * scale, (iy - ty) * scale);
  };

  QPen guidePen(QColor(0, 204, 204, 178));
  guidePen.setWidth(1);
  p.setPen(guidePen);

  for (const Guide& g : viewer->seq->guides) {
    if (g.orientation == Guide::Horizontal) {
      QPointF left = toWidget(0, g.position);
      QPointF right = toWidget(viewer->seq->width, g.position);
      p.drawLine(left, right);
      if (g.mirror) {
        int mirror_pos = viewer->seq->height - g.position;
        p.drawLine(toWidget(0, mirror_pos), toWidget(viewer->seq->width, mirror_pos));
      }
    } else {
      QPointF top = toWidget(g.position, 0);
      QPointF bottom = toWidget(g.position, viewer->seq->height);
      p.drawLine(top, bottom);
      if (g.mirror) {
        int mirror_pos = viewer->seq->width - g.position;
        p.drawLine(toWidget(mirror_pos, 0), toWidget(mirror_pos, viewer->seq->height));
      }
    }
  }

  // Draw preview line for guide being created
  if (creating_guide_) {
    QPen createPen(QColor(255, 165, 0, 178));
    createPen.setWidth(1);
    p.setPen(createPen);
    if (creating_guide_orientation_ == Guide::Horizontal) {
      p.drawLine(toWidget(0, creating_guide_pos_), toWidget(viewer->seq->width, creating_guide_pos_));
    } else {
      p.drawLine(toWidget(creating_guide_pos_, 0), toWidget(creating_guide_pos_, viewer->seq->height));
    }
  }
}

void ViewerWidget::draw_gizmos(QPainter& p) {
  double dot_size_px = GIZMO_DOT_SIZE;
  double target_size_px = GIZMO_TARGET_SIZE;

  // container->zoom maps image-space pixels to widget-space pixels directly
  double scale = container->zoom;
  double tx = (viewer->seq->width - (width() / container->zoom)) * x_scroll;
  double ty = (viewer->seq->height - (height() / container->zoom)) * (1.0 - y_scroll);

  // Gizmo screen_pos is in image-space with Y=0 at bottom. Convert to widget-space (Y=0 at top).
  auto toWidget = [&](double ix, double iy) -> QPointF {
    double iy_topdown = viewer->seq->height - iy;
    return QPointF((ix - tx) * scale, (iy_topdown - ty) * scale);
  };

  p.setRenderHint(QPainter::Antialiasing, true);

  for (int j = 0; j < gizmos->gizmo_count(); j++) {
    EffectGizmo* g = gizmos->gizmo(j);
    QPen pen(g->color);
    pen.setWidth(1);
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);

    switch (g->get_type()) {
      case GIZMO_TYPE_DOT: {
        QPointF center = toWidget(g->screen_pos[0].x(), g->screen_pos[0].y());
        p.setBrush(g->color);
        p.drawRect(QRectF(center.x() - dot_size_px, center.y() - dot_size_px,
                          dot_size_px * 2, dot_size_px * 2));
        p.setBrush(Qt::NoBrush);
        break;
      }
      case GIZMO_TYPE_POLY: {
        QPolygonF poly;
        for (int k = 0; k < g->get_point_count(); k++) {
          poly << toWidget(g->screen_pos[k].x(), g->screen_pos[k].y());
        }
        poly << poly.first(); // close
        p.drawPolyline(poly);
        break;
      }
      case GIZMO_TYPE_TARGET: {
        QPointF center = toWidget(g->screen_pos[0].x(), g->screen_pos[0].y());
        double ts = target_size_px;
        p.drawRect(QRectF(center.x() - ts, center.y() - ts, ts * 2, ts * 2));
        p.drawLine(QPointF(center.x() - ts, center.y()), QPointF(center.x() + ts, center.y()));
        p.drawLine(QPointF(center.x(), center.y() - ts), QPointF(center.x(), center.y() + ts));
        break;
      }
    }
  }

  p.end();
}

int ViewerWidget::find_guide_at(int video_x, int video_y, bool* hit_mirror) const {
  if (viewer->seq == nullptr) return -1;
  int threshold = qMax(3, int(5.0 * viewer->seq->width / width()));
  for (int i = 0; i < viewer->seq->guides.size(); i++) {
    const Guide& g = viewer->seq->guides[i];
    if (g.orientation == Guide::Horizontal) {
      if (qAbs(video_y - g.position) <= threshold) {
        if (hit_mirror) *hit_mirror = false;
        return i;
      }
      if (g.mirror) {
        int mirror_pos = viewer->seq->height - g.position;
        if (qAbs(video_y - mirror_pos) <= threshold) {
          if (hit_mirror) *hit_mirror = true;
          return i;
        }
      }
    } else {
      if (qAbs(video_x - g.position) <= threshold) {
        if (hit_mirror) *hit_mirror = false;
        return i;
      }
      if (g.mirror) {
        int mirror_pos = viewer->seq->width - g.position;
        if (qAbs(video_x - mirror_pos) <= threshold) {
          if (hit_mirror) *hit_mirror = true;
          return i;
        }
      }
    }
  }
  return -1;
}

void ViewerWidget::guide_action_delete() {
  if (hovered_guide_index_ < 0 || viewer->seq == nullptr) return;
  olive::UndoStack.push(new DeleteGuideAction(viewer->seq.get(), hovered_guide_index_));
  hovered_guide_index_ = -1;
  update();
}

void ViewerWidget::guide_action_mirror() {
  if (hovered_guide_index_ < 0 || viewer->seq == nullptr) return;
  const Guide& g = viewer->seq->guides[hovered_guide_index_];
  bool was_mirrored = g.mirror;
  if (was_mirrored && hovered_mirror_side_) {
    int dim = (g.orientation == Guide::Horizontal) ? viewer->seq->height : viewer->seq->width;
    int mirror_pos = dim - g.position;
    auto* combo = new ComboAction();
    combo->append(new MoveGuideAction(viewer->seq.get(), hovered_guide_index_, g.position, mirror_pos));
    combo->append(new SetGuideMirrorAction(viewer->seq.get(), hovered_guide_index_, false));
    olive::UndoStack.push(combo);
  } else {
    olive::UndoStack.push(new SetGuideMirrorAction(viewer->seq.get(), hovered_guide_index_, !was_mirrored));
  }
  update();
}

void ViewerWidget::show_guide_context_menu(int guide_index, const QPoint& global_pos, bool on_mirror) {
  QMenu menu;
  QAction* set_value = menu.addAction(tr("Set Value..."));
  QAction* mirror_action = menu.addAction(tr("Mirror") + "\t" + guide_mirror_action_->shortcut().toString());
  mirror_action->setCheckable(true);
  mirror_action->setChecked(viewer->seq->guides[guide_index].mirror);
  QAction* delete_guide = menu.addAction(tr("Delete Guide") + "\t" + guide_delete_action_->shortcut().toString());

  QAction* selected = menu.exec(global_pos);
  if (selected == set_value) {
    const Guide& g = viewer->seq->guides[guide_index];
    int dim = (g.orientation == Guide::Horizontal) ? viewer->seq->height : viewer->seq->width;
    int display_pos = (on_mirror && g.mirror) ? (dim - g.position) : g.position;
    bool ok;
    int val = QInputDialog::getInt(this, tr("Set Guide Value"), tr("Position (pixels):"), display_pos, 0, dim, 1, &ok);
    if (ok && val != display_pos) {
      int new_primary = (on_mirror && g.mirror) ? (dim - val) : val;
      olive::UndoStack.push(new MoveGuideAction(viewer->seq.get(), guide_index, g.position, new_primary));
      update();
    }
  } else if (selected == mirror_action) {
    const Guide& g = viewer->seq->guides[guide_index];
    bool was_mirrored = g.mirror;
    if (was_mirrored && on_mirror) {
      // Unchecking mirror from the mirror side: move primary to mirror position, then disable mirror
      int dim = (g.orientation == Guide::Horizontal) ? viewer->seq->height : viewer->seq->width;
      int mirror_pos = dim - g.position;
      auto* combo = new ComboAction();
      combo->append(new MoveGuideAction(viewer->seq.get(), guide_index, g.position, mirror_pos));
      combo->append(new SetGuideMirrorAction(viewer->seq.get(), guide_index, false));
      olive::UndoStack.push(combo);
    } else {
      olive::UndoStack.push(new SetGuideMirrorAction(viewer->seq.get(), guide_index, !was_mirrored));
    }
    update();
  } else if (selected == delete_guide) {
    olive::UndoStack.push(new DeleteGuideAction(viewer->seq.get(), guide_index));
    update();
  }
}

void ViewerWidget::start_guide_creation(Guide::Orientation orientation, int video_pos) {
  creating_guide_ = true;
  creating_guide_orientation_ = orientation;
  creating_guide_pos_ = video_pos;
  update();
}

void ViewerWidget::update_guide_creation(int video_pos) {
  creating_guide_pos_ = video_pos;
  update();
}

void ViewerWidget::finish_guide_creation() {
  if (creating_guide_ && viewer->seq != nullptr) {
    int max_val = (creating_guide_orientation_ == Guide::Horizontal) ? viewer->seq->height : viewer->seq->width;
    if (creating_guide_pos_ >= 0 && creating_guide_pos_ <= max_val) {
      Guide g;
      g.orientation = creating_guide_orientation_;
      g.position = creating_guide_pos_;
      olive::UndoStack.push(new AddGuideAction(viewer->seq.get(), g));
    }
  }
  creating_guide_ = false;
  update();
}

void ViewerWidget::cancel_guide_creation() {
  creating_guide_ = false;
  update();
}

void ViewerWidget::render(QRhiCommandBuffer *cb) {
  if (waveform) {
    const QColor clearColor(0, 0, 0, 255);
    cb->beginPass(renderTarget(), clearColor, {1.0f, 0});
    cb->endPass();
    return;
  }

  // --- CPU bridge: get frame pixels from RenderThread ---
  QMutex *frame_lock = renderer->get_texture_mutex();
  frame_lock->lock();

  const char* frame_data = renderer->get_frame_data();
  int fw = renderer->get_frame_width();
  int fh = renderer->get_frame_height();
  bool has_frame = (frame_data != nullptr && fw > 0 && fh > 0 && viewer->seq != nullptr);

  QRhiResourceUpdateBatch *u = rhi_->nextResourceUpdateBatch();
  if (has_frame) {
    if (fw != cached_tex_w_ || fh != cached_tex_h_) {
      delete frame_tex_;
      frame_tex_ = rhi_->newTexture(QRhiTexture::RGBA8, QSize(fw, fh));
      frame_tex_->create();

      srb_->setBindings({
          QRhiShaderResourceBinding::uniformBuffer(0, QRhiShaderResourceBinding::VertexStage, vert_ubuf_),
          QRhiShaderResourceBinding::uniformBuffer(1, QRhiShaderResourceBinding::FragmentStage, frag_ubuf_),
          QRhiShaderResourceBinding::sampledTexture(2, QRhiShaderResourceBinding::FragmentStage, frame_tex_, sampler_),
      });
      srb_->create();

      pipeline_->setShaderResourceBindings(srb_);
      pipeline_->setRenderPassDescriptor(renderTarget()->renderPassDescriptor());
      pipeline_->create();

      cached_tex_w_ = fw;
      cached_tex_h_ = fh;
    }

    QRhiTextureSubresourceUploadDescription desc(reinterpret_cast<const uchar*>(frame_data), fw * fh * 4);
    u->uploadTexture(frame_tex_, QRhiTextureUploadDescription({QRhiTextureUploadEntry(0, 0, desc)}));
  }

  // Update gizmo tracking — detect stale gizmo (user selected a different clip while paused)
  gizmos = renderer->gizmos;
  if (viewer->seq != nullptr && gizmos != viewer->seq->GetSelectedGizmo()) {
    gizmos = nullptr;
    QTimer::singleShot(0, this, &ViewerWidget::frame_update);
  }

  // Pass frame to fullscreen window (copies data)
  if (has_frame && window->isVisible()) {
    window->set_frame(frame_data, fw, fh);
  }

  frame_lock->unlock();
  // --- end CPU bridge ---

  // Compute zoom/scroll quad in NDC
  float zoom_left = -1.0f, zoom_right = 1.0f, zoom_bottom = -1.0f, zoom_top = 1.0f;
  if (viewer->seq != nullptr) {
    double zoom_factor = container->zoom / (double(width()) / double(viewer->seq->width));
    double zoom_size = (zoom_factor * 2.0) - 2.0;
    zoom_left = float(-zoom_size * x_scroll - 1.0);
    zoom_right = float(zoom_size * (1.0 - x_scroll) + 1.0);
    zoom_bottom = float(-zoom_size * (1.0 - y_scroll) - 1.0);
    zoom_top = float(zoom_size * y_scroll + 1.0);
  }

  // TriangleStrip: BL, TL, BR, TR — texcoords Y-flipped (glReadPixels is bottom-to-top)
  float vertexData[] = {
      zoom_left, zoom_bottom, 0.0f, 1.0f,
      zoom_left, zoom_top, 0.0f, 0.0f,
      zoom_right, zoom_bottom, 1.0f, 1.0f,
      zoom_right, zoom_top, 1.0f, 0.0f,
  };

  QMatrix4x4 mvp = rhi_->clipSpaceCorrMatrix();
  mvp.ortho(-1.0f, 1.0f, -1.0f, 1.0f, -1.0f, 1.0f);

  float colorMult[] = {1.0f, 1.0f, 1.0f, 1.0f};

  u->updateDynamicBuffer(vbuf_, 0, sizeof(vertexData), vertexData);
  u->updateDynamicBuffer(vert_ubuf_, 0, 64, mvp.constData());
  u->updateDynamicBuffer(frag_ubuf_, 0, 16, colorMult);

  const QColor clearColor(0, 0, 0, 255);
  cb->beginPass(renderTarget(), clearColor, {1.0f, 0}, u);

  if (has_frame) {
    const QSize outputSize = renderTarget()->pixelSize();
    cb->setGraphicsPipeline(pipeline_);
    cb->setViewport({0, 0, float(outputSize.width()), float(outputSize.height())});
    cb->setShaderResources(srb_);
    const QRhiCommandBuffer::VertexInput vbufBinding(vbuf_, 0);
    cb->setVertexInput(0, 1, &vbufBinding);
    cb->draw(4);
  }

  cb->endPass();

  // Retry render if texture failed
  if (renderer->did_texture_fail() && !viewer->playing) {
    renderer->start_render(viewer->seq.get(), viewer->get_playback_speed());
  }

  // Schedule overlay repaint
  if (overlay_) overlay_->update();
}

// --- ViewerOverlay ---

ViewerOverlay::ViewerOverlay(ViewerWidget* vw, QWidget* parent) : QWidget(parent), vw_(vw) {
  setAttribute(Qt::WA_TranslucentBackground);
  setAttribute(Qt::WA_TransparentForMouseEvents);
}

void ViewerOverlay::paintEvent(QPaintEvent*) {
  QPainter p(this);
  if (vw_->waveform) {
    vw_->draw_waveform_func(p);
  } else if (vw_->viewer->seq != nullptr) {
    if (olive::CurrentConfig.show_title_safe_area) {
      vw_->draw_title_safe_area(p);
    }
    vw_->draw_guides(p);
    if (vw_->gizmos != nullptr) {
      vw_->draw_gizmos(p);
    }
  }
}
