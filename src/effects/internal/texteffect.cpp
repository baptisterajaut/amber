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

#include <QGridLayout>
#include <QLabel>
#include <QTextEdit>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QColorDialog>
#include <QFontDatabase>
#include <QComboBox>
#include <QWidget>
#include <QtMath>
#include <QMenu>
#include <QTextLayout>
#include <QTextBoundaryFinder>
#include <QGlyphRun>
#include <QRawFont>

#include "ui/labelslider.h"
#include "ui/collapsiblewidget.h"
#include "engine/clip.h"
#include "engine/sequence.h"
#include "ui/comboboxex.h"
#include "ui/colorbutton.h"
#include "ui/fontcombobox.h"
#include "ui/blur.h"
#include "global/config.h"
          
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
bool TextEffect::AlwaysUpdate() {
  if (!animate_in_bool->GetBoolAt(0)) return false;

  if (!amber::ActiveSequence || !parent_clip) return true;  // fail safe if state's unavailable

  double current_frame = amber::ActiveSequence->playhead;
  double clip_start = parent_clip->timeline_in();
  double clip_end = parent_clip->timeline_out();
  double fps = amber::ActiveSequence->frame_rate;
  if (fps <= 0.0) fps = 29.97;
  double frame_duration = 1.0 / fps;
  double curtime = (current_frame - clip_start) * frame_duration;
  double clipduration = (clip_end - clip_start) * frame_duration;
  double duration = qMax(0.01, animate_duration->GetDoubleAt(0));

  // Conservative margin: the last character's fade-in (or first character's
  // fade-out) starts stagger * character_count seconds after/before the
  // clip's edge, so widen the window rather than track exact bounds.
  double rough_char_count = text_val->GetStringAt(0).length();
  double margin = 0.03 * rough_char_count;  // matches redraw()'s hardcoded stagger

  bool fading_in = curtime < (duration + margin);
  bool fading_out = (clipduration - curtime) < (duration + margin);
  return fading_in || fading_out;
}

TextEffect::TextEffect(Clip* c, const EffectMeta* em) :
  Effect(c, em)
{
  SetFlags(Effect::SuperimposeFlag);

  EffectRow* text_field = new EffectRow(this, tr("Text"));
  text_val = new StringField(text_field, "text", false);
  text_val->SetColumnSpan(2);
  
  EffectRow* position_row = new EffectRow(this, tr("Position"));
  position_x = new DoubleField(position_row, "posx");
  position_y = new DoubleField(position_row, "posy");
  
//======================================================
  EffectRow* reveal_row = new EffectRow(this, tr("Reveal %"));
  reveal_val = new DoubleField(reveal_row, "reveal");
  reveal_val->SetMinimum(0);
  reveal_val->SetMaximum(100);
  reveal_val->SetColumnSpan(2);
  
  EffectRow* animate_row = new EffectRow(this, tr("Animate"));
  animate_in_bool = new BoolField(animate_row, "animatein");
  animate_in_bool->SetColumnSpan(2);
  
//  EffectRow* stagger_row = new EffectRow(this, tr("Stagger(sec)*10"));
//  animate_stagger = new DoubleField(stagger_row, "stagger*10");                             
//  animate_stagger->SetMinimum(0);
//  animate_stagger->SetColumnSpan(2);
                                             
  EffectRow* duration_row = new EffectRow(this, tr("Animate duration (sec)"));
  animate_duration = new DoubleField(duration_row, "animateduration");
  animate_duration->SetMinimum(0.1);
  animate_duration->SetColumnSpan(2);
  
//  EffectRow* rise_row = new EffectRow(this, tr("Rise (px)"));
//  animate_rise = new DoubleField(rise_row, "animaterise");
//  animate_rise->SetColumnSpan(2);  
//======================================================

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

//  size_val->SetDefault(48);
  size_val->SetDefault(100);
  text_val->SetValueAt(0, tr("Sample"));
  set_color_button->SetValueAt(0, QColor(Qt::yellow));
  halign_field->SetValueAt(0, Qt::AlignLeft);
  valign_field->SetValueAt(0, Qt::AlignTop);
  word_wrap_field->SetValueAt(0, true);
  outline_color->SetValueAt(0, QColor(Qt::black));
  shadow_color->SetValueAt(0, QColor(Qt::black));
  shadow_angle->SetDefault(45);
  shadow_opacity->SetDefault(100);
  shadow_softness->SetDefault(5);
  shadow_distance->SetDefault(5);
  shadow_opacity->SetDefault(80);
  outline_width->SetDefault(20); 
  position_x->SetDefault(20);
  position_y->SetDefault(10);
//======================================================  
  reveal_val->SetDefault(100);    
//  animate_stagger->SetDefault(0.3);
  animate_duration->SetDefault(0.5);
//  animate_rise->SetDefault(20);  
                                  
  outline_enable(false);
  shadow_enable(false);

  connect(shadow_bool, &BoolField::Toggled, this, &TextEffect::shadow_enable);
  connect(outline_bool, &BoolField::Toggled, this, &TextEffect::outline_enable);

  vertPath = "common.vert";
  fragPath = "dropshadow.frag";
  
}

void TextEffect::redraw(double timecode) {
  if (size_val->GetDoubleAt(timecode) <= 0) {                                                      
    return;
  }

  QColor bkg = set_color_button->GetColorAt(timecode);
  bkg.setAlpha(0);
  img.fill(bkg);
                                                  
  QPainter p(&img);
  p.setRenderHint(QPainter::Antialiasing);
  int padding = qRound(padding_field->GetDoubleAt(timecode));
  int width = img.width() - padding * 2;
  int height = img.height() - padding * 2;

  // set font
  font.setStyleHint(QFont::Helvetica, QFont::PreferAntialias);
  font.setFamily(set_font_combobox->GetFontAt(timecode));
  font.setPointSize(qRound(size_val->GetDoubleAt(timecode)));
  p.setFont(font);
  QFontMetrics fm(font);

  QStringList lines = text_val->GetStringAt(timecode).split('\n');

  // word wrap function
  if (word_wrap_field->GetBoolAt(timecode)) {
    for (int i=0;i<lines.size();i++) {
      QString s(lines.at(i));
      if (fm.horizontalAdvance(s) > width) {
        int last_space_index = 0;
        for (int j=0;j<s.length();j++) {
          if (s.at(j) == ' ') {
            if (fm.horizontalAdvance(s.left(j)) > width) {
              break;
            } else {
              last_space_index = j;
            }
          }
        }
        if (last_space_index > 0) {
          lines.insert(i+1, s.mid(last_space_index + 1));
          lines[i] = s.left(last_space_index);
        }
      }
    }
  } 
// truncate revealed text, character-by-character, without disturbing the
// line breaks the word-wrap pass just computed 
//
// Count and truncate at grapheme-cluster boundaries, not raw UTF-16 code
  // units, so combining marks, surrogate pairs, and joined sequences never
  // get split mid-cluster before shaping even sees them.
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
      visible_lines.append(QString());   // keep the slot so valign/height stays stable
    } else if (line_grapheme_count <= remaining) {
      visible_lines.append(l);
      remaining -= line_grapheme_count;
    } else {
      visible_lines.append(l.left(b.at(remaining)));  // cut at a real grapheme boundary
      remaining = 0;
    }
  }
  lines = visible_lines;

  QPainterPath path;

  int line_h = qRound(fm.height() * line_height_field->GetDoubleAt(timecode));
  int text_height = (lines.size() - 1) * line_h + fm.height();

  for (int i=0;i<lines.size();i++) {
    int text_x, text_y;

    switch (halign_field->GetValueAt(timecode).toInt()) {
    case Qt::AlignLeft: text_x = 0; break;
    case Qt::AlignRight: text_x = width - fm.horizontalAdvance(lines.at(i)); break;
    case Qt::AlignJustify:
      // add spaces until the string is too big
      text_x = 0;
      while (fm.horizontalAdvance(lines.at(i)) < width) {
        bool space = false;
        QString spaced(lines.at(i));
        for (int i=0;i<spaced.length();i++) {
          if (spaced.at(i) == ' ') {
            // insert a space
            spaced.insert(i, ' ');
            space = true;

            // scan to next non-space
            while (i < spaced.length() && spaced.at(i) == ' ') i++;
          }
        }
        if (fm.horizontalAdvance(spaced) > width || !space) {
          break;
        } else {
          lines[i] = spaced;
        }
      }
      break;
    case Qt::AlignHCenter:
    default:
      text_x = (width/2) - (fm.horizontalAdvance(lines.at(i))/2);
      break;
    }

    switch (valign_field->GetValueAt(timecode).toInt()) {
    case Qt::AlignTop:
      text_y = (line_h*i)+fm.ascent();
      break;
    case Qt::AlignBottom:
      text_y = (height - text_height - fm.descent()) + (line_h*i) + fm.height();
      break;
    case Qt::AlignVCenter:
    default:
      text_y = ((height/2) - (text_height/2) - fm.descent()) + (line_h*i) + fm.height();
      break;
    }

    path.addText(text_x, text_y, font, lines.at(i));
  }

  path.translate(position_x->GetDoubleAt(timecode) + padding, position_y->GetDoubleAt(timecode) + padding);

  // draw software shadow
  if (shadow_bool->GetBoolAt(timecode)) {
    p.setPen(Qt::NoPen);

    // calculate offset using distance and angle
    double angle = shadow_angle->GetDoubleAt(timecode) * M_PI / 180.0;
    double distance = qFloor(shadow_distance->GetDoubleAt(timecode));
    int shadow_x_offset = qRound(qCos(angle) * distance);
    int shadow_y_offset = qRound(qSin(angle) * distance);

    QPainterPath shadow_path(path);
    shadow_path.translate(shadow_x_offset, shadow_y_offset);

    QColor col = shadow_color->GetColorAt(timecode);
    col.setAlpha(0);
    img.fill(col);

    col.setAlphaF(shadow_opacity->GetDoubleAt(timecode)*0.01);
    p.setBrush(col);
    p.drawPath(shadow_path);

    int blurSoftness = qFloor(shadow_softness->GetDoubleAt(timecode));
    if (blurSoftness > 0) amber::ui::blur(img, img.rect(), blurSoftness, true);
  }
//======================================================
//Draw the animated text
  bool animate = animate_in_bool->GetBoolAt(timecode);
  double stagger = 0.03;//animate_stagger->GetDoubleAt(timecode) * 0.1;
  double duration = qMax(0.01, animate_duration->GetDoubleAt(timecode));
  double rise = 20;//animate_rise->GetDoubleAt(timecode);
   
  p.setPen(Qt::NoPen);
  p.save(); 
  
// Outline  
  int outline_width_val = qCeil(outline_width->GetDoubleAt(timecode));
  bool do_outline = outline_bool->GetBoolAt(timecode) && outline_width_val > 0;
  QColor outline_base_color = outline_color->GetColorAt(timecode);
  QColor fill_color = set_color_button->GetColorAt(timecode);  
  
  p.translate(position_x->GetDoubleAt(timecode) + padding,
                position_y->GetDoubleAt(timecode) + padding);
  
  int global_char_index = 0;
  
  double current_frame = amber::ActiveSequence->playhead;
  double clip_start    = parent_clip->timeline_in();  
  double clip_end    = parent_clip->timeline_out();  
  if (current_frame < clip_start) return;

  double fps = amber::ActiveSequence->frame_rate;
  if (fps <= 0.0) fps = 29.97; 
  double frame_duration = 1.0 / fps;
  double curtime = (current_frame - clip_start) * frame_duration;
  double clipduration = (clip_end - clip_start)*frame_duration;  
  
  for (int i = 0; i < lines.size(); i++) {
    const QString& line = lines.at(i);
                                            
    // reuse the same text_x/text_y alignment calc used earlier for this line
    int text_x, text_y;
    switch (halign_field->GetValueAt(timecode).toInt()) {
      case Qt::AlignLeft: text_x = 0; break;
      case Qt::AlignRight: text_x = width - fm.horizontalAdvance(line); break;
      case Qt::AlignHCenter:
      default: text_x = (width / 2) - (fm.horizontalAdvance(line) / 2); break;
    }
    switch (valign_field->GetValueAt(timecode).toInt()) {
      case Qt::AlignTop: text_y = (fm.height() * i) + fm.ascent(); break;
      case Qt::AlignBottom: text_y = (height - text_height - fm.descent()) + (fm.height() * (i + 1)); break;
      case Qt::AlignVCenter:
      default: text_y = ((height / 2) - (text_height / 2) - fm.descent()) + (fm.height() * (i + 1)); break;
    }
    int accumulated_x = 0;
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
    // our already-computed baseline anchor (text_x, text_y) to that origin.
    double anchor_x = text_x;
    double anchor_y = text_y - text_line.ascent();

    QVector<int> clusters = grapheme_boundaries(line);

    for (int ci = 0; ci < clusters.size() - 1; ++ci) {
      int cluster_start = clusters.at(ci);
      int cluster_len = clusters.at(ci + 1) - cluster_start;

      double progress_in = 1.0;
      if (animate) {
        double t = (curtime - stagger * global_char_index) / duration;
        progress_in = qBound(0.0, t, 1.0);
        progress_in = progress_in * progress_in * (3.0 - 2.0 * progress_in);
      }

      double progress_out = 1.0;
      if (animate) {
        double time_remaining = clipduration - curtime;
        double t_out = (time_remaining - stagger * global_char_index) / duration;
        progress_out = qBound(0.0, t_out, 1.0);
        progress_out = progress_out * progress_out * (3.0 - 2.0 * progress_out);
      }
      double progress = qMin(progress_in, progress_out);

      double y_offset = (1.0 - progress) * rise;

      const QList<QGlyphRun> runs = text_line.glyphRuns(cluster_start, cluster_len);
      for (const QGlyphRun& run : runs) {
        QRawFont raw_font = run.rawFont();
        const QVector<quint32> glyph_indexes = run.glyphIndexes();
        const QVector<QPointF> positions = run.positions();

        for (int g = 0; g < glyph_indexes.size(); ++g) {
          QPainterPath glyph_path = raw_font.pathForGlyph(glyph_indexes.at(g));

          QTransform tf;
          tf.translate(anchor_x + positions.at(g).x(), anchor_y + positions.at(g).y() + y_offset);
          QPainterPath placed = tf.map(glyph_path);

          if (do_outline) {
            QColor oc = outline_base_color;
            oc.setAlphaF(oc.alphaF() * progress);
            QPen pen(oc);
            pen.setWidth(outline_width_val);
            p.setPen(pen);
            p.setBrush(Qt::NoBrush);
            p.drawPath(placed);
          }

          QColor c = fill_color;
          c.setAlphaF(c.alphaF() * progress);
          p.setPen(Qt::NoPen);
          p.setBrush(c);
          p.drawPath(placed);
        }
      }

      global_char_index++;
    }
  }  
  p.restore();
                                       
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
