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

#include <atomic>
#include <QThread>
#include <QMutex>
#include <QWaitCondition>
#include <QOffscreenSurface>
#include <rhi/qrhi.h>
#include <rhi/qshader.h>

#include "engine/sequence.h"
#include "effects/effect.h"

class RenderThread : public QThread {
  Q_OBJECT
public:
  RenderThread();
  ~RenderThread() override;
  void run() override;

  QMutex* get_texture_mutex(int buffer_index);

  // CPU bridge: pixel data read back after compositing
  const char* get_frame_data(int buffer_index) const;
  int get_frame_width() const;
  int get_frame_height() const;

  Effect* gizmos{nullptr};
  void paint();
  // Returns the current front buffer index (snapshot of the atomic switcher).
  int front_buffer_index() const;

  void start_render(Sequence* s,
                    int playback_speed,
                    const QString& save = nullptr,
                    void* pixels = nullptr,
                    int pixel_linesize = 0,
                    int idivider = 0,
                    bool scrubbing = false);
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
private:
  static QMutex deferred_delete_mutex_;
  static QVector<QRhiResource*> deferred_delete_queue_;
  void drainDeferredDeletes();

  void delete_buffers();

  // RHI resources
  QRhi* rhi_{nullptr};
  QOffscreenSurface* fallbackSurface_{nullptr};

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

#endif // RENDERTHREAD_H
