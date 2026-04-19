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

#ifndef RENDERFUNCTIONS_H
#define RENDERFUNCTIONS_H

#include <QVector>
#include <rhi/qrhi.h>
#include <rhi/qshader.h>

#include "engine/sequence.h"
#include "effects/effect.h"

// Per-clip QRhi FBO resources (ping-pong textures for effect processing).
// Stored as void* in Clip::fbo_rhi to avoid circular includes.
//
// rt[j] are PreserveColorContents render targets (LoadOp::Load) used for
// accumulating composites via srcover. rt_clear[j] are non-preserve
// (LoadOp::Clear) aliases wrapping the same tex[j] — needed because a
// beginPass(clearColor) on a PreserveColorContents target is a no-op.
// rt_clear entries are only allocated for nested-sequence clips (count==3),
// where rt[0] is the nested accumulator and rt[1] is the per-inner-clip
// scratch back-buffer — both need to be explicitly cleared between uses
// to prevent transparency leaks (closes #24).
struct ClipRhiResources {
  QRhiTexture* tex[3] = {};
  QRhiTextureRenderTarget* rt[3] = {};
  QRhiTextureRenderTarget* rt_clear[3] = {};
  QRhiRenderPassDescriptor* rpd = nullptr;
  QRhiRenderPassDescriptor* clear_rpd = nullptr;
  int count = 0;
};

struct ComposeSequenceParams {
  QRhi* rhi;
  QRhiCommandBuffer* cb;
  Sequence* seq;
  QVector<Clip*> nests;
  bool video;
  Effect* gizmos;
  bool texture_failed;
  bool wait_for_mutexes;
  int playback_speed;
  bool scrubbing{false};

  // Shared sampler (created by RenderThread, reused across all blit passes)
  QRhiSampler* sampler;

  // Core shaders (loaded from QRC .qsb)
  QShader passthroughVert;
  QShader passthroughFrag;
  QShader blendingFrag;
  QShader premultiplyFrag;
  QShader yuvFrag;

  // Main compositing target (PreserveColorContents)
  QRhiTexture* main_tex;
  QRhiTextureRenderTarget* main_target;
  QRhiRenderPassDescriptor* main_rpd;

  // Backend texture/target (single — used for clip→main compositing)
  QRhiTexture* backend_tex1;
  QRhiTextureRenderTarget* backend_target1;
  QRhiRenderPassDescriptor* backend_rpd;

  // Transient resources (pipelines, SRBs, UBOs) created during recording.
  // Must stay alive until endOffscreenFrame() — caller deletes after frame ends.
  QVector<QRhiResource*> transientResources;
};

namespace amber {
namespace rendering {

QRhiTexture* compose_sequence(ComposeSequenceParams &params);

void compose_audio(Sequence *seq, bool scrubbing, int playback_speed, bool wait_for_mutexes);
}
}

long rescale_frame_number(long framenumber, double source_frame_rate, double target_frame_rate);
double get_timecode(Clip *c, long playhead);
long playhead_to_clip_frame(Clip* c, long playhead);
double playhead_to_clip_seconds(Clip *c, long playhead);
int64_t seconds_to_timestamp(Clip* c, double seconds);
int64_t playhead_to_timestamp(Clip *c, long playhead);
void close_active_clips(Sequence* s);

#endif // RENDERFUNCTIONS_H
