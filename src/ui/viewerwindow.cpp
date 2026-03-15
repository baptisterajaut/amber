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

#include "viewerwindow.h"

#include <QApplication>
#include <QFile>
#include <QKeyEvent>
#include <QLabel>
#include <QMenuBar>
#include <QShortcut>
#include <QWindow>

#if QT_CONFIG(vulkan)
#include <QVulkanInstance>
#endif
#if QT_CONFIG(vulkan) && defined(VK_VERSION_1_0)
#define AMBER_HAS_VULKAN 1
#endif

#include "global/config.h"
#include "mainwindow.h"

ViewerWindow::ViewerWindow(QWidget* parent) : QRhiWidget(parent) {
  setWindowFlags(Qt::Window);

  switch (olive::CurrentRuntimeConfig.rhi_backend) {
    case RhiBackend::Vulkan: setApi(Api::Vulkan); break;
    case RhiBackend::Metal: setApi(Api::Metal); break;
    case RhiBackend::D3D12: setApi(Api::Direct3D12); break;
    case RhiBackend::D3D11: setApi(Api::Direct3D11); break;
    default: setApi(Api::OpenGL); break;
  }

#if AMBER_HAS_VULKAN
  if (olive::CurrentRuntimeConfig.rhi_backend == RhiBackend::Vulkan) {
    auto* vi = static_cast<QVulkanInstance*>(olive::CurrentRuntimeConfig.vulkan_instance);
    if (vi) {
      winId();
      if (windowHandle()) {
        windowHandle()->setVulkanInstance(vi);
      }
    }
  }
#endif

  setMouseTracking(true);

  fullscreen_msg_timer.setInterval(2000);
  connect(&fullscreen_msg_timer, &QTimer::timeout, this, &ViewerWindow::fullscreen_msg_timeout);

  // Overlay label for "Exit Fullscreen" message (replaces QPainter which doesn't work on QRhiWidget)
  fullscreen_msg_label_ = new QLabel(tr("Exit Fullscreen"), this);
  fullscreen_msg_label_->setAlignment(Qt::AlignCenter);
  fullscreen_msg_label_->setStyleSheet(
      "QLabel { color: white; background-color: rgba(0, 0, 0, 128);"
      " font-size: 24pt; padding: 8px; }");
  fullscreen_msg_label_->setVisible(false);
  fullscreen_msg_label_->adjustSize();
}

void ViewerWindow::set_frame(const char* data, int w, int h) {
  int bytes = w * h * 4;
  if (frame_data_.size() != bytes) frame_data_.resize(bytes);
  memcpy(frame_data_.data(), data, bytes);
  frame_w_ = w;
  frame_h_ = h;
  ar = double(w) / double(h);
  update();
}

void ViewerWindow::shortcut_copier(QVector<QShortcut*>& shortcuts, QMenu* menu) {
  QList<QAction*> menu_action = menu->actions();
  for (auto i : menu_action) {
    if (i->menu() != nullptr) {
      shortcut_copier(shortcuts, i->menu());
    } else if (!i->isSeparator() && !i->shortcut().isEmpty()) {
      QShortcut* sc = new QShortcut(this);
      sc->setKey(i->shortcut());
      connect(sc, &QShortcut::activated, i, &QAction::trigger);
      shortcuts.append(sc);
    }
  }
}

void ViewerWindow::showEvent(QShowEvent*) {
  for (auto shortcut : shortcuts_) {
    delete shortcut;
  }
  shortcuts_.clear();

  QList<QAction*> menubar_actions = olive::MainWindow->menuBar()->actions();
  for (auto menubar_action : menubar_actions) {
    shortcut_copier(shortcuts_, menubar_action->menu());
  }
}

void ViewerWindow::keyPressEvent(QKeyEvent* e) {
  if (e->key() == Qt::Key_Escape) {
    hide();
  }
}

void ViewerWindow::mousePressEvent(QMouseEvent* e) {
  if (fullscreen_msg_label_->isVisible() && fullscreen_msg_label_->geometry().contains(e->position().toPoint())) {
    hide();
  }
}

void ViewerWindow::mouseMoveEvent(QMouseEvent*) {
  fullscreen_msg_timer.start();
  if (!fullscreen_msg_label_->isVisible()) {
    fullscreen_msg_label_->setVisible(true);
    position_fullscreen_msg();
  }
}

void ViewerWindow::resizeEvent(QResizeEvent* e) {
  QRhiWidget::resizeEvent(e);
  if (fullscreen_msg_label_->isVisible()) {
    position_fullscreen_msg();
  }
}

void ViewerWindow::position_fullscreen_msg() {
  fullscreen_msg_label_->adjustSize();
  int x = (width() - fullscreen_msg_label_->width()) / 2;
  int y = fullscreen_msg_label_->height();
  fullscreen_msg_label_->move(x, y);
}

void ViewerWindow::initialize(QRhiCommandBuffer* cb) {
  Q_UNUSED(cb)

  if (rhi_ != rhi()) {
    releaseResources();
  }
  if (rhi_initialized_) return;
  rhi_ = rhi();

  QFile vsFile(QStringLiteral(":/shaders/common.vert.qsb"));
  if (!vsFile.open(QIODevice::ReadOnly)) {
    qCritical() << "ViewerWindow: failed to load vertex shader";
  }
  QShader vs = QShader::fromSerialized(vsFile.readAll());

  QFile fsFile(QStringLiteral(":/shaders/passthrough.frag.qsb"));
  if (!fsFile.open(QIODevice::ReadOnly)) {
    qCritical() << "ViewerWindow: failed to load fragment shader";
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

  // Blend: alpha-composite RGB, keep alpha=1 from clear (Wayland fix)
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

void ViewerWindow::render(QRhiCommandBuffer* cb) {
  bool has_frame = !frame_data_.isEmpty();

  // Upload CPU frame data as QRhiTexture
  QRhiResourceUpdateBatch* u = rhi_->nextResourceUpdateBatch();
  if (has_frame) {
    if (frame_w_ != cached_tex_w_ || frame_h_ != cached_tex_h_) {
      delete frame_tex_;
      frame_tex_ = rhi_->newTexture(QRhiTexture::RGBA8, QSize(frame_w_, frame_h_));
      frame_tex_->create();

      srb_->setBindings({
          QRhiShaderResourceBinding::uniformBuffer(0, QRhiShaderResourceBinding::VertexStage, vert_ubuf_),
          QRhiShaderResourceBinding::uniformBuffer(1, QRhiShaderResourceBinding::FragmentStage, frag_ubuf_),
          QRhiShaderResourceBinding::sampledTexture(2, QRhiShaderResourceBinding::FragmentStage, frame_tex_, sampler_),
      });
      srb_->create();

      cached_tex_w_ = frame_w_;
      cached_tex_h_ = frame_h_;
    }
    QRhiTextureSubresourceUploadDescription desc(frame_data_.constData(), frame_data_.size());
    desc.setSourceSize(QSize(frame_w_, frame_h_));
    u->uploadTexture(frame_tex_, QRhiTextureUploadDescription({QRhiTextureUploadEntry(0, 0, desc)}));
  }

  // Compute letterbox coordinates in [0,1] range
  float top = 0.0f, left = 0.0f, right = 1.0f, bottom = 1.0f;
  if (has_frame && width() > 0 && height() > 0) {
    double widget_ar = double(width()) / double(height());
    if (widget_ar > ar) {
      double w = ar / widget_ar;
      left = float((1.0 - w) * 0.5);
      right = float(left + w);
    } else {
      double h = 1.0 / ar * widget_ar;
      top = float((1.0 - h) * 0.5);
      bottom = float(top + h);
    }
  }

  // TriangleStrip: BL, TL, BR, TR — texcoords Y-flipped (glReadPixels is bottom-to-top)
  float vertexData[] = {
      left, top, 0.0f, 1.0f,
      left, bottom, 0.0f, 0.0f,
      right, top, 1.0f, 1.0f,
      right, bottom, 1.0f, 0.0f,
  };

  QMatrix4x4 mvp = rhi_->clipSpaceCorrMatrix();
  mvp.ortho(0.0f, 1.0f, 0.0f, 1.0f, -1.0f, 1.0f);

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
}

void ViewerWindow::releaseResources() {
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

void ViewerWindow::fullscreen_msg_timeout() {
  fullscreen_msg_timer.stop();
  if (fullscreen_msg_label_->isVisible()) {
    fullscreen_msg_label_->setVisible(false);
  }
}
