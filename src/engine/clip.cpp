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

#include "clip.h"

#include <QtMath>

#include "effects/effect.h"
#include "effects/transition.h"
#include "project/footage.h"
#include "global/config.h"
#include "ui/colorlabel.h"
#include "cacher.h"
#include "rendering/renderfunctions.h"
#include "rendering/renderthread.h"
#include "sequence.h"
#include "project/media.h"
#include "project/clipboard.h"
#include "engine/undo/undo.h"
#include "global/debug.h"

extern "C" {
#include <libavutil/pixfmt.h>
}

const int kRGBAComponentCount = 4;

Clip::Clip(Sequence* s)
    : sequence(s),
      cacher(this),

      autoscale_(amber::CurrentConfig.autoscale_by_default),
      opening_transition(nullptr),
      closing_transition(nullptr)

{
}

ClipPtr Clip::copy(Sequence* s) {
  ClipPtr copy = std::make_shared<Clip>(s);

  copy->set_enabled(enabled());
  copy->set_name(name());
  copy->set_clip_in(clip_in());
  copy->set_timeline_in(timeline_in());
  copy->set_timeline_out(timeline_out());
  copy->set_track(track());
  copy->set_color(color());
  copy->set_media(media(), media_stream_index());
  copy->set_autoscaled(autoscaled());
  copy->set_speed(speed());
  copy->set_reversed(reversed());
  copy->set_color_label(color_label());
  copy->set_loop_mode(loop_mode());

  for (const auto& effect : effects) {
    copy->effects.append(effect->copy(copy.get()));
  }

  copy->set_cached_frame_rate((this->sequence == nullptr) ? cached_frame_rate() : this->sequence->frame_rate);

  copy->refresh();

  return copy;
}

bool Clip::IsActiveAt(long timecode) {
  if (this->sequence == nullptr) return false;

  int open_buffer = qCeil(this->sequence->frame_rate * 2);
  int close_buffer = qCeil(this->sequence->frame_rate);

  return enabled() && timeline_in(true) < timecode + open_buffer && timeline_out(true) > timecode - close_buffer &&
         timecode - timeline_in(true) + clip_in(true) < media_length();
}

bool Clip::IsSelected(bool containing) {
  if (this->sequence == nullptr) {
    return false;
  }

  return this->sequence->IsClipSelected(this, containing);
}

const QColor& Clip::color() const { return color_; }

QColor Clip::display_color() const {
  if (amber::CurrentConfig.show_color_labels && color_label_ > 0) {
    return amber::GetColorLabel(color_label_);
  }
  return color();
}

int Clip::color_label() const { return color_label_; }
void Clip::set_color_label(int label) { color_label_ = label; }
int* Clip::color_label_ptr() { return &color_label_; }

int Clip::loop_mode() const { return loop_mode_; }
void Clip::set_loop_mode(int mode) { loop_mode_ = mode; }
int* Clip::loop_mode_ptr() { return &loop_mode_; }

void Clip::set_color(int r, int g, int b) {
  color_.setRed(r);
  color_.setGreen(g);
  color_.setBlue(b);
}

void Clip::set_color(const QColor& c) { color_ = c; }

Media* Clip::media() { return media_; }

FootageStream* Clip::media_stream() {
  if (media() != nullptr && media()->get_type() == MEDIA_TYPE_FOOTAGE) {
    Footage* f = media()->to_footage();
    if (!f) {
      qWarning() << "Clip::media_stream: to_footage() is null";
      return nullptr;
    }
    return f->get_stream_from_file_index(track() < 0, media_stream_index());
  }

  return nullptr;
}

int Clip::media_stream_index() { return media_stream_; }

void Clip::set_media(Media* m, int s) {
  media_ = m;
  media_stream_ = s;
}

bool Clip::enabled() { return enabled_; }

void Clip::set_enabled(bool e) { enabled_ = e; }

void Clip::move(ComboAction* ca, long iin, long iout, long iclip_in, int itrack, bool verify_transitions,
                bool relative) {
  if (!ca) {
    qWarning() << "Clip::move: ComboAction is null";
    return;
  }
  ca->append(new MoveClipAction(this, iin, iout, iclip_in, itrack, relative));

  if (verify_transitions) {
    if (opening_transition != nullptr && opening_transition->secondary_clip != nullptr &&
        opening_transition->secondary_clip->timeline_out() != iin) {
      ca->append(new SetPointer(reinterpret_cast<void**>(&opening_transition->secondary_clip), nullptr));
      ca->append(new AddTransitionCommand(nullptr, opening_transition->secondary_clip, opening_transition, nullptr, 0));
    }

    if (closing_transition != nullptr && closing_transition->secondary_clip != nullptr &&
        closing_transition->parent_clip->timeline_in() != iout) {
      ca->append(new SetPointer(reinterpret_cast<void**>(&closing_transition->secondary_clip), nullptr));
      ca->append(new AddTransitionCommand(nullptr, this, closing_transition, nullptr, 0));
    }
  }
}

void Clip::reset_audio() {
  if (UsesCacher()) {
    cacher.ResetAudio();
  }
  if (media() != nullptr && media()->get_type() == MEDIA_TYPE_SEQUENCE) {
    Sequence* nested_sequence = media()->to_sequence().get();
    if (!nested_sequence) {
      qWarning() << "Clip::reset_audio: nested sequence is null";
      return;
    }
    for (const auto& clip : nested_sequence->clips) {
      Clip* c = clip.get();
      if (c != nullptr) {
        c->reset_audio();
      }
    }
  }
}

void Clip::refresh() {
  if (replaced && media() != nullptr && media()->get_type() == MEDIA_TYPE_FOOTAGE) {
    Footage* m = media()->to_footage();
    if (!m) {
      qWarning() << "Clip::refresh: to_footage() is null";
      replaced = false;
      return;
    }

    if (track() < 0 && m->video_tracks.size() > 0) {
      set_media(media(), m->video_tracks.at(0).file_index);
    } else if (track() >= 0 && m->audio_tracks.size() > 0) {
      set_media(media(), m->audio_tracks.at(0).file_index);
    }
  }
  replaced = false;

  for (const auto& effect : effects) {
    effect->refresh();
  }
}

QVector<Marker>& Clip::get_markers() {
  if (media() != nullptr) {
    return media()->get_markers();
  }
  return markers;
}

int Clip::IndexOfEffect(Effect* e) {
  for (int i = 0; i < effects.size(); i++) {
    if (effects.at(i).get() == e) {
      return i;
    }
  }
  return -1;
}

Clip::~Clip() {
  if (IsOpen()) {
    Close(true);
  }

  // Safety net: queue any remaining QRhi resources for deferred deletion.
  // If Close() succeeded, these are already nullptr (no-op).
  QVector<QRhiResource*> to_delete;
  if (yuv_rt) to_delete.append(yuv_rt);
  if (yuv_rpd) to_delete.append(yuv_rpd);
  if (yuv_tex_y) to_delete.append(yuv_tex_y);
  if (yuv_tex_u) to_delete.append(yuv_tex_u);
  if (yuv_tex_v) to_delete.append(yuv_tex_v);
  if (yuv_converted_tex) to_delete.append(yuv_converted_tex);
  if (rgba_tex) to_delete.append(rgba_tex);
  if (fbo_rhi != nullptr) {
    ClipRhiResources* res = static_cast<ClipRhiResources*>(fbo_rhi);
    for (int j = 0; j < res->count; j++) {
      if (res->rt[j]) to_delete.append(res->rt[j]);
      if (res->tex[j]) to_delete.append(res->tex[j]);
    }
    if (res->rpd) to_delete.append(res->rpd);
    delete res;
  }
  if (!to_delete.isEmpty()) {
    RenderThread::DeferRhiResourceDeletion(to_delete);
  }

  effects.clear();
}

long Clip::clip_in(bool with_transition) {
  if (with_transition && opening_transition != nullptr && opening_transition->secondary_clip != nullptr) {
    return clip_in_ - opening_transition->get_true_length();
  }
  return clip_in_;
}

void Clip::set_clip_in(long c) { clip_in_ = c; }

long Clip::timeline_in(bool with_transition) {
  if (with_transition && opening_transition != nullptr && opening_transition->secondary_clip != nullptr) {
    return timeline_in_ - opening_transition->get_true_length();
  }
  return timeline_in_;
}

void Clip::set_timeline_in(long t) { timeline_in_ = t; }

long Clip::timeline_out(bool with_transitions) {
  if (with_transitions && closing_transition != nullptr && closing_transition->secondary_clip != nullptr) {
    return timeline_out_ + closing_transition->get_true_length();
  } else {
    return timeline_out_;
  }
}

void Clip::set_timeline_out(long t) { timeline_out_ = t; }

bool Clip::reversed() { return reverse_; }

void Clip::set_reversed(bool r) { reverse_ = r; }

bool Clip::autoscaled() { return autoscale_; }

void Clip::set_autoscaled(bool b) { autoscale_ = b; }

double Clip::cached_frame_rate() { return cached_fr_; }

void Clip::set_cached_frame_rate(double d) { cached_fr_ = d; }

const QString& Clip::name() { return name_; }

void Clip::set_name(const QString& s) { name_ = s; }

const ClipSpeed& Clip::speed() { return speed_; }

void Clip::set_speed(const ClipSpeed& d) { speed_ = d; }

AVRational Clip::time_base() { return cacher.media_time_base(); }

int Clip::track() { return track_; }

void Clip::set_track(int t) { track_ = t; }

long Clip::length() { return timeline_out_ - timeline_in_; }

double Clip::media_frame_rate() {
  Q_ASSERT(track_ < 0);
  if (media_ != nullptr) {
    double rate = media_->get_frame_rate(media_stream_index());
    if (!qIsNaN(rate)) return rate;
  }
  if (sequence != nullptr) return sequence->frame_rate;
  return qSNaN();
}

long Clip::media_length() {
  if (this->sequence != nullptr) {
    if (qFuzzyIsNull(speed_.value)) return LONG_MAX;

    double fr = this->sequence->frame_rate;

    fr /= speed_.value;

    if (media_ == nullptr) {
      return LONG_MAX;
    } else {
      switch (media_->get_type()) {
        case MEDIA_TYPE_FOOTAGE: {
          Footage* m = media_->to_footage();
          if (!m) {
            qWarning() << "Clip::media_length: to_footage() is null";
            return 0;
          }
          const FootageStream* ms = m->get_stream_from_file_index(track_ < 0, media_stream_index());
          if (ms != nullptr && ms->infinite_length) {
            return LONG_MAX;
          } else {
            return m->get_length_in_frames(fr);
          }
        }
        case MEDIA_TYPE_SEQUENCE: {
          Sequence* s = media_->to_sequence().get();
          if (!s) {
            qWarning() << "Clip::media_length: to_sequence() is null";
            return 0;
          }
          return rescale_frame_number(s->getEndFrame(), s->frame_rate, fr);
        }
      }
    }
  }
  return 0;
}

int Clip::media_width() {
  if (media_ == nullptr) return (sequence != nullptr) ? sequence->width : 0;
  switch (media_->get_type()) {
    case MEDIA_TYPE_FOOTAGE: {
      const FootageStream* ms = media_stream();
      if (ms != nullptr) return ms->video_width;
      if (sequence != nullptr) return sequence->width;
      break;
    }
    case MEDIA_TYPE_SEQUENCE: {
      Sequence* s = media_->to_sequence().get();
      if (!s) {
        qWarning() << "Clip::media_width: to_sequence() is null";
        return 0;
      }
      return s->width;
    }
  }
  return 0;
}

int Clip::media_height() {
  if (media_ == nullptr) return (sequence != nullptr) ? sequence->height : 0;
  switch (media_->get_type()) {
    case MEDIA_TYPE_FOOTAGE: {
      const FootageStream* ms = media_stream();
      if (ms != nullptr) return ms->video_height;
      if (sequence != nullptr) return sequence->height;
    } break;
    case MEDIA_TYPE_SEQUENCE: {
      Sequence* s = media_->to_sequence().get();
      if (!s) {
        qWarning() << "Clip::media_height: to_sequence() is null";
        return 0;
      }
      return s->height;
    }
  }
  return 0;
}

void Clip::refactor_frame_rate(ComboAction* ca, double multiplier, bool change_timeline_points) {
  if (!ca) {
    qWarning() << "Clip::refactor_frame_rate: ComboAction is null";
    return;
  }
  if (change_timeline_points) {
    this->move(ca, qRound(double(timeline_in_) * multiplier), qRound(double(timeline_out_) * multiplier),
               qRound(double(clip_in_) * multiplier), track_);
  }

  for (auto e : effects) {
    if (!e) continue;
    for (int j = 0; j < e->row_count(); j++) {
      EffectRow* r = e->row(j);
      if (!r) {
        qWarning() << "Clip::refactor_frame_rate: effect row is null";
        continue;
      }
      for (int l = 0; l < r->FieldCount(); l++) {
        EffectField* f = r->Field(l);
        if (!f) {
          qWarning() << "Clip::refactor_frame_rate: effect field is null";
          continue;
        }
        for (auto& keyframe : f->keyframes) {
          ca->append(new SetLong(&keyframe.time, keyframe.time, qRound(keyframe.time * multiplier)));
        }
      }
    }
  }
}

void Clip::Open() {
  if (!open_ && state_change_lock.tryLock()) {
    open_ = true;
    cacher_uses_rgba_ = NeedsCpuRgba();

    for (const auto& effect : effects) {
      effect->open();
    }

    texture_frame = -1;

    if (UsesCacher()) {
      cacher.Open();
    } else {
      state_change_lock.unlock();
    }
  }
}

void Clip::Close(bool wait) {
  if (open_ && state_change_lock.tryLock()) {
    open_ = false;

    if (media() != nullptr && media()->get_type() == MEDIA_TYPE_SEQUENCE) {
      Sequence* nested = media()->to_sequence().get();
      if (nested) {
        close_active_clips(nested);
      } else {
        qWarning() << "Clip::Close: nested sequence is null";
      }
    }

    // Queue YUV conversion resources for deferred deletion on the render thread
    QVector<QRhiResource*> to_delete;
    if (yuv_rt) to_delete.append(yuv_rt);
    if (yuv_rpd) to_delete.append(yuv_rpd);
    if (yuv_tex_y) to_delete.append(yuv_tex_y);
    if (yuv_tex_u) to_delete.append(yuv_tex_u);
    if (yuv_tex_v) to_delete.append(yuv_tex_v);
    if (yuv_converted_tex) to_delete.append(yuv_converted_tex);
    if (rgba_tex) to_delete.append(rgba_tex);

    if (fbo_rhi != nullptr) {
      ClipRhiResources* res = static_cast<ClipRhiResources*>(fbo_rhi);
      for (int j = 0; j < res->count; j++) {
        if (res->rt[j]) to_delete.append(res->rt[j]);
        if (res->tex[j]) to_delete.append(res->tex[j]);
      }
      if (res->rpd) to_delete.append(res->rpd);
      delete res;  // Plain C++ struct, not a QRhiResource — safe from any thread
      fbo_rhi = nullptr;
    }

    RenderThread::DeferRhiResourceDeletion(to_delete);

    yuv_rt = nullptr;
    yuv_rpd = nullptr;
    yuv_tex_y = nullptr;
    yuv_tex_u = nullptr;
    yuv_tex_v = nullptr;
    yuv_converted_tex = nullptr;
    cached_rhi_tex = nullptr;
    rgba_tex = nullptr;

    // Close all effects
    for (const auto& effect : effects) {
      if (effect->is_open()) {
        effect->close();
      }
    }

    if (UsesCacher()) {
      cacher.Close(wait);
    } else {
      state_change_lock.unlock();
    }
  }
}

bool Clip::IsOpen() { return open_; }

void Clip::Cache(long playhead, bool scrubbing, QVector<Clip*>& nests, int playback_speed) {
  cacher.Cache(playhead, scrubbing, nests, playback_speed);
  cacher_frame = playhead;
}

bool Clip::Retrieve(QRhi* rhi, QRhiCommandBuffer* cb, ComposeSequenceParams* params) {
  if (!rhi) {
    qWarning() << "Clip::Retrieve: rhi is null";
    return false;
  }
  if (!cb) {
    qWarning() << "Clip::Retrieve: cb is null";
    return false;
  }
  if (!params) {
    qWarning() << "Clip::Retrieve: params is null";
    return false;
  }
  bool ret = false;

  if (UsesCacher()) {
    AVFrame* frame = cacher.Retrieve();

    cacher.queue()->lock();

    if (frame != nullptr && cacher.queue()->contains(frame)) {
      bool is_yuv420p = (frame->format == AV_PIX_FMT_YUV420P);
      bool is_nv12 = (frame->format == AV_PIX_FMT_NV12);

      if (rhi != nullptr && (is_yuv420p || is_nv12)) {
        // GPU YUV->RGB path: upload plane textures and convert via shader
        int w = cacher.media_width();
        int h = cacher.media_height();

        // Create/recreate plane textures if needed
        if (yuv_tex_y == nullptr) {
          yuv_tex_y = rhi->newTexture(QRhiTexture::R8, QSize(w, h));
          yuv_tex_y->create();
        }
        if (yuv_tex_u == nullptr) {
          if (is_nv12) {
            yuv_tex_u = rhi->newTexture(QRhiTexture::RG8, QSize(w / 2, h / 2));
          } else {
            yuv_tex_u = rhi->newTexture(QRhiTexture::R8, QSize(w / 2, h / 2));
          }
          yuv_tex_u->create();
        }
        if (yuv_tex_v == nullptr && is_yuv420p) {
          yuv_tex_v = rhi->newTexture(QRhiTexture::R8, QSize(w / 2, h / 2));
          yuv_tex_v->create();
        }
        if (yuv_converted_tex == nullptr) {
          yuv_converted_tex = rhi->newTexture(QRhiTexture::RGBA8, QSize(w, h),
                                              1, QRhiTexture::RenderTarget);
          yuv_converted_tex->create();
          yuv_rt = rhi->newTextureRenderTarget({yuv_converted_tex});
          yuv_rpd = yuv_rt->newCompatibleRenderPassDescriptor();
          yuv_rt->setRenderPassDescriptor(yuv_rpd);
          yuv_rt->create();
        }

        // Upload plane data
        QRhiResourceUpdateBatch* u = rhi->nextResourceUpdateBatch();

        // Y plane
        {
          QByteArray yData(reinterpret_cast<const char*>(frame->data[0]), frame->linesize[0] * h);
          QRhiTextureSubresourceUploadDescription desc(yData);
          desc.setSourceSize(QSize(w, h));
          desc.setDataStride(frame->linesize[0]);
          u->uploadTexture(yuv_tex_y, QRhiTextureUploadDescription({QRhiTextureUploadEntry(0, 0, desc)}));
        }

        // U/UV plane
        {
          int uv_h = h / 2;
          if (is_nv12) {
            QByteArray uvData(reinterpret_cast<const char*>(frame->data[1]), frame->linesize[1] * uv_h);
            QRhiTextureSubresourceUploadDescription desc(uvData);
            desc.setSourceSize(QSize(w / 2, uv_h));
            desc.setDataStride(frame->linesize[1]);
            u->uploadTexture(yuv_tex_u, QRhiTextureUploadDescription({QRhiTextureUploadEntry(0, 0, desc)}));
          } else {
            QByteArray uData(reinterpret_cast<const char*>(frame->data[1]), frame->linesize[1] * uv_h);
            QRhiTextureSubresourceUploadDescription desc(uData);
            desc.setSourceSize(QSize(w / 2, uv_h));
            desc.setDataStride(frame->linesize[1]);
            u->uploadTexture(yuv_tex_u, QRhiTextureUploadDescription({QRhiTextureUploadEntry(0, 0, desc)}));
          }
        }

        // V plane (YUV420P only)
        if (is_yuv420p) {
          int v_h = h / 2;
          QByteArray vData(reinterpret_cast<const char*>(frame->data[2]), frame->linesize[2] * v_h);
          QRhiTextureSubresourceUploadDescription desc(vData);
          desc.setSourceSize(QSize(w / 2, v_h));
          desc.setDataStride(frame->linesize[2]);
          u->uploadTexture(yuv_tex_v, QRhiTextureUploadDescription({QRhiTextureUploadEntry(0, 0, desc)}));
        }

        // YUV->RGB conversion pass (dedicated buffers to avoid shared dynamic buffer hazard)
        {
          int format_type = is_nv12 ? 1 : 0;

          // Determine YUV color space from decoded frame metadata
          int color_space_val = 0;  // 0 = BT.709 (default)
          if (frame->colorspace == AVCOL_SPC_BT470BG || frame->colorspace == AVCOL_SPC_SMPTE170M) {
            color_space_val = 1;  // BT.601
          } else if (frame->colorspace == AVCOL_SPC_UNSPECIFIED) {
            // Infer from resolution: SD content (width <= 720) is typically BT.601
            if (cacher.media_width() <= 720) {
              color_space_val = 1;  // BT.601
            }
          }

          QByteArray fragData(16, 0);
          memcpy(fragData.data(), &format_type, 4);
          memcpy(fragData.data() + 4, &color_space_val, 4);

          QRhiBuffer* yuvVbuf =
              rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::VertexBuffer, 4 * 4 * sizeof(float));
          yuvVbuf->create();
          QRhiBuffer* yuvVertUbo =
              rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, 64);
          yuvVertUbo->create();
          QRhiBuffer* yuvFragUbo =
              rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, 16);
          yuvFragUbo->create();

          QRhiSampler* sampler = params->sampler;
          QRhiTexture* vTex = yuv_tex_v ? yuv_tex_v : yuv_tex_u;

          QRhiShaderResourceBindings* srb = rhi->newShaderResourceBindings();
          srb->setBindings({
              QRhiShaderResourceBinding::uniformBuffer(0, QRhiShaderResourceBinding::VertexStage, yuvVertUbo),
              QRhiShaderResourceBinding::uniformBuffer(1, QRhiShaderResourceBinding::FragmentStage, yuvFragUbo),
              QRhiShaderResourceBinding::sampledTexture(2, QRhiShaderResourceBinding::FragmentStage, yuv_tex_y,
                                                         sampler),
              QRhiShaderResourceBinding::sampledTexture(3, QRhiShaderResourceBinding::FragmentStage, yuv_tex_u,
                                                         sampler),
              QRhiShaderResourceBinding::sampledTexture(4, QRhiShaderResourceBinding::FragmentStage, vTex, sampler),
          });
          srb->create();

          QRhiGraphicsPipeline* pipeline = rhi->newGraphicsPipeline();
          pipeline->setShaderStages(
              {{QRhiShaderStage::Vertex, params->passthroughVert}, {QRhiShaderStage::Fragment, params->yuvFrag}});
          QRhiVertexInputLayout inputLayout;
          inputLayout.setBindings({{4 * sizeof(float)}});
          inputLayout.setAttributes({
              {0, 0, QRhiVertexInputAttribute::Float2, 0},
              {0, 1, QRhiVertexInputAttribute::Float2, 2 * sizeof(float)},
          });
          pipeline->setVertexInputLayout(inputLayout);
          pipeline->setTopology(QRhiGraphicsPipeline::TriangleStrip);
          QRhiGraphicsPipeline::TargetBlend noBlend;
          noBlend.enable = false;
          pipeline->setTargetBlends({noBlend});
          pipeline->setShaderResourceBindings(srb);
          pipeline->setRenderPassDescriptor(yuv_rpd);
          pipeline->create();

          float blitQuad[] = {
              -1, -1, 0, 0, -1, 1, 0, 1, 1, -1, 1, 0, 1, 1, 1, 1,
          };
          QMatrix4x4 mvp = rhi->clipSpaceCorrMatrix();
          mvp.ortho(-1, 1, -1, 1, -1, 1);

          u->updateDynamicBuffer(yuvVbuf, 0, sizeof(blitQuad), blitQuad);
          u->updateDynamicBuffer(yuvVertUbo, 0, 64, mvp.constData());
          u->updateDynamicBuffer(yuvFragUbo, 0, 16, fragData.constData());

          cb->beginPass(yuv_rt, QColor(0, 0, 0, 0), {1.0f, 0}, u);
          cb->setGraphicsPipeline(pipeline);
          cb->setViewport({0, 0, float(w), float(h)});
          cb->setShaderResources(srb);
          const QRhiCommandBuffer::VertexInput vbufBinding(yuvVbuf, 0);
          cb->setVertexInput(0, 1, &vbufBinding);
          cb->draw(4);
          cb->endPass();

          params->transientResources.append(pipeline);
          params->transientResources.append(srb);
          params->transientResources.append(yuvVbuf);
          params->transientResources.append(yuvVertUbo);
          params->transientResources.append(yuvFragUbo);
        }

        cached_rhi_tex = yuv_converted_tex;
        ret = true;
      } else {
        // CPU RGBA path
        int w = cacher.media_width();
        int h = cacher.media_height();

        if (rgba_tex == nullptr) {
          rgba_tex = rhi->newTexture(QRhiTexture::RGBA8, QSize(w, h));
          rgba_tex->create();
        }

        // Process image effects on CPU
        bool using_db_1 = true;
        uint8_t* data_buffer_1 = frame->data[0];
        uint8_t* data_buffer_2 = nullptr;
        int frame_size = frame->linesize[0] * frame->height;

        for (const auto& effect : effects) {
          Effect* e = effect.get();
          if ((e->Flags() & Effect::ImageFlag) && e->IsEnabled()) {
            if (data_buffer_1 == frame->data[0]) {
              data_buffer_1 = new uint8_t[frame_size];
              data_buffer_2 = new uint8_t[frame_size];
              memcpy(data_buffer_1, frame->data[0], frame_size);
            }

            e->process_image(get_timecode(this, cacher_frame), using_db_1 ? data_buffer_1 : data_buffer_2,
                             using_db_1 ? data_buffer_2 : data_buffer_1, frame_size);

            using_db_1 = !using_db_1;
          }
        }

        // Upload to QRhiTexture
        QRhiResourceUpdateBatch* u = rhi->nextResourceUpdateBatch();
        const uint8_t* uploadData = using_db_1 ? data_buffer_1 : data_buffer_2;
        int stride = frame->linesize[0];
        QByteArray texData(reinterpret_cast<const char*>(uploadData), stride * h);
        QRhiTextureSubresourceUploadDescription desc(texData);
        desc.setSourceSize(QSize(w, h));
        desc.setDataStride(stride);
        u->uploadTexture(rgba_tex, QRhiTextureUploadDescription({QRhiTextureUploadEntry(0, 0, desc)}));

        // Submit upload in a dummy pass (no rendering needed)
        // Actually just pass it to next beginPass
        // For now, do a minimal pass to flush the upload
        if (fbo_rhi != nullptr) {
          ClipRhiResources* res = static_cast<ClipRhiResources*>(fbo_rhi);
          QColor clearColor(0, 0, 0, 0);
          cb->beginPass(res->rt[0], clearColor, {1.0f, 0}, u);
          cb->endPass();
        } else {
          // No FBO yet — submit via main target
          // The upload batch will be consumed by the next beginPass in compose_sequence
          // We can't submit it without a render target, so we need a workaround
          // Use the params main target
          QColor clearColor(0, 0, 0, 0);
          cb->beginPass(params->main_target, clearColor, {1.0f, 0}, u);
          cb->endPass();
        }

        if (data_buffer_1 != frame->data[0]) {
          delete[] data_buffer_1;
          delete[] data_buffer_2;
        }

        cached_rhi_tex = rgba_tex;
        ret = true;
      }
    } else {
      qWarning() << "Failed to retrieve frame for clip" << name();
    }

    cacher.queue()->unlock();
  }

  return ret;
}

bool Clip::UsesCacher() { return track() >= 0 || (media() != nullptr && media()->get_type() == MEDIA_TYPE_FOOTAGE); }

bool Clip::NeedsCpuRgba() const {
  for (const auto& effect : effects) {
    if ((effect->Flags() & Effect::ImageFlag) && effect->IsEnabled()) {
      return true;
    }
  }
  return false;
}

bool Clip::NeedsCacherReconfigure() const { return open_ && (NeedsCpuRgba() != cacher_uses_rgba_); }

ClipSpeed::ClipSpeed()

    = default;
