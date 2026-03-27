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

#include "renderthread.h"

#include <QApplication>
#include <QImage>
#include <QDateTime>
#include <QDebug>
#include <QFile>
// Qt may set QT_CONFIG(vulkan) even without a real Vulkan SDK (e.g. macOS
// Homebrew Qt with MoltenVK headers but no SDK on the build machine).
// After including <QVulkanInstance>, check for VK_VERSION_1_0 to know if
// the real vulkan.h was found — without it, QVulkanInstance is stub-only.
#if QT_CONFIG(vulkan)
#include <QVulkanInstance>
#endif
#if QT_CONFIG(vulkan) && defined(VK_VERSION_1_0)
#define AMBER_HAS_VULKAN 1
#endif

#include "global/config.h"
#include "rendering/renderfunctions.h"
#include "engine/sequence.h"

static QShader loadQsb(const QString& path) {
  QFile f(path);
  if (!f.open(QIODevice::ReadOnly)) {
    qCritical() << "Failed to load shader:" << path;
    return {};
  }
  QShader s = QShader::fromSerialized(f.readAll());
  if (!s.isValid()) {
    qCritical() << "Invalid .qsb shader:" << path;
  }
  return s;
}

QMutex RenderThread::deferred_delete_mutex_;
QVector<QRhiResource*> RenderThread::deferred_delete_queue_;

void RenderThread::DeferRhiResourceDeletion(QRhiResource* res) {
  if (res == nullptr) return;
  QMutexLocker lock(&deferred_delete_mutex_);
  deferred_delete_queue_.append(res);
}

void RenderThread::DeferRhiResourceDeletion(const QVector<QRhiResource*>& resources) {
  QMutexLocker lock(&deferred_delete_mutex_);
  for (auto* res : resources) {
    if (res) deferred_delete_queue_.append(res);
  }
}

void RenderThread::drainDeferredDeletes() {
  QMutexLocker lock(&deferred_delete_mutex_);
  qDeleteAll(deferred_delete_queue_);
  deferred_delete_queue_.clear();
}

RenderThread::RenderThread()
    : front_buffer_switcher(false), queued(false) {
}

RenderThread::~RenderThread() {}

void RenderThread::run() {
  wait_lock_.lock();

  while (running) {
    if (!queued) {
      wait_cond_.wait(&wait_lock_);
    }
    if (!running) {
      break;
    }
    queued = false;

    // Create QRhi if not yet initialized
    if (rhi_ == nullptr) {
      RhiBackend backend = amber::CurrentRuntimeConfig.rhi_backend;

      switch (backend) {
#if AMBER_HAS_VULKAN
        case RhiBackend::Vulkan: {
          auto* vi = static_cast<QVulkanInstance*>(amber::CurrentRuntimeConfig.vulkan_instance);
          if (vi && vi->isValid()) {
            QRhiVulkanInitParams vkParams;
            vkParams.inst = vi;
            rhi_ = QRhi::create(QRhi::Vulkan, &vkParams);
          }
          if (!rhi_) qWarning() << "Vulkan QRhi creation failed, falling back to OpenGL";
          break;
        }
#endif
#if defined(Q_OS_MACOS) || defined(Q_OS_IOS)
        case RhiBackend::Metal: {
          QRhiMetalInitParams mtlParams;
          rhi_ = QRhi::create(QRhi::Metal, &mtlParams);
          if (!rhi_) qWarning() << "Metal QRhi creation failed, falling back to OpenGL";
          break;
        }
#endif
#if defined(Q_OS_WIN)
        case RhiBackend::D3D12: {
          QRhiD3D12InitParams d3d12Params;
          rhi_ = QRhi::create(QRhi::D3D12, &d3d12Params);
          if (!rhi_) {
            qWarning() << "D3D12 QRhi creation failed, trying D3D11";
            QRhiD3D11InitParams d3d11Params;
            rhi_ = QRhi::create(QRhi::D3D11, &d3d11Params);
            if (!rhi_) qWarning() << "D3D11 also failed, falling back to OpenGL";
          }
          break;
        }
        case RhiBackend::D3D11: {
          QRhiD3D11InitParams d3d11Params;
          rhi_ = QRhi::create(QRhi::D3D11, &d3d11Params);
          if (!rhi_) qWarning() << "D3D11 QRhi creation failed, falling back to OpenGL";
          break;
        }
#endif
        default:
          break;
      }

      // OpenGL fallback — create surface on this thread so the GL context
      // can be made current here (Qt requires thread affinity match).
      if (!rhi_) {
        if (!fallbackSurface_)
          fallbackSurface_ = QRhiGles2InitParams::newFallbackSurface();
        QRhiGles2InitParams glParams;
        glParams.fallbackSurface = fallbackSurface_;
        rhi_ = QRhi::create(QRhi::OpenGLES2, &glParams);
      }

      // Last resort: software Vulkan (llvmpipe) if OpenGL also failed
#if AMBER_HAS_VULKAN
      if (!rhi_ && amber::CurrentRuntimeConfig.vulkan_is_software) {
        auto* vi = static_cast<QVulkanInstance*>(amber::CurrentRuntimeConfig.vulkan_instance);
        if (vi && vi->isValid()) {
          qWarning() << "OpenGL failed, falling back to software Vulkan (llvmpipe)";
          QRhiVulkanInitParams vkParams;
          vkParams.inst = vi;
          rhi_ = QRhi::create(QRhi::Vulkan, &vkParams);
        }
      }
#endif

      if (!rhi_) {
        qCritical() << "Failed to create QRhi with any backend";
        continue;
      }
      qInfo() << "QRhi initialized, backend:" << rhi_->backendName()
              << "driver:" << rhi_->driverInfo().deviceName;

      // Load core shaders from QRC
      passthroughVert_ = loadQsb(QStringLiteral(":/shaders/common.vert.qsb"));
      passthroughFrag_ = loadQsb(QStringLiteral(":/shaders/passthrough.frag.qsb"));
      blendingFrag_ = loadQsb(QStringLiteral(":/shaders/blending.frag.qsb"));
      premultiplyFrag_ = loadQsb(QStringLiteral(":/shaders/premultiply.frag.qsb"));
      yuvFrag_ = loadQsb(QStringLiteral(":/shaders/yuv2rgb.frag.qsb"));

      // Sampler
      sampler_ = rhi_->newSampler(QRhiSampler::Linear, QRhiSampler::Linear, QRhiSampler::None,
                                   QRhiSampler::ClampToEdge, QRhiSampler::ClampToEdge);
      sampler_->create();
    }

    if (rhi_ != nullptr) {
      // Recreate buffers if sequence size changed
      if (seq->width != tex_width || seq->height != tex_height) {
        delete_buffers();
        tex_width = seq->width;
        tex_height = seq->height;
      }

      // Create front buffers (double-buffered compositing targets)
      if (front_tex_[0] == nullptr) {
        for (int i = 0; i < 2; i++) {
          front_tex_[i] = rhi_->newTexture(QRhiTexture::RGBA8, QSize(tex_width, tex_height),
                                           1, QRhiTexture::RenderTarget | QRhiTexture::UsedAsTransferSource);
          front_tex_[i]->create();

          // Compositing RT: preserves contents between clips
          front_rt_[i] = rhi_->newTextureRenderTarget(
              {front_tex_[i]}, QRhiTextureRenderTarget::PreserveColorContents);
          if (i == 0) {
            front_rpd_ = front_rt_[i]->newCompatibleRenderPassDescriptor();
          }
          front_rt_[i]->setRenderPassDescriptor(front_rpd_);
          front_rt_[i]->create();

          // Clear RT: same texture, no PreserveColorContents (beginPass actually clears)
          front_rt_clear_[i] = rhi_->newTextureRenderTarget({front_tex_[i]});
          if (i == 0) {
            front_clear_rpd_ = front_rt_clear_[i]->newCompatibleRenderPassDescriptor();
          }
          front_rt_clear_[i]->setRenderPassDescriptor(front_clear_rpd_);
          front_rt_clear_[i]->create();
        }
      }

      // Create back buffer (single — used for clip→main compositing)
      if (back_tex_ == nullptr) {
        back_tex_ = rhi_->newTexture(QRhiTexture::RGBA8, QSize(tex_width, tex_height),
                                     1, QRhiTexture::RenderTarget);
        back_tex_->create();
        back_rt_ = rhi_->newTextureRenderTarget({back_tex_});
        back_rpd_ = back_rt_->newCompatibleRenderPassDescriptor();
        back_rt_->setRenderPassDescriptor(back_rpd_);
        back_rt_->create();
      }

      // Draw frame
      paint();

      front_buffer_switcher = !front_buffer_switcher;

      emit ready();
    }
  }

  delete_ctx();

  wait_lock_.unlock();
}

QMutex* RenderThread::get_texture_mutex(int buffer_index) {
  return buffer_index ? &front_mutex2 : &front_mutex1;
}

int RenderThread::front_buffer_index() const {
  return front_buffer_switcher.load() ? 1 : 0;
}

const char* RenderThread::get_frame_data(int buffer_index) const {
  const QByteArray& buf = cpu_frame_[buffer_index];
  return buf.isEmpty() ? nullptr : buf.constData();
}

int RenderThread::get_frame_width() const { return tex_width; }
int RenderThread::get_frame_height() const { return tex_height; }

void RenderThread::paint() {
  int active_idx = front_buffer_switcher ? 0 : 1;

  QMutex& active_mutex = front_buffer_switcher ? front_mutex1 : front_mutex2;
  active_mutex.lock();

  QRhiCommandBuffer* cb = nullptr;
  if (rhi_->beginOffscreenFrame(&cb) != QRhi::FrameOpSuccess) {
    qWarning() << "beginOffscreenFrame failed, skipping frame";
    active_mutex.unlock();
    texture_failed = true;
    return;
  }

  // Clear the main target (use non-PreserveColorContents RT so clear actually happens)
  {
    QColor clearColor(0, 0, 0, 0);
    cb->beginPass(front_rt_clear_[active_idx], clearColor, {1.0f, 0});
    cb->endPass();
  }

  // Set up compose_sequence() parameters
  ComposeSequenceParams params;
  params.viewer = nullptr;
  params.rhi = rhi_;
  params.cb = cb;
  params.seq = seq;
  params.video = true;
  params.texture_failed = false;
  params.wait_for_mutexes = true;
  params.playback_speed = playback_speed_;
  params.scrubbing = scrubbing_;

  params.sampler = sampler_;

  params.passthroughVert = passthroughVert_;
  params.passthroughFrag = passthroughFrag_;
  params.blendingFrag = blendingFrag_;
  params.premultiplyFrag = premultiplyFrag_;
  params.yuvFrag = yuvFrag_;

  params.main_tex = front_tex_[active_idx];
  params.main_target = front_rt_[active_idx];
  params.main_rpd = front_rpd_;

  params.backend_tex1 = back_tex_;
  params.backend_target1 = back_rt_;
  params.backend_rpd = back_rpd_;

  gizmos = seq->GetSelectedGizmo();
  params.gizmos = gizmos;

  amber::rendering::compose_sequence(params);
  gizmos = params.gizmos;  // nulled by compose_sequence if clip wasn't rendered

  // CPU readback
  {
    QRhiReadbackResult readback;
    QRhiResourceUpdateBatch* u = rhi_->nextResourceUpdateBatch();
    u->readBackTexture(QRhiReadbackDescription(front_tex_[active_idx]), &readback);
    QColor clearColor(0, 0, 0, 0);
    cb->beginPass(front_rt_[active_idx], clearColor, {1.0f, 0});
    cb->endPass(u);

    rhi_->endOffscreenFrame();

    qDeleteAll(params.transientResources);
    params.transientResources.clear();

    // Drain deferred deletes from other threads (e.g. Clip::Close on main thread)
    drainDeferredDeletes();

    QByteArray& dst = cpu_frame_[active_idx];
    dst = readback.data;
  }

  active_mutex.unlock();

  texture_failed = params.texture_failed;

  // On OpenGL, framebuffer readback is bottom-to-top (row 0 = bottom).
  // The viewer compensates via clipSpaceCorrMatrix() when displaying,
  // but save-frame and export consume raw pixels and need top-to-bottom order.
  const bool need_yflip = rhi_->isYUpInFramebuffer();

  // Save frame if requested
  if (!save_fn.isEmpty()) {
    if (texture_failed) {
      queued = true;
    } else {
      QByteArray& data = cpu_frame_[active_idx];
      if (!data.isEmpty()) {
        QImage img(reinterpret_cast<const uchar*>(data.constData()), tex_width, tex_height,
                   QImage::Format_RGBA8888);
        if (need_yflip) {
#if QT_VERSION >= QT_VERSION_CHECK(6, 9, 0)
          img = img.flipped(Qt::Vertical);
#else
          img = img.mirrored(false, true);
#endif
        }
        img.save(save_fn);
      }
      save_fn = "";
    }
  }

  // Export pixel buffer if requested
  if (pixel_buffer != nullptr) {
    QByteArray& data = cpu_frame_[active_idx];
    if (!data.isEmpty()) {
      int copy_width = pixel_buffer_linesize == 0 ? tex_width : pixel_buffer_linesize;
      int row_bytes = copy_width * 4;
      if (need_yflip) {
        // Copy rows in reverse order: source bottom-to-top → dest top-to-bottom
        const char* src = data.constData();
        int src_stride = tex_width * 4;
        char* dst = static_cast<char*>(pixel_buffer);
        int copy_per_row = qMin(row_bytes, src_stride);
        for (int y = 0; y < tex_height; y++) {
          const char* src_row = src + static_cast<qsizetype>(tex_height - 1 - y) * src_stride;
          char* dst_row = dst + static_cast<qsizetype>(y) * row_bytes;
          memcpy(dst_row, src_row, copy_per_row);
          if (row_bytes > src_stride) {
            memset(dst_row + src_stride, 0, row_bytes - src_stride);
          }
        }
      } else {
        int src_stride = tex_width * 4;
        if (row_bytes > src_stride) {
          // Per-row copy with padding zero-fill when destination has padding
          const char* src = data.constData();
          char* dst = static_cast<char*>(pixel_buffer);
          for (int y = 0; y < tex_height; y++) {
            char* dst_row = dst + static_cast<qsizetype>(y) * row_bytes;
            memcpy(dst_row, src + static_cast<qsizetype>(y) * src_stride, src_stride);
            memset(dst_row + src_stride, 0, row_bytes - src_stride);
          }
        } else {
          int bytes = row_bytes * tex_height;
          memcpy(pixel_buffer, data.constData(), qMin(bytes, int(data.size())));
        }
      }
    }
    pixel_buffer = nullptr;
  }
}

void RenderThread::start_render(Sequence* s,
                                int playback_speed,
                                const QString& save,
                                void* pixels,
                                int pixel_linesize,
                                int idivider,
                                bool scrubbing) {
  Q_UNUSED(idivider)

  wait_lock_.lock();

  seq = s;

  playback_speed_ = playback_speed;
  scrubbing_ = scrubbing;

  // stall any dependent actions
  texture_failed = true;

  save_fn = save;
  pixel_buffer = pixels;
  pixel_buffer_linesize = pixel_linesize;

  queued = true;

  wait_cond_.wakeAll();

  wait_lock_.unlock();
}

bool RenderThread::did_texture_fail() { return texture_failed; }

void RenderThread::cancel() {
  running = false;
  wait_cond_.wakeAll();
  wait();
}

void RenderThread::wait_until_paused() {
  if (wait_lock_.tryLock()) {
    wait_lock_.unlock();
    return;
  } else {
    wait_lock_.lock();
    wait_lock_.unlock();
  }
}

void RenderThread::delete_buffers() {
  for (int i = 0; i < 2; i++) {
    delete front_rt_clear_[i];
    front_rt_clear_[i] = nullptr;
    delete front_rt_[i];
    front_rt_[i] = nullptr;
    delete front_tex_[i];
    front_tex_[i] = nullptr;
  }
  delete front_clear_rpd_;
  front_clear_rpd_ = nullptr;
  delete front_rpd_;
  front_rpd_ = nullptr;
  delete back_rt_;
  back_rt_ = nullptr;
  delete back_tex_;
  back_tex_ = nullptr;
  delete back_rpd_;
  back_rpd_ = nullptr;
}

void RenderThread::delete_ctx() {
  drainDeferredDeletes();  // Clean up anything queued before shutdown
  delete_buffers();

  delete sampler_;
  sampler_ = nullptr;

  delete rhi_;
  rhi_ = nullptr;

  delete fallbackSurface_;
  fallbackSurface_ = nullptr;
}
