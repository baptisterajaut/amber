#ifndef SUBTITLEEFFECT_H
#define SUBTITLEEFFECT_H

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

  void SetCues(const QVector<SubtitleCue>& cues);

 private slots:
  void outline_enable(bool e);
  void shadow_enable(bool e);

 private:
  int find_active_cue(qint64 time_ms) const;
  QString CollectActiveCues(qint64 time_ms) const;
  void DrawSubtitleShadow(QPainter& p, QTextDocument& td, const QRect& clip_rect, int translate_x, int translate_y,
                          double timecode);
  void DrawSubtitleOutline(QPainter& p, QTextDocument& td, const QRect& clip_rect, int translate_x, int translate_y,
                           double timecode);

  QVector<SubtitleCue> cues_;
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
