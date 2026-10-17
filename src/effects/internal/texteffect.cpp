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

#include "texteffect.h"

#include <QFontDatabase>
#include <QGlyphRun>
#include <QPainter>
#include <QPainterPath>
#include <QRawFont>
#include <QTextBoundaryFinder>
#include <QTextLayout>
#include <QtMath>

#include "core/blur.h"
#include "engine/clip.h"
#include "engine/sequence.h"
#include "global/config.h"

// Reveal animation: delay between consecutive characters, and how far a
// character rises into place while it fades in.
static constexpr double kRevealStagger = 0.03;  // seconds
static constexpr double kRevealRise = 20;       // px

// Returns the grapheme-cluster boundary offsets within s, including 0 and
// s.length(). Cutting s at any of these offsets (e.g. s.left(bounds[k]))
// never splits a user-perceived character — a base letter plus combining
// marks, a surrogate pair, an emoji + ZWJ sequence — in half. Used both to
// count "visible characters" for the reveal animation and to find safe
// truncation points, instead of raw UTF-16 code-unit length.
static QVector<int> grapheme_boundaries(const QString& s) {
  QVector<int> bounds;
  bounds.append(0);
  if (s.isEmpty()) return bounds;
  QTextBoundaryFinder finder(QTextBoundaryFinder::Grapheme, s);
  int pos = finder.toNextBoundary();
  while (pos != -1) {
    bounds.append(pos);
    if (pos >= s.length()) break;
    pos = finder.toNextBoundary();
  }
  return bounds;
}

// CBA: redraws every frame while Animate is on; pass the timecode to AlwaysUpdate() if that shows up in profiles
bool TextEffect::AlwaysUpdate() { return animate_in_bool->GetBoolAt(0); }

TextEffect::TextEffect(Clip* c, const EffectMeta* em) : Effect(c, em) {
  SetFlags(Effect::SuperimposeFlag);

  EffectRow* text_field = new EffectRow(this, tr("Text"));
  text_val = new StringField(text_field, "text", false);
  text_val->SetColumnSpan(2);

  EffectRow* position_row = new EffectRow(this, tr("Position"));
  position_x = new DoubleField(position_row, "posx");
  position_y = new DoubleField(position_row, "posy");

  EffectRow* reveal_row = new EffectRow(this, tr("Reveal %"));
  reveal_val = new DoubleField(reveal_row, "reveal");
  reveal_val->SetMinimum(0);
  reveal_val->SetMaximum(100);
  reveal_val->SetColumnSpan(2);

  EffectRow* animate_row = new EffectRow(this, tr("Animate"));
  animate_in_bool = new BoolField(animate_row, "animatein");
  animate_in_bool->SetColumnSpan(2);

  EffectRow* duration_row = new EffectRow(this, tr("Animate duration (sec)"));
  animate_duration = new DoubleField(duration_row, "animateduration");
  animate_duration->SetMinimum(0.1);
  animate_duration->SetColumnSpan(2);

  EffectRow* font_row = new EffectRow(this, tr("Font"));
  set_font_combobox = new FontField(font_row, "font");
  set_font_combobox->SetColumnSpan(2);

  EffectRow* size_row = new EffectRow(this, tr("Size"));
  size_val = new DoubleField(size_row, "size");
  size_val->SetMinimum(0);
  size_val->SetColumnSpan(2);

  EffectRow* color_row = new EffectRow(this, tr("Color"));
  set_color_button = new ColorField(color_row, "color");
  set_color_button->SetColumnSpan(2);

  EffectRow* alignment_row = new EffectRow(this, tr("Alignment"));
  halign_field = new ComboField(alignment_row, "halign");
  halign_field->AddItem(tr("Left"), Qt::AlignLeft);
  halign_field->AddItem(tr("Center"), Qt::AlignHCenter);
  halign_field->AddItem(tr("Right"), Qt::AlignRight);
  halign_field->AddItem(tr("Justify"), Qt::AlignJustify);

  valign_field = new ComboField(alignment_row, "valign");
  valign_field->AddItem(tr("Top"), Qt::AlignTop);
  valign_field->AddItem(tr("Center"), Qt::AlignVCenter);
  valign_field->AddItem(tr("Bottom"), Qt::AlignBottom);

  EffectRow* word_wrap_row = new EffectRow(this, tr("Word Wrap"));
  word_wrap_field = new BoolField(word_wrap_row, "wordwrap");
  word_wrap_field->SetColumnSpan(2);

  EffectRow* line_height_row = new EffectRow(this, tr("Line Height"));
  line_height_field = new DoubleField(line_height_row, "lineheight");
  line_height_field->SetMinimum(0.1);
  line_height_field->SetDefault(1.0);
  line_height_field->SetColumnSpan(2);

  EffectRow* padding_row = new EffectRow(this, tr("Padding"));
  padding_field = new DoubleField(padding_row, "padding");
  padding_field->SetColumnSpan(2);

  EffectRow* outline_row = new EffectRow(this, tr("Outline"));
  outline_bool = new BoolField(outline_row, "outline");
  outline_bool->SetColumnSpan(2);

  EffectRow* outline_color_row = new EffectRow(this, tr("Outline Color"));
  outline_color = new ColorField(outline_color_row, "outlinecolor");
  outline_color->SetColumnSpan(2);

  EffectRow* outline_width_row = new EffectRow(this, tr("Outline Width"));
  outline_width = new DoubleField(outline_width_row, "outlinewidth");
  outline_width->SetColumnSpan(2);
  outline_width->SetMinimum(0);

  EffectRow* shadow_row = new EffectRow(this, tr("Shadow"));
  shadow_bool = new BoolField(shadow_row, "shadow");
  shadow_bool->SetColumnSpan(2);

  EffectRow* shadow_color_row = new EffectRow(this, tr("Shadow Color"));
  shadow_color = new ColorField(shadow_color_row, "shadowcolor");
  shadow_color->SetColumnSpan(2);

  EffectRow* shadow_angle_row = new EffectRow(this, tr("Shadow Angle"));
  shadow_angle = new DoubleField(shadow_angle_row, "shadowangle");
  shadow_angle->SetColumnSpan(2);

  EffectRow* shadow_distance_row = new EffectRow(this, tr("Shadow Distance"));
  shadow_distance = new DoubleField(shadow_distance_row, "shadowdistance");
  shadow_distance->SetColumnSpan(2);
  shadow_distance->SetMinimum(0);

  EffectRow* shadow_softness_row = new EffectRow(this, tr("Shadow Softness"));
  shadow_softness = new DoubleField(shadow_softness_row, "shadowsoftness");
  shadow_softness->SetColumnSpan(2);
  shadow_softness->SetMinimum(0);

  EffectRow* shadow_opacity_row = new EffectRow(this, tr("Shadow Opacity"));
  shadow_opacity = new DoubleField(shadow_opacity_row, "shadowopacity");
  shadow_opacity->SetColumnSpan(2);
  shadow_opacity->SetMinimum(0);
  shadow_opacity->SetMaximum(100);

  size_val->SetDefault(48);
  text_val->SetValueAt(0, tr("Sample Text"));
  set_color_button->SetValueAt(0, QColor(Qt::white));
  halign_field->SetValueAt(0, Qt::AlignHCenter);
  valign_field->SetValueAt(0, Qt::AlignVCenter);
  word_wrap_field->SetValueAt(0, true);
  outline_color->SetValueAt(0, QColor(Qt::black));
  shadow_color->SetValueAt(0, QColor(Qt::black));
  shadow_angle->SetDefault(45);
  shadow_opacity->SetDefault(100);
  shadow_softness->SetDefault(5);
  shadow_distance->SetDefault(5);
  shadow_opacity->SetDefault(80);
  outline_width->SetDefault(20);
  reveal_val->SetDefault(100);
  animate_duration->SetDefault(0.5);

  outline_enable(false);
  shadow_enable(false);

  connect(shadow_bool, &BoolField::Toggled, this, &TextEffect::shadow_enable);
  connect(outline_bool, &BoolField::Toggled, this, &TextEffect::outline_enable);

  vertPath = "common.vert";
  fragPath = "dropshadow.frag";
}

void TextEffect::ApplyWordWrap(QStringList& lines, const QFontMetrics& fm, int width) {
  for (int i = 0; i < lines.size(); i++) {
    const QString& s = lines.at(i);
    if (fm.horizontalAdvance(s) <= width) continue;
    int last_space_index = 0;
    for (int j = 0; j < s.length(); j++) {
      if (s.at(j) == ' ') {
        if (fm.horizontalAdvance(s.left(j)) > width) break;
        last_space_index = j;
      }
    }
    if (last_space_index > 0) {
      lines.insert(i + 1, s.mid(last_space_index + 1));
      lines[i] = s.left(last_space_index);
    }
  }
}

int TextEffect::ComputeTextX(const QString& line, int halign, const QFontMetrics& fm, int width) {
  if (halign == Qt::AlignLeft) return 0;
  if (halign == Qt::AlignRight) return width - fm.horizontalAdvance(line);
  if (halign == Qt::AlignHCenter) return (width / 2) - (fm.horizontalAdvance(line) / 2);

  // AlignJustify: expand spaces until line fills width
  // (modifies a local copy; the outer loop must use the return value)
  return 0;
}

int TextEffect::ComputeTextY(int i, int valign, const QFontMetrics& fm, int line_h, int text_height, int height) {
  if (valign == Qt::AlignTop) return line_h * i + fm.ascent();
  if (valign == Qt::AlignBottom) return (height - text_height - fm.descent()) + line_h * i + fm.height();
  // AlignVCenter (default)
  return ((height / 2) - (text_height / 2) - fm.descent()) + line_h * i + fm.height();
}

void TextEffect::DrawShadow(QPainter& p, const QVector<ClusterPath>& clusters, double timecode) {
  p.setPen(Qt::NoPen);
  double angle = shadow_angle->GetDoubleAt(timecode) * M_PI / 180.0;
  double distance = qFloor(shadow_distance->GetDoubleAt(timecode));
  int sx = qRound(qCos(angle) * distance);
  int sy = qRound(qSin(angle) * distance);

  QColor col = shadow_color->GetColorAt(timecode);
  col.setAlpha(0);
  img.fill(col);

  // Characters are drawn at their reveal alpha only, so overlapping joins
  // (Arabic, script fonts) don't stack into darker seams; the opacity is
  // applied once to the whole shadow layer afterwards.
  p.save();
  p.translate(sx, sy);
  for (const ClusterPath& cp : clusters) {
    col.setAlphaF(cp.progress);
    p.setBrush(col);
    p.drawPath(cp.path);
  }
  p.resetTransform();
  p.setCompositionMode(QPainter::CompositionMode_DestinationIn);
  p.fillRect(img.rect(), QColor(0, 0, 0, qRound(shadow_opacity->GetDoubleAt(timecode) * 2.55)));
  p.restore();

  int blurSoftness = qFloor(shadow_softness->GetDoubleAt(timecode));
  if (blurSoftness > 0) amber::blur(img, img.rect(), blurSoftness, true);
}

// All outlines before any fill (see redraw()), so a letter's outline never
// cuts into its neighbour's fill (Arabic joins).
void TextEffect::DrawOutline(QPainter& p, const QVector<ClusterPath>& clusters, double timecode) {
  int outline_width_val = qCeil(outline_width->GetDoubleAt(timecode));
  if (outline_width_val <= 0) return;
  QColor outline_base_color = outline_color->GetColorAt(timecode);
  p.setBrush(Qt::NoBrush);
  for (const ClusterPath& cp : clusters) {
    QColor oc = outline_base_color;
    oc.setAlphaF(oc.alphaF() * cp.progress);
    QPen pen(oc);
    pen.setWidth(outline_width_val);
    p.setPen(pen);
    p.drawPath(cp.path);
  }
}

void TextEffect::redraw(double timecode) {
  if (size_val->GetDoubleAt(timecode) <= 0) return;

  QColor bkg = set_color_button->GetColorAt(timecode);
  bkg.setAlpha(0);
  img.fill(bkg);

  QPainter p(&img);
  p.setRenderHint(QPainter::Antialiasing);
  int padding = qRound(padding_field->GetDoubleAt(timecode));
  int width = img.width() - padding * 2;
  int height = img.height() - padding * 2;

  font.setStyleHint(QFont::Helvetica, QFont::PreferAntialias);
  font.setFamily(set_font_combobox->GetFontAt(timecode));
  font.setPointSize(qRound(size_val->GetDoubleAt(timecode)));
  p.setFont(font);
  QFontMetrics fm(font);

  QStringList lines = text_val->GetStringAt(timecode).split('\n');

  if (word_wrap_field->GetBoolAt(timecode)) ApplyWordWrap(lines, fm, width);

  // Truncate the revealed text without disturbing the line breaks the word-wrap
  // pass just computed. Count and cut at grapheme-cluster boundaries, not raw
  // UTF-16 code units, so combining marks, surrogate pairs, and joined
  // sequences never get split mid-cluster before shaping even sees them.
  QVector<QVector<int>> line_bounds;
  int total_graphemes = 0;
  for (const QString& l : lines) {
    QVector<int> b = grapheme_boundaries(l);
    line_bounds.append(b);
    total_graphemes += b.size() - 1;
  }

  int graphemes_to_show = qRound(total_graphemes * (reveal_val->GetDoubleAt(timecode) * 0.01));

  QStringList visible_lines;
  int remaining = graphemes_to_show;
  for (int li = 0; li < lines.size(); ++li) {
    const QString& l = lines.at(li);
    const QVector<int>& b = line_bounds.at(li);
    int line_grapheme_count = b.size() - 1;
    if (remaining <= 0) {
      visible_lines.append(QString());  // keep the slot so valign/height stays stable
    } else if (line_grapheme_count <= remaining) {
      visible_lines.append(l);
      remaining -= line_grapheme_count;
    } else {
      visible_lines.append(l.left(b.at(remaining)));  // cut at a real grapheme boundary
      remaining = 0;
    }
  }
  lines = visible_lines;

  // baseline anchor of each line, reused by the shaping pass below
  QVector<QPoint> anchors;
  int line_h = qRound(fm.height() * line_height_field->GetDoubleAt(timecode));
  int text_height = (lines.size() - 1) * line_h + fm.height();
  int halign = halign_field->GetValueAt(timecode).toInt();
  int valign = valign_field->GetValueAt(timecode).toInt();

  for (int i = 0; i < lines.size(); i++) {
    int text_x = ComputeTextX(lines.at(i), halign, fm, width);
    // Justify: expand spaces until the line fills the width
    if (halign == Qt::AlignJustify) {
      while (fm.horizontalAdvance(lines.at(i)) < width) {
        bool space = false;
        QString spaced(lines.at(i));
        for (int j = 0; j < spaced.length(); j++) {
          if (spaced.at(j) == ' ') {
            spaced.insert(j, ' ');
            space = true;
            while (j < spaced.length() && spaced.at(j) == ' ') j++;
          }
        }
        if (fm.horizontalAdvance(spaced) > width || !space) break;
        lines[i] = spaced;
      }
    }
    int text_y = ComputeTextY(i, valign, fm, line_h, text_height, height);
    anchors.append(QPoint(text_x, text_y));
  }

  // Reveal animation timing, in seconds since the clip started on the timeline.
  // timecode is counted from the start of the source media, so subtract the
  // clip's in point.
  bool animate = animate_in_bool->GetBoolAt(timecode);
  double curtime = 0;
  double clipduration = 0;
  if (animate && parent_clip != nullptr && parent_clip->sequence != nullptr && parent_clip->sequence->frame_rate > 0) {
    double frame_rate = parent_clip->sequence->frame_rate;
    curtime = timecode - parent_clip->clip_in(true) / frame_rate;
    clipduration = parent_clip->length() / frame_rate;
  } else {
    animate = false;
  }
  double duration = qMax(0.01, animate_duration->GetDoubleAt(timecode));

  QVector<ClusterPath> cluster_paths;
  int global_char_index = 0;

  for (int i = 0; i < lines.size(); i++) {
    const QString& line = lines.at(i);
    if (line.isEmpty()) continue;

    // Shape the whole line once so kerning, ligatures, and script-specific
    // reordering (Arabic joining, Devanagari matra placement, etc.) are all
    // resolved correctly, then walk the result one grapheme cluster at a
    // time so each visible "character" can still get its own reveal/rise
    // animation without re-shaping it in isolation.
    QTextLayout layout(line, font);
    layout.beginLayout();
    QTextLine text_line = layout.createLine();
    text_line.setLineWidth(1e6);  // word-wrap already happened above; don't re-wrap here
    layout.endLayout();

    // QTextLine positions glyphs relative to the line's own top-left; convert
    // the baseline anchor computed above to that origin.
    double anchor_x = anchors.at(i).x();
    double anchor_y = anchors.at(i).y() - text_line.ascent();

    QVector<int> clusters = grapheme_boundaries(line);

    for (int ci = 0; ci < clusters.size() - 1; ++ci) {
      int cluster_start = clusters.at(ci);
      int cluster_len = clusters.at(ci + 1) - cluster_start;

      // Before the clip starts / after it ends this just clamps to hidden.
      double progress = 1.0;
      if (animate) {
        double t_in = (curtime - kRevealStagger * global_char_index) / duration;
        double progress_in = qBound(0.0, t_in, 1.0);
        progress_in = progress_in * progress_in * (3.0 - 2.0 * progress_in);

        double t_out = (clipduration - curtime - kRevealStagger * global_char_index) / duration;
        double progress_out = qBound(0.0, t_out, 1.0);
        progress_out = progress_out * progress_out * (3.0 - 2.0 * progress_out);

        progress = qMin(progress_in, progress_out);
      }

      double y_offset = (1.0 - progress) * kRevealRise;

      // Winding fill so overlapping contours within a cluster (Devanagari
      // conjuncts, stacked marks) union instead of cancelling.
      QPainterPath cluster_path;
      cluster_path.setFillRule(Qt::WindingFill);

      const QList<QGlyphRun> runs = text_line.glyphRuns(cluster_start, cluster_len);
      for (const QGlyphRun& run : runs) {
        QRawFont raw_font = run.rawFont();
        const QVector<quint32> glyph_indexes = run.glyphIndexes();
        const QVector<QPointF> positions = run.positions();

        for (int g = 0; g < glyph_indexes.size(); ++g) {
          QPainterPath glyph_path = raw_font.pathForGlyph(glyph_indexes.at(g));

          QTransform tf;
          tf.translate(anchor_x + positions.at(g).x(), anchor_y + positions.at(g).y() + y_offset);
          cluster_path.addPath(tf.map(glyph_path));
        }
      }

      cluster_paths.append({cluster_path, progress});
      global_char_index++;
    }
  }

  p.translate(position_x->GetDoubleAt(timecode) + padding, position_y->GetDoubleAt(timecode) + padding);

  if (shadow_bool->GetBoolAt(timecode) && !force_disable_shadow_rendering) DrawShadow(p, cluster_paths, timecode);

  if (outline_bool->GetBoolAt(timecode)) DrawOutline(p, cluster_paths, timecode);

  // draw "master" text
  QColor fill_color = set_color_button->GetColorAt(timecode);
  p.setPen(Qt::NoPen);
  for (const ClusterPath& cp : cluster_paths) {
    QColor c = fill_color;
    c.setAlphaF(c.alphaF() * cp.progress);
    p.setBrush(c);
    p.drawPath(cp.path);
  }

  p.end();
}

void TextEffect::shadow_enable(bool e) {
  shadow_color->SetEnabled(e);
  shadow_angle->SetEnabled(e);
  shadow_distance->SetEnabled(e);
  shadow_softness->SetEnabled(e);
  shadow_opacity->SetEnabled(e);
}

void TextEffect::outline_enable(bool e) {
  outline_color->SetEnabled(e);
  outline_width->SetEnabled(e);
}
