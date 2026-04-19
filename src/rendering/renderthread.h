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

#ifndef RENDERTHREAD_H
#define RENDERTHREAD_H

#include <rhi/qrhi.h>
#include <rhi/qshader.h>
#include <QMutex>
#include <QOffscreenSurface>
#include <QThread>
#include <QWaitCondition>
#include <atomic>

#include "effects/effect.h"
#include "engine/sequence.h"

class RenderThread : public QThread {
  Q_OBJECT
 public:
  RenderThread();
  ~RenderThread() override;
  void run() override;

  // Pre-set a fallback surface for the OpenGL backend.  Must be created on
  // the GUI thread (via QRhiGles2InitParams::newFallbackSurface()) and passed
  // here BEFORE start().  RenderThread does NOT take ownership — the caller
  // must ensure the surface outlives the RenderThread and delete it on the
  // GUI thread after cancel()/wait().
  void setGlFallbackSurface(QOffscreenSurface* surface);

  QMutex* get_texture_mutex(int buffer_index);

  // CPU bridge: pixel data read back after compositing
  const char* get_frame_data(int buffer_index) const;
  int get_frame_width() const;
  int get_frame_height() const;

  Effect* gizmos{nullptr};
  void paint();
  // Returns the current front buffer index (snapshot of the atomic switcher).
  int front_buffer_index() const;

  void start_render(Sequence* s, int playback_speed, const QString& save = nullptr, void* pixels = nullptr,
                    int pixel_linesize = 0, int idivider = 0, bool scrubbing = false);
  bool did_texture_fail();
  void cancel();
  void wait_until_paused();

  // Queue a QRhi resource for deferred deletion on the render thread.
  // Safe to call from any thread.
  static void DeferRhiResourceDeletion(QRhiResource* res);
  static void DeferRhiResourceDeletion(const QVector<QRhiResource*>& resources);

 public slots:
  void delete_ctx();
 signals:
  void ready();
  void frame_save_failed(const QString& path);

 private:
  static QMutex deferred_delete_mutex_;
  static QVector<QRhiResource*> deferred_delete_queue_;
  void drainDeferredDeletes();

  void delete_buffers();

  // RHI initialization helpers
  bool try_create_rhi();
  void init_rhi_resources();
  void ensure_render_buffers();

  // RHI resources
  QRhi* rhi_{nullptr};
  QOffscreenSurface* fallbackSurface_{nullptr};
  bool owns_fallback_surface_{false};  // true when created internally (legacy path)

  // Core shaders loaded from QRC .qsb files
  QShader passthroughVert_;
  QShader passthroughFrag_;
  QShader blendingFrag_;
  QShader premultiplyFrag_;
  QShader yuvFrag_;

  QRhiSampler* sampler_{nullptr};

  // Main compositing target (double-buffered)
  QRhiTexture* front_tex_[2] = {};
  QRhiTextureRenderTarget* front_rt_[2] = {};        // PreserveColorContents (for multi-clip compositing)
  QRhiTextureRenderTarget* front_rt_clear_[2] = {};  // No preserve (for initial clear)
  QRhiRenderPassDescriptor* front_rpd_{nullptr};
  QRhiRenderPassDescriptor* front_clear_rpd_{nullptr};

  QMutex front_mutex1;
  QMutex front_mutex2;
  std::atomic<bool> front_buffer_switcher;

  // Backend target (single — used for clip→main compositing)
  QRhiTexture* back_tex_{nullptr};
  QRhiTextureRenderTarget* back_rt_{nullptr};
  QRhiRenderPassDescriptor* back_rpd_{nullptr};

  QWaitCondition wait_cond_;
  QMutex wait_lock_;

  QWaitCondition main_thread_wait_cond_;
  QMutex main_thread_lock_;

  Sequence* seq{nullptr};
  int playback_speed_;
  int tex_width{-1};
  int tex_height{-1};
  int divider_{1};
  QAtomicInt queued;
  bool texture_failed{false};
  bool scrubbing_{false};
  bool running{true};
  QString save_fn;
  void* pixel_buffer;
  int pixel_buffer_linesize;

  // CPU bridge: double-buffered pixel readback
  QByteArray cpu_frame_[2];
};

#endif  // RENDERTHREAD_H
