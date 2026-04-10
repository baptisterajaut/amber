#include "subtitleeffect.h"

#include <QGuiApplication>
#include <QPainter>
#include <QTextDocument>
#include <QtMath>

#include "engine/clip.h"
#include "rendering/renderthread.h"
#include "ui/blur.h"

SubtitleEffect::SubtitleEffect(Clip* c, const EffectMeta* em) : Effect(c, em) {
  SetFlags(Effect::SuperimposeFlag);

  EffectRow* font_row = new EffectRow(this, tr("Font"));
  font_field_ = new FontField(font_row, "font");
  font_field_->SetColumnSpan(2);

  EffectRow* size_row = new EffectRow(this, tr("Size"));
  size_field_ = new DoubleField(size_row, "size");
  size_field_->SetMinimum(1);
  size_field_->SetColumnSpan(2);

  EffectRow* color_row = new EffectRow(this, tr("Color"));
  color_field_ = new ColorField(color_row, "color");
  color_field_->SetColumnSpan(2);

  EffectRow* halign_row = new EffectRow(this, tr("Horizontal Alignment"));
  halign_field_ = new ComboField(halign_row, "halign");
  halign_field_->AddItem(tr("Left"), Qt::AlignLeft);
  halign_field_->AddItem(tr("Center"), Qt::AlignHCenter);
  halign_field_->AddItem(tr("Right"), Qt::AlignRight);
  halign_field_->SetColumnSpan(2);

  EffectRow* valign_row = new EffectRow(this, tr("Vertical Alignment"));
  valign_field_ = new ComboField(valign_row, "valign");
  valign_field_->AddItem(tr("Top"), Qt::AlignTop);
  valign_field_->AddItem(tr("Center"), Qt::AlignVCenter);
  valign_field_->AddItem(tr("Bottom"), Qt::AlignBottom);
  valign_field_->SetColumnSpan(2);

  EffectRow* wordwrap_row = new EffectRow(this, tr("Word Wrap"));
  wordwrap_field_ = new BoolField(wordwrap_row, "wordwrap");
  wordwrap_field_->SetColumnSpan(2);

  EffectRow* padding_row = new EffectRow(this, tr("Padding"));
  padding_field_ = new DoubleField(padding_row, "padding");
  padding_field_->SetMinimum(0);
  padding_field_->SetColumnSpan(2);

  EffectRow* outline_row = new EffectRow(this, tr("Outline"));
  outline_field_ = new BoolField(outline_row, "outline");
  outline_field_->SetColumnSpan(2);

  EffectRow* outline_width_row = new EffectRow(this, tr("Outline Width"));
  outline_width_field_ = new DoubleField(outline_width_row, "outlinewidth");
  outline_width_field_->SetMinimum(0);
  outline_width_field_->SetColumnSpan(2);

  EffectRow* outline_color_row = new EffectRow(this, tr("Outline Color"));
  outline_color_field_ = new ColorField(outline_color_row, "outlinecolor");
  outline_color_field_->SetColumnSpan(2);

  EffectRow* shadow_row = new EffectRow(this, tr("Shadow"));
  shadow_field_ = new BoolField(shadow_row, "shadow");
  shadow_field_->SetColumnSpan(2);

  EffectRow* shadow_angle_row = new EffectRow(this, tr("Shadow Angle"));
  shadow_angle_field_ = new DoubleField(shadow_angle_row, "shadowangle");
  shadow_angle_field_->SetColumnSpan(2);

  EffectRow* shadow_distance_row = new EffectRow(this, tr("Shadow Distance"));
  shadow_distance_field_ = new DoubleField(shadow_distance_row, "shadowdistance");
  shadow_distance_field_->SetMinimum(0);
  shadow_distance_field_->SetColumnSpan(2);

  EffectRow* shadow_color_row = new EffectRow(this, tr("Shadow Color"));
  shadow_color_field_ = new ColorField(shadow_color_row, "shadowcolor");
  shadow_color_field_->SetColumnSpan(2);

  EffectRow* shadow_softness_row = new EffectRow(this, tr("Shadow Softness"));
  shadow_softness_field_ = new DoubleField(shadow_softness_row, "shadowsoftness");
  shadow_softness_field_->SetMinimum(0);
  shadow_softness_field_->SetColumnSpan(2);

  EffectRow* shadow_opacity_row = new EffectRow(this, tr("Shadow Opacity"));
  shadow_opacity_field_ = new DoubleField(shadow_opacity_row, "shadowopacity");
  shadow_opacity_field_->SetMinimum(0);
  shadow_opacity_field_->SetMaximum(100);
  shadow_opacity_field_->SetColumnSpan(2);

  // Set defaults
  font_field_->SetValueAt(0, QGuiApplication::font().family());
  size_field_->SetDefault(48);
  color_field_->SetValueAt(0, QColor(Qt::white));
  halign_field_->SetValueAt(0, Qt::AlignHCenter);
  valign_field_->SetValueAt(0, Qt::AlignBottom);
  wordwrap_field_->SetValueAt(0, true);
  padding_field_->SetDefault(20);
  outline_field_->SetValueAt(0, true);
  outline_width_field_->SetDefault(2);
  outline_color_field_->SetValueAt(0, QColor(Qt::black));
  shadow_field_->SetValueAt(0, false);
  shadow_angle_field_->SetDefault(315);
  shadow_distance_field_->SetDefault(5);
  shadow_color_field_->SetValueAt(0, QColor(Qt::black));
  shadow_softness_field_->SetDefault(5);
  shadow_opacity_field_->SetDefault(100);

  outline_enable(true);
  shadow_enable(false);

  // Current Cue display — non-savable, non-keyframable, MUST be last row
  // (Effect::load matches rows by index; non-savable rows shift the index)
  EffectRow* cue_row = new EffectRow(this, tr("Current Cue"), false, false);
  current_cue_field_ = new StringField(cue_row, "currentcue");
  current_cue_field_->SetColumnSpan(2);

  connect(outline_field_, &BoolField::Toggled, this, &SubtitleEffect::outline_enable);
  connect(shadow_field_, &BoolField::Toggled, this, &SubtitleEffect::shadow_enable);
}

void SubtitleEffect::SetCues(const QVector<SubtitleCue>& cues) {
  cues_ = cues;
  active_cue_index_ = -1;
}

int SubtitleEffect::find_active_cue(qint64 time_ms) const {
  for (int i = 0; i < cues_.size(); ++i) {
    if (time_ms >= cues_[i].start_ms && time_ms < cues_[i].end_ms) {
      return i;
    }
  }
  return -1;
}

void SubtitleEffect::outline_enable(bool e) {
  outline_width_field_->SetEnabled(e);
  outline_color_field_->SetEnabled(e);
}

void SubtitleEffect::shadow_enable(bool e) {
  shadow_angle_field_->SetEnabled(e);
  shadow_distance_field_->SetEnabled(e);
  shadow_color_field_->SetEnabled(e);
  shadow_softness_field_->SetEnabled(e);
  shadow_opacity_field_->SetEnabled(e);
}

QRhiTexture* SubtitleEffect::process_superimpose(QRhi* rhi, QRhiResourceUpdateBatch* u, double timecode) {
  bool dimensions_changed = false;
  bool redrew_image = false;

  int width = parent_clip->media_width();
  int height = parent_clip->media_height();

  if (width != img.width() || height != img.height()) {
    img = QImage(width, height, QImage::Format_RGBA8888_Premultiplied);
    dimensions_changed = true;
  }

  // Check if the active cue changed
  qint64 time_ms = qRound64(timecode * 1000.0);
  int new_cue = find_active_cue(time_ms);
  bool cue_changed = (new_cue != active_cue_index_);
  active_cue_index_ = new_cue;

  // Update main-thread UI with active cue text (capture by value to avoid cross-thread data race)
  if (cue_changed) {
    QString cue_text = new_cue >= 0 ? cues_[new_cue].text.left(80) : QString();
    QMetaObject::invokeMethod(
        this, [this, cue_text]() { current_cue_field_->SetValueAt(0, cue_text); }, Qt::QueuedConnection);
  }

  if (valueHasChanged(timecode) || dimensions_changed || cue_changed) {
    redraw(timecode);
    redrew_image = true;
  }

  if (superimposeTex_ == nullptr || dimensions_changed) {
    RenderThread::DeferRhiResourceDeletion(superimposeTex_);
    superimposeTex_ = rhi->newTexture(QRhiTexture::RGBA8, QSize(width, height));
    superimposeTex_->create();
    redrew_image = true;
  }

  if (redrew_image) {
    QRhiTextureSubresourceUploadDescription desc(img.constBits(), int(img.sizeInBytes()));
    desc.setSourceSize(QSize(width, height));
    u->uploadTexture(superimposeTex_, QRhiTextureUploadDescription({QRhiTextureUploadEntry(0, 0, desc)}));
  }

  return superimposeTex_;
}

void SubtitleEffect::DrawSubtitleShadow(QPainter& p, QTextDocument& td, const QRect& clip_rect, int translate_x,
                                        int translate_y, double timecode) {
  double angle = shadow_angle_field_->GetDoubleAt(timecode) * M_PI / 180.0;
  double distance = qFloor(shadow_distance_field_->GetDoubleAt(timecode));
  int sx = qRound(qCos(angle) * distance);
  int sy = qRound(qSin(angle) * distance);

  p.translate(sx, sy);
  QRect shadow_clip = clip_rect;
  shadow_clip.translate(-sx, -sy);
  td.drawContents(&p, shadow_clip);

  int blur_softness = qFloor(shadow_softness_field_->GetDoubleAt(timecode));
  if (blur_softness > 0) {
    p.end();
    amber::ui::blur(img, img.rect(), blur_softness, true);
    p.begin(&img);
    p.setRenderHint(QPainter::Antialiasing);
    p.translate(translate_x + sx, translate_y + sy);
  }

  p.setCompositionMode(QPainter::CompositionMode_SourceIn);
  QColor scol = shadow_color_field_->GetColorAt(timecode);
  scol.setAlphaF(shadow_opacity_field_->GetDoubleAt(timecode) * 0.01);
  p.fillRect(shadow_clip, scol);
  p.setCompositionMode(QPainter::CompositionMode_SourceOver);
  p.translate(-sx, -sy);
}

void SubtitleEffect::DrawSubtitleOutline(QPainter& p, QTextDocument& td, const QRect& clip_rect, int translate_x,
                                         int translate_y, double timecode) {
  int ow = qCeil(outline_width_field_->GetDoubleAt(timecode));
  if (ow <= 0) return;

  QColor ocol = outline_color_field_->GetColorAt(timecode);
  QImage outline_img(img.size(), QImage::Format_RGBA8888_Premultiplied);
  outline_img.fill(Qt::transparent);

  QPainter op(&outline_img);
  op.setRenderHint(QPainter::Antialiasing);
  op.translate(translate_x, translate_y);

  int steps = qBound(8, ow * 4, 24);
  for (int s = 0; s < steps; s++) {
    double a = 2.0 * M_PI * s / steps;
    int ox = qRound(qCos(a) * ow);
    int oy = qRound(qSin(a) * ow);
    op.save();
    op.translate(ox, oy);
    QRect offset_clip = clip_rect;
    offset_clip.translate(-ox, -oy);
    td.drawContents(&op, offset_clip);
    op.restore();
  }

  // Tint with outline color
  op.setCompositionMode(QPainter::CompositionMode_SourceIn);
  op.fillRect(outline_img.rect(), ocol);
  op.end();

  // Composite outline onto main image (over shadow, under text)
  p.drawImage(0, 0, outline_img);
}

static QString HalignToString(int halign) {
  if (halign == Qt::AlignLeft) return QStringLiteral("left");
  if (halign == Qt::AlignRight) return QStringLiteral("right");
  return QStringLiteral("center");
}

static int CalcVerticalOffset(int valign, int height, int doc_height) {
  if (valign == Qt::AlignVCenter) return height / 2 - doc_height / 2;
  if (valign == Qt::AlignBottom) return height - doc_height;
  return 0;
}

QString SubtitleEffect::CollectActiveCues(qint64 time_ms) const {
  QString active_text;
  for (int i = 0; i < cues_.size(); i++) {
    if (cues_[i].start_ms <= time_ms && time_ms < cues_[i].end_ms) {
      if (!active_text.isEmpty()) active_text += QLatin1String("<br>");
      active_text += cues_[i].text;
    }
  }
  return active_text;
}

void SubtitleEffect::redraw(double timecode) {
  img.fill(Qt::transparent);

  qint64 time_ms = qRound64(timecode * 1000.0);
  QString active_text = CollectActiveCues(time_ms);
  if (active_text.isEmpty()) return;

  int padding = qRound(padding_field_->GetDoubleAt(timecode));
  int width = img.width() - padding * 2;
  int height = img.height() - padding * 2;
  if (width <= 0 || height <= 0) return;

  double font_size = size_field_->GetDoubleAt(timecode);
  if (font_size <= 0) return;

  QColor text_color = color_field_->GetColorAt(timecode);
  QString font_family = font_field_->GetFontAt(timecode);
  QString align_str = HalignToString(halign_field_->GetValueAt(timecode).toInt());

  QString html =
      QStringLiteral(
          "<html><body style=\"color: %1; font-family: '%2'; font-size: %3pt; text-align: %4;\">%5</body></html>")
          .arg(text_color.name(), font_family)
          .arg(qRound(font_size))
          .arg(align_str, active_text);

  QTextDocument td;
  td.setHtml(html);
  if (wordwrap_field_->GetBoolAt(timecode)) td.setTextWidth(width);

  int doc_height = qRound(td.size().height());

  int translate_x = padding;
  int translate_y = padding + CalcVerticalOffset(valign_field_->GetValueAt(timecode).toInt(), height, doc_height);

  QRect clip_rect = img.rect();
  clip_rect.translate(-translate_x, -translate_y);

  QPainter p(&img);
  p.setRenderHint(QPainter::Antialiasing);
  p.translate(translate_x, translate_y);

  if (shadow_field_->GetBoolAt(timecode)) DrawSubtitleShadow(p, td, clip_rect, translate_x, translate_y, timecode);

  // Draw outline via multi-pass offset on a scratch image to avoid SourceIn
  // tinting the already-composited shadow pixels
  if (outline_field_->GetBoolAt(timecode)) DrawSubtitleOutline(p, td, clip_rect, translate_x, translate_y, timecode);

  // Draw main text on top
  td.drawContents(&p, clip_rect);
  p.end();
}

void SubtitleEffect::custom_load(QXmlStreamReader& stream) {
  if (stream.name() == QLatin1String("cues")) {
    cues_.clear();
    while (!stream.atEnd() && !(stream.name() == QLatin1String("cues") && stream.isEndElement())) {
      stream.readNext();
      if (stream.name() == QLatin1String("cue") && stream.isStartElement()) {
        SubtitleCue cue;
        cue.start_ms = stream.attributes().value("start").toLongLong();
        cue.end_ms = stream.attributes().value("end").toLongLong();
        cue.text = stream.readElementText();
        cues_.append(cue);
      }
    }
  }
}

void SubtitleEffect::save(QXmlStreamWriter& stream) {
  Effect::save(stream);

  stream.writeStartElement("cues");
  for (const SubtitleCue& cue : cues_) {
    stream.writeStartElement("cue");
    stream.writeAttribute("start", QString::number(cue.start_ms));
    stream.writeAttribute("end", QString::number(cue.end_ms));
    stream.writeCharacters(cue.text);
    stream.writeEndElement();
  }
  stream.writeEndElement();
}

EffectPtr SubtitleEffect::copy(Clip* c) {
  EffectPtr e = Effect::copy(c);
  SubtitleEffect* sub = static_cast<SubtitleEffect*>(e.get());
  sub->cues_ = cues_;
  return e;
}
