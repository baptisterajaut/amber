#ifndef SUBTITLEEFFECT_H
#define SUBTITLEEFFECT_H

#include <QMutex>

#include "effects/effect.h"
#include "srtparser.h"

class QPainter;
class QTextDocument;

class SubtitleEffect : public Effect {
  Q_OBJECT
 public:
  SubtitleEffect(Clip* c, const EffectMeta* em);

  void redraw(double timecode) override;
  QRhiTexture* process_superimpose(QRhi* rhi, QRhiResourceUpdateBatch* u, double timecode) override;

  void custom_load(QXmlStreamReader& stream) override;
  void save(QXmlStreamWriter& stream) override;
  EffectPtr copy(Clip* c) override;

  // Force redraw every frame: the active subtitle entry depends on the playhead
  // (timecode), not on any keyframed EffectField. Without this, the base
  // Effect::valueHasChanged() short-circuit can skip a re-render when scrubbing
  // past an entry boundary, leaving the previous overlay on screen.
  bool AlwaysUpdate() override { return true; }

  // Main thread. The render thread reads the cues while drawing: they are swapped under a lock and the next frame
  // redraws even when the same cue stays active (its text or times may have changed)
  void SetCues(const QVector<SubtitleCue>& cues);
  QVector<SubtitleCue> cues() const;

  // Bumped by every SetCues()/load, for views that mirror the cues (the Subtitle Editor)
  int cues_revision() const;

 private slots:
  void outline_enable(bool e);
  void shadow_enable(bool e);

 private:
  static int find_active_cue(const QVector<SubtitleCue>& cues, qint64 time_ms);
  static QString CollectActiveCues(const QVector<SubtitleCue>& cues, qint64 time_ms);
  void DrawSubtitleShadow(QPainter& p, QTextDocument& td, const QRect& clip_rect, int translate_x, int translate_y,
                          double timecode);
  void DrawSubtitleOutline(QPainter& p, QTextDocument& td, const QRect& clip_rect, int translate_x, int translate_y,
                           double timecode);

  mutable QMutex cues_mutex_;
  QVector<SubtitleCue> cues_;  // written under cues_mutex_
  int cues_revision_{0};
  bool cues_dirty_{false};  // under cues_mutex_: set by SetCues(), cleared by the render thread when it redraws
  int active_cue_index_{-1};

  // Style fields
  FontField* font_field_;
  DoubleField* size_field_;
  ColorField* color_field_;
  ComboField* halign_field_;
  ComboField* valign_field_;
  BoolField* wordwrap_field_;
  DoubleField* padding_field_;

  BoolField* outline_field_;
  DoubleField* outline_width_field_;
  ColorField* outline_color_field_;

  BoolField* shadow_field_;
  DoubleField* shadow_angle_field_;
  DoubleField* shadow_distance_field_;
  ColorField* shadow_color_field_;
  DoubleField* shadow_softness_field_;
  DoubleField* shadow_opacity_field_;

  // Read-only display of active cue (non-savable)
  StringField* current_cue_field_;
};

#endif  // SUBTITLEEFFECT_H
