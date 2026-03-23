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

#include "renderfunctions.h"

extern "C" {
#include <libavformat/avformat.h>
}

#include <QApplication>
#include <QDebug>
#include <QScreen>
#include <utility>

#include "engine/clip.h"
#include "engine/sequence.h"
#include "effects/effect.h"
#include "effects/transition.h"
#include "project/footage.h"
#include "project/media.h"

#include "ui/collapsiblewidget.h"

#include "rendering/audio.h"

#include "core/math.h"
#include "global/config.h"

#include "panels/timeline.h"
#include "panels/viewer.h"

// Depth-only correction for intermediate offscreen passes (no Y-flip).
// Remaps OpenGL depth range [-1,1] to [0,1] for Vulkan/Metal/D3D.
static QMatrix4x4 depthCorrMatrix(QRhi* rhi) {
  QMatrix4x4 m;
  if (rhi->isClipDepthZeroToOne()) {
    m(2, 2) = 0.5f;
    m(2, 3) = 0.5f;
  }
  return m;
}

// Helper: create a temporary pipeline, draw a fullscreen blit, then destroy the pipeline.
// This replaces the old draw_clip() / full_blit() pattern.
static void rhi_blit(ComposeSequenceParams& params, QRhiTextureRenderTarget* target, QRhiRenderPassDescriptor* rpd,
                     QRhiTexture* srcTex, const QShader& vertShader, const QShader& fragShader, const QMatrix4x4& mvp,
                     const QByteArray& fragUboData, int fragUboSize, int texBindingCount = 1,
                     QRhiTexture* extraTex1 = nullptr, QRhiTexture* extraTex2 = nullptr,
                     bool skipClipSpaceCorr = false) {
  if (!params.rhi) {
    qWarning() << "rhi_blit: params.rhi is null";
    return;
  }
  if (!params.cb) {
    qWarning() << "rhi_blit: params.cb is null";
    return;
  }
  if (!target) {
    qWarning() << "rhi_blit: target is null";
    return;
  }
  if (!rpd) {
    qWarning() << "rhi_blit: rpd is null";
    return;
  }
  if (!srcTex) {
    qWarning() << "rhi_blit: srcTex is null";
    return;
  }
  QRhi* rhi = params.rhi;
  QRhiCommandBuffer* cb = params.cb;

  // Create fragment UBO (skip if shader has no fragment uniforms)
  QRhiBuffer* fragUbo = nullptr;
  if (fragUboSize > 0) {
    fragUbo = rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, fragUboSize);
    fragUbo->create();
  }

  // Build SRB
  QVector<QRhiShaderResourceBinding> bindings;
  bindings.append(QRhiShaderResourceBinding::uniformBuffer(0, QRhiShaderResourceBinding::VertexStage, params.vertUbo));
  if (fragUboSize > 0) {
    bindings.append(QRhiShaderResourceBinding::uniformBuffer(1, QRhiShaderResourceBinding::FragmentStage, fragUbo));
  }
  bindings.append(
      QRhiShaderResourceBinding::sampledTexture(2, QRhiShaderResourceBinding::FragmentStage, srcTex, params.sampler));
  if (texBindingCount >= 2 && extraTex1) {
    bindings.append(QRhiShaderResourceBinding::sampledTexture(3, QRhiShaderResourceBinding::FragmentStage, extraTex1,
                                                              params.sampler));
  }
  if (texBindingCount >= 3 && extraTex2) {
    bindings.append(QRhiShaderResourceBinding::sampledTexture(4, QRhiShaderResourceBinding::FragmentStage, extraTex2,
                                                              params.sampler));
  }

  QRhiShaderResourceBindings* srb = rhi->newShaderResourceBindings();
  srb->setBindings(bindings.cbegin(), bindings.cend());
  srb->create();

  // Build pipeline
  QRhiGraphicsPipeline* pipeline = rhi->newGraphicsPipeline();
  pipeline->setShaderStages({{QRhiShaderStage::Vertex, vertShader}, {QRhiShaderStage::Fragment, fragShader}});
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
  pipeline->setRenderPassDescriptor(rpd);
  if (!pipeline->create()) {
    qWarning() << "[RHI-PIPELINE] rhi_blit pipeline creation FAILED";
  }

  // Fullscreen quad vertex data
  float blitQuad[] = {
      -1, -1, 0, 0,  // BL
      -1, 1,  0, 1,  // TL
      1,  -1, 1, 0,  // BR
      1,  1,  1, 1,  // TR
  };

  // Final compositing passes: full clipSpaceCorrMatrix (Y-flip + depth remap).
  // Intermediate passes: depth-only correction (no Y-flip to avoid accumulated flips).
  QMatrix4x4 corrected_mvp = (skipClipSpaceCorr ? depthCorrMatrix(rhi) : rhi->clipSpaceCorrMatrix()) * mvp;

  QRhiResourceUpdateBatch* u = rhi->nextResourceUpdateBatch();
  u->updateDynamicBuffer(params.vbuf, 0, sizeof(blitQuad), blitQuad);
  u->updateDynamicBuffer(params.vertUbo, 0, 64, corrected_mvp.constData());
  if (fragUboSize > 0 && !fragUboData.isEmpty()) {
    u->updateDynamicBuffer(fragUbo, 0, fragUboSize, fragUboData.constData());
  }

  QSize sz = target->pixelSize();
  QColor clearColor(0, 0, 0, 0);
  cb->beginPass(target, clearColor, {1.0f, 0}, u);
  cb->setGraphicsPipeline(pipeline);
  cb->setViewport({0, 0, float(sz.width()), float(sz.height())});
  cb->setShaderResources(srb);
  const QRhiCommandBuffer::VertexInput vbufBinding(params.vbuf, 0);
  cb->setVertexInput(0, 1, &vbufBinding);
  cb->draw(4);
  cb->endPass();

  // Defer cleanup — resources must survive until endOffscreenFrame()
  params.transientResources.append(pipeline);
  params.transientResources.append(srb);
  if (fragUbo) params.transientResources.append(fragUbo);
}

// Blit with hardware SrcOver alpha blending (One/OneMinusSrcAlpha for premultiplied sources).
// Target MUST have PreserveColorContents so existing content acts as background.
static void rhi_blit_srcover(ComposeSequenceParams& params, QRhiTextureRenderTarget* target,
                             QRhiRenderPassDescriptor* rpd, QRhiTexture* srcTex, float opacity = 1.0f,
                             bool skipClipSpaceCorr = false) {
  if (!params.rhi) {
    qWarning() << "rhi_blit_srcover: params.rhi is null";
    return;
  }
  if (!params.cb) {
    qWarning() << "rhi_blit_srcover: params.cb is null";
    return;
  }
  if (!target) {
    qWarning() << "rhi_blit_srcover: target is null";
    return;
  }
  if (!rpd) {
    qWarning() << "rhi_blit_srcover: rpd is null";
    return;
  }
  if (!srcTex) {
    qWarning() << "rhi_blit_srcover: srcTex is null";
    return;
  }
  QRhi* rhi = params.rhi;
  QRhiCommandBuffer* cb = params.cb;

  QRhiBuffer* fragUbo = rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, 16);
  fragUbo->create();

  QRhiShaderResourceBindings* srb = rhi->newShaderResourceBindings();
  srb->setBindings({
      QRhiShaderResourceBinding::uniformBuffer(0, QRhiShaderResourceBinding::VertexStage, params.vertUbo),
      QRhiShaderResourceBinding::uniformBuffer(1, QRhiShaderResourceBinding::FragmentStage, fragUbo),
      QRhiShaderResourceBinding::sampledTexture(2, QRhiShaderResourceBinding::FragmentStage, srcTex, params.sampler),
  });
  srb->create();

  QRhiGraphicsPipeline* pipeline = rhi->newGraphicsPipeline();
  pipeline->setShaderStages(
      {{QRhiShaderStage::Vertex, params.passthroughVert}, {QRhiShaderStage::Fragment, params.passthroughFrag}});
  QRhiVertexInputLayout inputLayout;
  inputLayout.setBindings({{4 * sizeof(float)}});
  inputLayout.setAttributes({
      {0, 0, QRhiVertexInputAttribute::Float2, 0},
      {0, 1, QRhiVertexInputAttribute::Float2, 2 * sizeof(float)},
  });
  pipeline->setVertexInputLayout(inputLayout);
  pipeline->setTopology(QRhiGraphicsPipeline::TriangleStrip);

  // SrcOver for premultiplied alpha: result = src + dst * (1 - srcAlpha)
  QRhiGraphicsPipeline::TargetBlend blend;
  blend.enable = true;
  blend.srcColor = QRhiGraphicsPipeline::One;
  blend.dstColor = QRhiGraphicsPipeline::OneMinusSrcAlpha;
  blend.srcAlpha = QRhiGraphicsPipeline::One;
  blend.dstAlpha = QRhiGraphicsPipeline::OneMinusSrcAlpha;
  pipeline->setTargetBlends({blend});

  pipeline->setShaderResourceBindings(srb);
  pipeline->setRenderPassDescriptor(rpd);
  if (!pipeline->create()) {
    qWarning() << "[RHI-PIPELINE] rhi_blit_srcover pipeline creation FAILED";
  }

  float blitQuad[] = {
      -1, -1, 0, 0, -1, 1, 0, 1, 1, -1, 1, 0, 1, 1, 1, 1,
  };

  QMatrix4x4 mvp;
  mvp.ortho(-1, 1, -1, 1, -1, 1);
  // Intentional clipSpaceCorrMatrix on this offscreen-to-offscreen pass: the extra Y-flip
  // pairs with those in render_clip_to_backbuffer and the YUV->RGB pass to keep the total
  // flip count even. Changing this requires adjusting the entire flip chain.
  QMatrix4x4 corrected_mvp = (skipClipSpaceCorr ? depthCorrMatrix(rhi) : rhi->clipSpaceCorrMatrix()) * mvp;

  float colorMult[4] = {opacity, opacity, opacity, opacity};

  QRhiResourceUpdateBatch* u = rhi->nextResourceUpdateBatch();
  u->updateDynamicBuffer(params.vbuf, 0, sizeof(blitQuad), blitQuad);
  u->updateDynamicBuffer(params.vertUbo, 0, 64, corrected_mvp.constData());
  u->updateDynamicBuffer(fragUbo, 0, 16, colorMult);

  QSize sz = target->pixelSize();
  QColor clearColor(0, 0, 0, 0);
  cb->beginPass(target, clearColor, {1.0f, 0}, u);
  cb->setGraphicsPipeline(pipeline);
  cb->setViewport({0, 0, float(sz.width()), float(sz.height())});
  cb->setShaderResources(srb);
  const QRhiCommandBuffer::VertexInput vbufBinding(params.vbuf, 0);
  cb->setVertexInput(0, 1, &vbufBinding);
  cb->draw(4);
  cb->endPass();

  params.transientResources.append(pipeline);
  params.transientResources.append(srb);
  params.transientResources.append(fragUbo);
}

// Simplified blit: passthrough shader, identity MVP
// Clearing is determined by the render target's PreserveColorContents flag.
static void rhi_blit_passthrough(ComposeSequenceParams& params, QRhiTextureRenderTarget* target,
                                 QRhiRenderPassDescriptor* rpd, QRhiTexture* srcTex, float opacity = 1.0f,
                                 bool skipClipSpaceCorr = false) {
  if (!target) {
    qWarning() << "rhi_blit_passthrough: target is null";
    return;
  }
  if (!rpd) {
    qWarning() << "rhi_blit_passthrough: rpd is null";
    return;
  }
  if (!srcTex) {
    qWarning() << "rhi_blit_passthrough: srcTex is null";
    return;
  }
  QMatrix4x4 mvp;
  mvp.ortho(-1, 1, -1, 1, -1, 1);

  float colorMult[4] = {opacity, opacity, opacity, opacity};
  QByteArray fragData(reinterpret_cast<const char*>(colorMult), 16);

  rhi_blit(params, target, rpd, srcTex, params.passthroughVert, params.passthroughFrag, mvp, fragData, 16, 1, nullptr,
           nullptr, skipClipSpaceCorr);
}

// Helper: create clip's ping-pong QRhiTexture + render target pairs (replaces QOpenGLFramebufferObject)
static ClipRhiResources* get_or_create_clip_resources(Clip* c, QRhi* rhi, int width, int height) {
  if (!c) {
    qWarning() << "get_or_create_clip_resources: c is null";
    return nullptr;
  }
  if (!rhi) {
    qWarning() << "get_or_create_clip_resources: rhi is null";
    return nullptr;
  }
  // We store the ClipRhiResources in c->fbo_rhi (a void* field we'll add to Clip)
  if (c->fbo_rhi == nullptr) {
    int fbo_count = (c->media() != nullptr && c->media()->get_type() == MEDIA_TYPE_SEQUENCE) ? 3 : 2;
    ClipRhiResources* res = new ClipRhiResources();
    res->count = fbo_count;
    for (int j = 0; j < fbo_count; j++) {
      res->tex[j] = rhi->newTexture(QRhiTexture::RGBA8, QSize(width, height), 1, QRhiTexture::RenderTarget);
      res->tex[j]->create();
      res->rt[j] = rhi->newTextureRenderTarget(
          {res->tex[j]}, QRhiTextureRenderTarget::PreserveColorContents);
      if (j == 0) {
        res->rpd = res->rt[j]->newCompatibleRenderPassDescriptor();
      }
      res->rt[j]->setRenderPassDescriptor(res->rpd);
      res->rt[j]->create();
    }
    c->fbo_rhi = res;
  }
  return static_cast<ClipRhiResources*>(c->fbo_rhi);
}

static void process_effect(Clip* c, Effect* e, double timecode, GLTextureCoords& coords,
                           QRhiTexture*& composite_texture, bool& fbo_switcher, bool& texture_failed, int data,
                           ComposeSequenceParams& params) {
  if (!c) {
    qWarning() << "process_effect: c is null";
    return;
  }
  if (!e) {
    qWarning() << "process_effect: e is null";
    return;
  }
  if (e->IsEnabled()) {
    if (e->Flags() & Effect::CoordsFlag) {
      e->process_coords(timecode, coords, data);
    }
    bool can_process_shaders = ((e->Flags() & Effect::ShaderFlag) && amber::CurrentRuntimeConfig.shaders_are_enabled);
    if (can_process_shaders || (e->Flags() & Effect::SuperimposeFlag)) {
      e->startEffect();

      bool has_shader = can_process_shaders && e->is_glsl_linked();

      if (has_shader && composite_texture != nullptr) {
        ClipRhiResources* res = static_cast<ClipRhiResources*>(c->fbo_rhi);
        QMatrix4x4 blitMvp;
        blitMvp.ortho(-1, 1, -1, 1, -1, 1);

        for (int i = 0; i < e->getIterations(); i++) {
          // Fill UBO data via the effect's process_shader
          QByteArray uboData;
          uboData.resize(qMax(e->fragUboSize(), e->vertUboSize()));
          uboData.fill(0);
          e->process_shader(timecode, coords, i, uboData);

          // Blit through effect shader into the next FBO (skip clipSpaceCorr — intermediate pass)
          rhi_blit(params, res->rt[fbo_switcher], res->rpd, composite_texture, e->vertexShader(), e->fragmentShader(),
                   blitMvp, uboData, qMax(e->fragUboSize(), e->vertUboSize()), 1, nullptr, nullptr,
                   /*skipClipSpaceCorr=*/true);
          composite_texture = res->tex[fbo_switcher];
          fbo_switcher = !fbo_switcher;
        }
      }

      if (e->Flags() & Effect::SuperimposeFlag) {
        QRhiResourceUpdateBatch* upload = params.rhi->nextResourceUpdateBatch();
        QRhiTexture* superimpose_texture = e->process_superimpose(params.rhi, upload, timecode);
        // Submit upload in a dummy pass
        ClipRhiResources* res = static_cast<ClipRhiResources*>(c->fbo_rhi);

        if (superimpose_texture == nullptr) {
          qWarning() << "Superimpose texture was nullptr, retrying...";
          texture_failed = true;
          upload->release();
        } else if (composite_texture == nullptr) {
          // No existing composite — just submit upload and use superimpose directly
          QColor clearColor(0, 0, 0, 0);
          params.cb->beginPass(res->rt[!fbo_switcher], clearColor, {1.0f, 0}, upload);
          params.cb->endPass();
          composite_texture = superimpose_texture;
        } else {
          // Composite: blit existing texture, then overlay superimpose
          // First submit the texture upload
          QColor clearColor(0, 0, 0, 0);
          params.cb->beginPass(res->rt[!fbo_switcher], clearColor, {1.0f, 0}, upload);
          params.cb->endPass();

          // Copy composite to fbo if not already there (fullscreen opaque blit overwrites all pixels,
          // so PreserveColorContents is harmless — stale data from previous frames is replaced)
          if (composite_texture != res->tex[0] && composite_texture != res->tex[1]) {
            rhi_blit_passthrough(params, res->rt[!fbo_switcher], res->rpd, composite_texture,
                                 1.0f, /*skipClipSpaceCorr=*/true);
          }
          // Overlay superimpose with alpha blending (PreserveColorContents keeps the composite)
          rhi_blit_srcover(params, res->rt[!fbo_switcher], res->rpd, superimpose_texture,
                           1.0f, /*skipClipSpaceCorr=*/true);
          composite_texture = res->tex[!fbo_switcher];
        }
      }
      e->endEffect();
    }
  }
}

// Collect active clips for the current playhead, open/close as needed, and sort video clips by track.
// Returns the sorted list of active clips and increments audio_track_count for audio tracks.
static QVector<Clip*> collect_active_clips(Sequence* s, long playhead, ComposeSequenceParams& params,
                                           int& audio_track_count) {
  QVector<Clip*> current_clips;
  if (!s) {
    qWarning() << "collect_active_clips: s is null";
    return current_clips;
  }

  for (const auto& clip : s->clips) {
    Clip* c = clip.get();
    if (c == nullptr) continue;
    if ((c->track() < 0) != params.video) continue;

    bool clip_is_active = false;

    if (c->media() != nullptr && c->media()->get_type() == MEDIA_TYPE_FOOTAGE) {
      Footage* m = c->media()->to_footage();
      if (!m->invalid && !(c->track() >= 0 && !is_audio_device_set())) {
        if (m->ready) {
          const FootageStream* ms = c->media_stream();
          if (ms != nullptr && c->IsActiveAt(playhead)) {
            if (c->NeedsCacherReconfigure()) {
              c->Close(false);
            }
            if (!c->IsOpen()) {
              c->Open();
            }
            clip_is_active = true;
            if (c->track() >= 0) audio_track_count++;
          } else if (c->IsOpen()) {
            c->Close(false);
          }
        } else {
          params.texture_failed = true;
        }
      }
    } else {
      if (c->IsActiveAt(playhead)) {
        if (!c->IsOpen()) {
          c->Open();
        }
        clip_is_active = true;
      } else if (c->IsOpen()) {
        c->Close(false);
      }
    }

    if (clip_is_active) {
      bool added = false;
      if (params.video) {
        for (int j = 0; j < current_clips.size(); j++) {
          if (current_clips.at(j)->track() < c->track()) {
            current_clips.insert(j, c);
            added = true;
            break;
          }
        }
      }
      if (!added) {
        current_clips.append(c);
      }
    }
  }

  return current_clips;
}

// Render a clip's textured quad into the back buffer with the given MVP transform.
// Uses dedicated per-pass buffers to avoid QRhi dynamic buffer sharing hazards.
static void render_clip_to_backbuffer(ComposeSequenceParams& params, QRhiTexture* textureID,
                                      const GLTextureCoords& coords, const QMatrix4x4& clip_mvp,
                                      QRhiTextureRenderTarget* back_target1, QRhiRenderPassDescriptor* back_rpd) {
  if (!params.rhi) {
    qWarning() << "render_clip_to_backbuffer: params.rhi is null";
    return;
  }
  if (!params.cb) {
    qWarning() << "render_clip_to_backbuffer: params.cb is null";
    return;
  }
  if (!textureID) {
    qWarning() << "render_clip_to_backbuffer: textureID is null";
    return;
  }
  if (!back_target1) {
    qWarning() << "render_clip_to_backbuffer: back_target1 is null";
    return;
  }
  if (!back_rpd) {
    qWarning() << "render_clip_to_backbuffer: back_rpd is null";
    return;
  }
  float quad_verts[] = {
      float(coords.vertexTopLeftX),      float(coords.vertexTopLeftY),     float(coords.textureTopLeftX),
      float(coords.textureTopLeftY),     float(coords.vertexTopRightX),    float(coords.vertexTopRightY),
      float(coords.textureTopRightX),    float(coords.textureTopRightY),   float(coords.vertexBottomLeftX),
      float(coords.vertexBottomLeftY),   float(coords.textureBottomLeftX), float(coords.textureBottomLeftY),
      float(coords.vertexBottomRightX),  float(coords.vertexBottomRightY), float(coords.textureBottomRightX),
      float(coords.textureBottomRightY),
  };

  float colorMult[4] = {1, 1, 1, 1};

  // Dedicated buffers per pass -- QRhi dynamic buffers are NOT snapshotted at
  // record time, so sharing params.vertUbo/vbuf with rhi_blit_passthrough causes
  // both passes to see the last-written values at endOffscreenFrame().
  QRhiBuffer* clipVbuf = params.rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::VertexBuffer, sizeof(quad_verts));
  clipVbuf->create();
  QRhiBuffer* clipVertUbo = params.rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, 64);
  clipVertUbo->create();
  QRhiBuffer* clipFragUbo = params.rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, 16);
  clipFragUbo->create();

  QRhiShaderResourceBindings* srb = params.rhi->newShaderResourceBindings();
  srb->setBindings({
      QRhiShaderResourceBinding::uniformBuffer(0, QRhiShaderResourceBinding::VertexStage, clipVertUbo),
      QRhiShaderResourceBinding::uniformBuffer(1, QRhiShaderResourceBinding::FragmentStage, clipFragUbo),
      QRhiShaderResourceBinding::sampledTexture(2, QRhiShaderResourceBinding::FragmentStage, textureID, params.sampler),
  });
  srb->create();

  QRhiGraphicsPipeline* pipeline = params.rhi->newGraphicsPipeline();
  pipeline->setShaderStages(
      {{QRhiShaderStage::Vertex, params.passthroughVert}, {QRhiShaderStage::Fragment, params.passthroughFrag}});
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
  pipeline->setRenderPassDescriptor(back_rpd);
  if (!pipeline->create()) {
    qWarning() << "[RHI-PIPELINE] clip rendering pipeline FAILED";
  }

  // Apply clip-space correction (Y-flip + depth remap for Vulkan/Metal/D3D)
  QMatrix4x4 corrected_clip_mvp = params.rhi->clipSpaceCorrMatrix() * clip_mvp;

  QRhiResourceUpdateBatch* u = params.rhi->nextResourceUpdateBatch();
  u->updateDynamicBuffer(clipVbuf, 0, sizeof(quad_verts), quad_verts);
  u->updateDynamicBuffer(clipVertUbo, 0, 64, corrected_clip_mvp.constData());
  u->updateDynamicBuffer(clipFragUbo, 0, 16, colorMult);

  QSize sz = back_target1->pixelSize();
  QColor clearColor(0, 0, 0, 0);
  params.cb->beginPass(back_target1, clearColor, {1.0f, 0}, u);
  params.cb->setGraphicsPipeline(pipeline);
  params.cb->setViewport({0, 0, float(sz.width()), float(sz.height())});
  params.cb->setShaderResources(srb);
  const QRhiCommandBuffer::VertexInput vbufBinding(clipVbuf, 0);
  params.cb->setVertexInput(0, 1, &vbufBinding);
  params.cb->draw(4);
  params.cb->endPass();

  // Defer cleanup -- resources must survive until endOffscreenFrame()
  params.transientResources.append(pipeline);
  params.transientResources.append(srb);
  params.transientResources.append(clipVbuf);
  params.transientResources.append(clipVertUbo);
  params.transientResources.append(clipFragUbo);
}

// Composite a single video clip: retrieve texture, apply effects/transitions, render to back buffer,
// then blend onto the final compositing target. Sets gizmos_drawn if this clip's gizmos were processed.
static void composite_video_clip(Clip* c, long playhead, Sequence* s, ComposeSequenceParams& params,
                                 const QMatrix4x4& sequence_ortho, QRhiTextureRenderTarget* final_target,
                                 bool& gizmos_drawn) {
  if (!c) {
    qWarning() << "composite_video_clip: c is null";
    return;
  }
  if (!s) {
    qWarning() << "composite_video_clip: s is null";
    return;
  }
  if (!final_target) {
    qWarning() << "composite_video_clip: final_target is null";
    return;
  }
  QRhiTexture* textureID = nullptr;

  int video_width = c->media_width();
  int video_height = c->media_height();

  if (c->media() != nullptr && c->media()->get_type() == MEDIA_TYPE_FOOTAGE) {
    c->Cache(qMax(playhead, c->timeline_in()), params.scrubbing, params.nests, params.playback_speed);
    if (!c->Retrieve(params.rhi, params.cb, &params)) {
      params.texture_failed = true;
    } else {
      textureID = c->cached_rhi_tex;
    }
  }

  // Create clip FBO resources if needed
  if (c->fbo_rhi == nullptr) {
    get_or_create_clip_resources(c, params.rhi, video_width, video_height);
  }

  if (playhead < c->timeline_in(true) || playhead >= c->timeline_out(true)) return;

  bool fbo_switcher = false;
  ClipRhiResources* res = static_cast<ClipRhiResources*>(c->fbo_rhi);

  if (c->media() != nullptr) {
    if (c->media()->get_type() == MEDIA_TYPE_SEQUENCE) {
      params.nests.append(c);
      auto saved_gizmos = params.gizmos;
      textureID = amber::rendering::compose_sequence(params);
      params.gizmos = saved_gizmos;
      params.nests.removeLast();
      fbo_switcher = true;
    } else if (c->media()->get_type() == MEDIA_TYPE_FOOTAGE) {
      if (textureID != nullptr && !c->media()->to_footage()->alpha_is_premultiplied) {
        QMatrix4x4 blitMvp;
        blitMvp.ortho(-1, 1, -1, 1, -1, 1);
        // Intentional clipSpaceCorrMatrix on this intermediate pass: the extra Y-flip pairs
        // with the YUV->RGB pass flip (for YUV clips) or the texcoord swap (for RGBA clips)
        // to keep the total flip count even. See also rhi_blit_srcover.
        QByteArray emptyFrag;  // premultiply.frag has no UBO (only sampler)
        rhi_blit(params, res->rt[0], res->rpd, textureID, params.passthroughVert, params.premultiplyFrag, blitMvp,
                 emptyFrag, 0);
        textureID = res->tex[0];
        fbo_switcher = true;
      }
    }
  }

  // set up default coordinates
  GLTextureCoords coords;
  coords.grid_size = 1;
  coords.vertexTopLeftX = coords.vertexBottomLeftX = -video_width / 2;
  coords.vertexTopLeftY = coords.vertexTopRightY = -video_height / 2;
  coords.vertexTopRightX = coords.vertexBottomRightX = video_width / 2;
  coords.vertexBottomLeftY = coords.vertexBottomRightY = video_height / 2;
  coords.vertexBottomLeftZ = coords.vertexBottomRightZ = coords.vertexTopLeftZ = coords.vertexTopRightZ = 1;
  coords.textureTopLeftY = coords.textureTopRightY = coords.textureTopLeftX = coords.textureBottomLeftX = 0.0;
  coords.textureBottomLeftY = coords.textureBottomRightY = coords.textureTopRightX = coords.textureBottomRightX = 1.0;
  coords.textureTopLeftQ = coords.textureTopRightQ = coords.textureBottomRightQ = coords.textureBottomLeftQ = 1;
  coords.blendmode = -1;
  coords.opacity = 1.0;

  double timecode = get_timecode(c, playhead);

  // Apply clip effects
  for (int j = 0; j < c->effects.size(); j++) {
    Effect* e = c->effects.at(j).get();
    process_effect(c, e, timecode, coords, textureID, fbo_switcher, params.texture_failed, kTransitionNone, params);
  }

  // Apply opening transition
  if (c->opening_transition != nullptr) {
    int transition_progress = playhead - c->timeline_in(true);
    if (transition_progress < c->opening_transition->get_length()) {
      process_effect(c, c->opening_transition.get(),
                     double(transition_progress) / double(c->opening_transition->get_length()), coords, textureID,
                     fbo_switcher, params.texture_failed, kTransitionOpening, params);
    }
  }

  // Apply closing transition
  if (c->closing_transition != nullptr) {
    int transition_progress = playhead - (c->timeline_out(true) - c->closing_transition->get_length());
    if (transition_progress >= 0 && transition_progress < c->closing_transition->get_length()) {
      process_effect(c, c->closing_transition.get(),
                     double(transition_progress) / double(c->closing_transition->get_length()), coords, textureID,
                     fbo_switcher, params.texture_failed, kTransitionClosing, params);
    }
  }

  // Build per-clip MVP
  QMatrix4x4 clip_mvp = sequence_ortho;
  clip_mvp *= coords.transform;
  if (c->autoscaled() && (video_width != s->width || video_height != s->height)) {
    float width_multiplier = float(s->width) / float(video_width);
    float height_multiplier = float(s->height) / float(video_height);
    float scale_multiplier = qMin(width_multiplier, height_multiplier);
    clip_mvp.scale(scale_multiplier, scale_multiplier, 1);
  }

  if (params.gizmos != nullptr && params.gizmos->parent_clip == c) {
    params.gizmos->gizmo_draw(timecode, coords);
    params.gizmos->gizmo_world_to_screen(clip_mvp);
    gizmos_drawn = true;
  }

  if (textureID == nullptr) return;

  // Determine backend targets for this clip
  QRhiTextureRenderTarget* back_target1;
  QRhiTexture* back_tex1;
  QRhiRenderPassDescriptor* back_rpd;
  if (params.nests.size() > 0 && params.nests.last()->fbo_rhi != nullptr) {
    ClipRhiResources* nestRes = static_cast<ClipRhiResources*>(params.nests.last()->fbo_rhi);
    back_target1 = nestRes->rt[1];
    back_tex1 = nestRes->tex[1];
    back_rpd = nestRes->rpd;
  } else {
    back_target1 = params.backend_target1;
    back_tex1 = params.backend_tex1;
    back_rpd = params.backend_rpd;
  }

  // The RGBA path has one fewer render pass than YUV (no YUV->RGB shader pass).
  // On non-OpenGL backends, each offscreen pass flips the image (either via
  // clipSpaceCorrMatrix on Vulkan, or inherent framebuffer convention on Metal/D3D).
  // The missing pass leaves the RGBA path with an odd flip count — compensate here.
  // isYUpInFramebuffer() is true only on OpenGL where no compensation is needed.
  if (!params.rhi->isYUpInFramebuffer() && c->rgba_tex != nullptr && c->cached_rhi_tex == c->rgba_tex) {
    std::swap(coords.textureTopLeftY, coords.textureBottomLeftY);
    std::swap(coords.textureTopRightY, coords.textureBottomRightY);
  }

  // Render clip into back buffer with clip_mvp transform
  render_clip_to_backbuffer(params, textureID, coords, clip_mvp, back_target1, back_rpd);

  // Composite foreground (clip in back_tex1) onto main target using SrcOver blending.
  // The main target has PreserveColorContents, so existing content is the background.
  // For non-normal blend modes, we'd need the blending shader (TODO).
  if (!amber::CurrentRuntimeConfig.disable_blending) {
    rhi_blit_srcover(params, final_target, params.main_rpd, back_tex1, coords.opacity);
  } else {
    rhi_blit_passthrough(params, final_target, params.main_rpd, back_tex1, coords.opacity);
  }
}

// Process a single audio clip: handle nested sequences or cache audio data.
static void process_audio_clip(Clip* c, long playhead, ComposeSequenceParams& params) {
  if (!c) {
    qWarning() << "process_audio_clip: c is null";
    return;
  }
  if (c->media() != nullptr && c->media()->get_type() == MEDIA_TYPE_SEQUENCE) {
    params.nests.append(c);
    amber::rendering::compose_sequence(params);
    params.nests.removeLast();
  } else {
    bool got_mutex2 = false;

    if (params.wait_for_mutexes) {
      c->cache_lock.lock();
      got_mutex2 = true;
    } else {
      got_mutex2 = c->cache_lock.tryLock(got_mutex2);
    }

    if (got_mutex2) {
      c->cache_lock.unlock();
      c->Cache(playhead, (params.viewer != nullptr && !params.viewer->playing), params.nests, params.playback_speed);
    }
  }
}

QRhiTexture* amber::rendering::compose_sequence(ComposeSequenceParams& params) {
  if (!params.seq) {
    qWarning() << "compose_sequence: params.seq is null";
    return nullptr;
  }
  QRhiTextureRenderTarget* final_target = params.main_target;
  QRhiTexture* final_tex = params.main_tex;

  Sequence* s = params.seq;
  long playhead = s->playhead;

  if (!params.nests.isEmpty()) {
    for (auto nest : params.nests) {
      if (nest->media() == nullptr) continue;
      s = nest->media()->to_sequence().get();
      if (s == nullptr) continue;
      playhead += nest->clip_in(true) - nest->timeline_in(true);
      playhead = rescale_frame_number(playhead, nest->sequence->frame_rate, s->frame_rate);
    }

    if (params.video && params.nests.last()->fbo_rhi != nullptr) {
      ClipRhiResources* nestRes = static_cast<ClipRhiResources*>(params.nests.last()->fbo_rhi);
      // Clear nest FBO[0]
      QColor clearColor(0, 0, 0, 0);
      params.cb->beginPass(nestRes->rt[0], clearColor, {1.0f, 0});
      params.cb->endPass();
      final_target = nestRes->rt[0];
      final_tex = nestRes->tex[0];
    }
  }

  int audio_track_count = 0;
  bool gizmos_drawn = false;

  QVector<Clip*> current_clips = collect_active_clips(s, playhead, params, audio_track_count);

  QMatrix4x4 sequence_ortho;
  if (params.video) {
    int half_width = s->width / 2;
    int half_height = s->height / 2;
    sequence_ortho.setToIdentity();
    sequence_ortho.ortho(-half_width, half_width, -half_height, half_height, -1, 10);
  }

  // loop through current clips
  for (auto c : current_clips) {
    bool got_mutex = true;

    if (params.wait_for_mutexes) {
      c->state_change_lock.lock();
    } else {
      got_mutex = c->state_change_lock.tryLock();
    }

    if (got_mutex && c->IsOpen()) {
      if (c->track() < 0) {
        composite_video_clip(c, playhead, s, params, sequence_ortho, final_target, gizmos_drawn);
      } else {
        process_audio_clip(c, playhead, params);
      }
    } else {
      params.texture_failed = true;
    }

    if (got_mutex) {
      c->state_change_lock.unlock();
    }
  }

  // Clear gizmos if the clip wasn't rendered this frame (e.g. playhead at clip end).
  // Prevents ViewerOverlay from drawing stale gizmo positions.
  if (params.gizmos != nullptr && !gizmos_drawn) {
    params.gizmos = nullptr;
  }

  if (audio_track_count == 0) {
    WakeAudioWakeObject();
  }

  if (!params.nests.isEmpty() && params.nests.last()->fbo_rhi != nullptr) {
    ClipRhiResources* nestRes = static_cast<ClipRhiResources*>(params.nests.last()->fbo_rhi);
    return nestRes->tex[0];
  }

  return nullptr;
}

void amber::rendering::compose_audio(Viewer* viewer, Sequence* seq, int playback_speed, bool wait_for_mutexes) {
  if (!seq) {
    qWarning() << "compose_audio: seq is null";
    return;
  }
  ComposeSequenceParams params;
  params.viewer = viewer;
  params.rhi = nullptr;
  params.cb = nullptr;
  params.seq = seq;
  params.video = false;
  params.gizmos = nullptr;
  params.wait_for_mutexes = wait_for_mutexes;
  params.playback_speed = playback_speed;
  params.scrubbing = (viewer != nullptr && !viewer->playing);
  compose_sequence(params);
}

long rescale_frame_number(long framenumber, double source_frame_rate, double target_frame_rate) {
  return qRound((double(framenumber) / source_frame_rate) * target_frame_rate);
}

double get_timecode(Clip* c, long playhead) {
  if (!c) {
    qWarning() << "get_timecode: c is null";
    return 0.0;
  }
  if (!c->sequence) {
    qWarning() << "get_timecode: c->sequence is null";
    return 0.0;
  }
  return double(playhead_to_clip_frame(c, playhead)) / c->sequence->frame_rate;
}

long playhead_to_clip_frame(Clip* c, long playhead) {
  if (!c) {
    qWarning() << "playhead_to_clip_frame: c is null";
    return 0;
  }
  long frame = (qMax(0L, playhead - c->timeline_in(true)) + c->clip_in(true));

  // Loop/clamp wrapping
  long source_length = c->media_length();
  if (source_length > 0 && frame >= source_length) {
    long usable = source_length - c->clip_in(true);
    if (usable > 0) {
      switch (c->loop_mode()) {
        case kLoopLoop:
          frame = c->clip_in(true) + ((frame - c->clip_in(true)) % usable);
          break;
        case kLoopClamp:
          frame = source_length - 1;
          break;
        default:  // kLoopNone — existing behavior, returns past-end frame
          break;
      }
    }
  }

  return frame;
}

double playhead_to_clip_seconds(Clip* c, long playhead) {
  if (!c) {
    qWarning() << "playhead_to_clip_seconds: c is null";
    return 0.0;
  }
  if (!c->sequence) {
    qWarning() << "playhead_to_clip_seconds: c->sequence is null";
    return 0.0;
  }
  long clip_frame = playhead_to_clip_frame(c, playhead);

  if (c->reversed()) {
    clip_frame = c->media_length() - clip_frame - 1;
  }

  double secs = (double(clip_frame) / c->sequence->frame_rate) * c->speed().value;
  if (c->media() != nullptr && c->media()->get_type() == MEDIA_TYPE_FOOTAGE) {
    secs *= c->media()->to_footage()->speed;
  }

  return secs;
}

int64_t seconds_to_timestamp(Clip* c, double seconds) {
  if (!c) {
    qWarning() << "seconds_to_timestamp: c is null";
    return 0;
  }
  return qRound64(seconds * av_q2d(av_inv_q(c->time_base())));
}

int64_t playhead_to_timestamp(Clip* c, long playhead) {
  if (!c) {
    qWarning() << "playhead_to_timestamp: c is null";
    return 0;
  }
  return seconds_to_timestamp(c, playhead_to_clip_seconds(c, playhead));
}

void close_active_clips(Sequence* s) {
  if (s != nullptr) {
    for (const auto& clip : s->clips) {
      Clip* c = clip.get();
      if (c != nullptr) {
        c->Close(true);
      }
    }
  }
}
