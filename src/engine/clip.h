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

#ifndef CLIP_H
#define CLIP_H

#include <memory>
#include <QWaitCondition>
#include <QMutex>
#include <QVector>
#include <rhi/qrhi.h>

#include "cacher.h"

#include "effects/effect.h"
#include "effects/transition.h"
#include "engine/undo/comboaction.h"
#include "project/media.h"
#include "project/footage.h"

#include "core/marker.h"

extern "C" {
#include <libavformat/avformat.h>
#include <libavfilter/avfilter.h>
}

enum ClipLoopMode { kLoopNone = 0, kLoopLoop = 1, kLoopClamp = 2 };

struct ClipSpeed {
  ClipSpeed();
  double value{1.0};
  bool maintain_audio_pitch{false};
};

using ClipPtr = std::shared_ptr<Clip>;

struct ComposeSequenceParams;
class Sequence;

class Clip {
public:
  Clip(Sequence *s);
  ~Clip();
  ClipPtr copy(Sequence *s);

  bool IsActiveAt(long timecode);
  bool IsSelected(bool containing = true);

  const QColor& color() const;
  void set_color(int r, int g, int b);
  void set_color(const QColor& c);

  QColor display_color() const;
  int color_label() const;
  void set_color_label(int label);
  int* color_label_ptr();

  int loop_mode() const;
  void set_loop_mode(int mode);
  int* loop_mode_ptr();

  Media* media();
  FootageStream* media_stream();
  int media_stream_index();
  int media_width();
  int media_height();
  double media_frame_rate();
  long media_length();
  void set_media(Media* m, int s);

  bool enabled();
  void set_enabled(bool e);

  void move(ComboAction* ca,
            long iin,
            long iout,
            long iclip_in,
            int itrack,
            bool verify_transitions = true,
            bool relative = false);

  long clip_in(bool with_transition = false);
  void set_clip_in(long c);

  long timeline_in(bool with_transition = false);
  void set_timeline_in(long t);

  long timeline_out(bool with_transition = false);
  void set_timeline_out(long t);

  int track();
  void set_track(int t);

  bool reversed();
  void set_reversed(bool r);

  bool autoscaled();
  void set_autoscaled(bool b);

  double cached_frame_rate();
  void set_cached_frame_rate(double d);

  const QString& name();
  void set_name(const QString& s);

  const ClipSpeed& speed();
  void set_speed(const ClipSpeed& s);

  AVRational time_base();

  void reset_audio();
  void refresh();

  long length();

  void refactor_frame_rate(ComboAction* ca, double multiplier, bool change_timeline_points);
  Sequence* sequence;

  // markers
  QVector<Marker>& get_markers();

  // other variables (should be deep copied/duplicated in copy())
  int IndexOfEffect(Effect* e);
  QList<EffectPtr> effects;
  QVector<int> linked;
  TransitionPtr opening_transition;
  TransitionPtr closing_transition;

  // playback functions
  void Open();
  void Cache(long playhead, bool scrubbing, QVector<Clip*> &nests, int playback_speed);
  bool Retrieve(QRhi* rhi, QRhiCommandBuffer* cb, ComposeSequenceParams* params);
  void Close(bool wait);
  bool IsOpen();

  bool UsesCacher();

  // temporary variables
  int load_id;
  bool undeletable{false};
  bool replaced{false};

  // caching functions
  QMutex state_change_lock;
  QMutex cache_lock;

  [[nodiscard]] bool NeedsCpuRgba() const;
  [[nodiscard]] bool NeedsCacherReconfigure() const;

  // video playback variables (QRhi)
  void* fbo_rhi{nullptr};  // ClipRhiResources* — opaque to avoid circular includes
  QRhiTexture* cached_rhi_tex{nullptr};

  // YUV plane textures (uploaded per-frame, converted via yuv2rgb shader)
  QRhiTexture* yuv_tex_y{nullptr};
  QRhiTexture* yuv_tex_u{nullptr};
  QRhiTexture* yuv_tex_v{nullptr};
  QRhiTexture* yuv_converted_tex{nullptr};
  QRhiTextureRenderTarget* yuv_rt{nullptr};
  QRhiRenderPassDescriptor* yuv_rpd{nullptr};

  // RGBA texture (CPU path)
  QRhiTexture* rgba_tex{nullptr};

  long texture_frame;

private:
  // timeline variables (should be copied in copy())
  bool enabled_{true};
  long clip_in_{0};
  long timeline_in_{0};
  long timeline_out_{0};
  int track_{0};
  QString name_;
  Media* media_{nullptr};
  int media_stream_;
  ClipSpeed speed_;
  double cached_fr_;
  bool reverse_{false};
  bool autoscale_;

  Cacher cacher;
  long cacher_frame;

  QVector<Marker> markers;
  QColor color_;
  int color_label_{0};
  int loop_mode_{kLoopNone};
  bool open_{false};
  bool cacher_uses_rgba_{false};
};

#endif // CLIP_H
