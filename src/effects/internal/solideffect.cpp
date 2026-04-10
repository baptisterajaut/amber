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

#include "solideffect.h"

#include <QComboBox>
#include <QImage>
#include <QPainter>
#include <QVariant>
#include <QtMath>

#include "engine/clip.h"
#include "engine/sequence.h"

const int SMPTE_BARS = 7;
const int SMPTE_STRIP_COUNT = 3;
const int SMPTE_LOWER_BARS = 4;

SolidEffect::SolidEffect(Clip* c, const EffectMeta* em) : Effect(c, em) {
  SetFlags(Effect::SuperimposeFlag);

  // Field for solid type
  EffectRow* type_row = new EffectRow(this, tr("Type"));
  solid_type = new ComboField(type_row, "type");
  solid_type->AddItem(tr("Solid Color"), SOLID_TYPE_COLOR);
  solid_type->AddItem(tr("SMPTE Bars"), SOLID_TYPE_BARS);
  solid_type->AddItem(tr("Checkerboard"), SOLID_TYPE_CHECKERBOARD);

  EffectRow* opacity_row = new EffectRow(this, tr("Opacity"));
  opacity_field = new DoubleField(opacity_row, "opacity");
  opacity_field->SetMinimum(0);
  opacity_field->SetDefault(100);
  opacity_field->SetMaximum(100);

  EffectRow* solid_color_row = new EffectRow(this, tr("Color"));
  solid_color_field = new ColorField(solid_color_row, "color");
  solid_color_field->SetValueAt(0, QColor(Qt::red));

  EffectRow* checkerboard_size = new EffectRow(this, tr("Checkerboard Size"));
  checkerboard_size_field = new DoubleField(checkerboard_size, "checker_size");
  checkerboard_size_field->SetMinimum(1);
  checkerboard_size_field->SetDefault(10);

  connect(solid_type, &ComboField::DataChanged, this, &SolidEffect::ui_update);

  // Set default UI
  solid_type->SetValueAt(0, SOLID_TYPE_COLOR);

  // TODO necessary? Isn't this called from the connect() above?
  ui_update(SOLID_TYPE_COLOR);

  /*vertPath = ":/shaders/common.vert";
  fragPath = ":/shaders/solideffect.frag";*/
}

void SolidEffect::DrawSmpteBars(int alpha) {
  int w = img.width();
  int h = img.height();
  QPainter p(&img);
  img.fill(Qt::transparent);

  int bar_width = qCeil(double(w) / 7.0);
  int first_bar_height = qCeil(double(h) / 3.0 * 2.0);
  int second_bar_height = qCeil(double(h) / 12.5);
  int third_bar_y = first_bar_height + second_bar_height;
  int third_bar_height = h - third_bar_y;
  int third_bar_width = 0;

  // Per-bar color table
  static const QColor kFirstColors[SMPTE_BARS] = {
      QColor(192, 192, 192), QColor(192, 192, 0), QColor(0, 192, 192), QColor(0, 192, 0),
      QColor(192, 0, 192),   QColor(192, 0, 0),   QColor(0, 0, 192),
  };
  static const QColor kSecondColors[SMPTE_BARS] = {
      QColor(0, 0, 192),   QColor(19, 19, 19), QColor(192, 0, 192),   QColor(19, 19, 19),
      QColor(0, 192, 192), QColor(19, 19, 19), QColor(192, 192, 192),
  };

  for (int i = 0; i < SMPTE_BARS; i++) {
    int bar_x = bar_width * i;

    if (i == 5) {
      // "PLUGE" area with three gradient strips
      third_bar_width = qRound(double(bar_x) / double(SMPTE_LOWER_BARS));
      int strip_width = qCeil(bar_width / SMPTE_STRIP_COUNT);
      p.fillRect(QRect(bar_x, third_bar_y, bar_width, third_bar_height), QColor(29, 29, 29, alpha));
      p.fillRect(QRect(bar_x + strip_width, third_bar_y, strip_width, third_bar_height), QColor(19, 19, 19, alpha));
      p.fillRect(QRect(bar_x, third_bar_y, strip_width, third_bar_height), QColor(9, 9, 9, alpha));
    } else if (i == 6) {
      p.fillRect(QRect(bar_x, third_bar_y, bar_width, third_bar_height), QColor(19, 19, 19, alpha));
    }

    QColor first_color = kFirstColors[i];
    QColor second_color = kSecondColors[i];
    first_color.setAlpha(alpha);
    second_color.setAlpha(alpha);
    p.fillRect(QRect(bar_x, 0, bar_width, first_bar_height), first_color);
    p.fillRect(QRect(bar_x, first_bar_height, bar_width, second_bar_height), second_color);
  }

  static const QColor kLowerColors[SMPTE_LOWER_BARS] = {QColor(0, 33, 76), QColor(255, 255, 255), QColor(50, 0, 106),
                                                        QColor(19, 19, 19)};
  for (int i = 0; i < SMPTE_LOWER_BARS; i++) {
    QColor c = kLowerColors[i];
    c.setAlpha(alpha);
    p.fillRect(QRect(third_bar_width * i, third_bar_y, third_bar_width, third_bar_height), c);
  }
}

void SolidEffect::DrawCheckerboard(double timecode, int alpha) {
  int w = img.width();
  int h = img.height();
  QPainter p(&img);
  img.fill(Qt::transparent);

  int checker_width = qCeil(checkerboard_size_field->GetDoubleAt(timecode));
  int checkerboard_size_w = qCeil(double(w) / checker_width);
  int checkerboard_size_h = qCeil(double(h) / checker_width);

  QColor checker_odd(0, 0, 0, alpha);
  QColor checker_even(solid_color_field->GetColorAt(timecode));
  checker_even.setAlpha(alpha);
  const QColor checker_color[2] = {checker_odd, checker_even};

  for (int i = 0; i < checkerboard_size_w; i++) {
    int checker_x = checker_width * i;
    for (int j = 0; j < checkerboard_size_h; j++) {
      int checker_y = checker_width * j;
      p.fillRect(QRect(checker_x, checker_y, checker_width, checker_width), checker_color[(i + j) % 2]);
    }
  }
}

void SolidEffect::redraw(double timecode) {
  int alpha = qRound(opacity_field->GetDoubleAt(timecode) * 2.55);

  switch (solid_type->GetValueAt(timecode).toInt()) {
    case SOLID_TYPE_COLOR: {
      QColor solidColor = solid_color_field->GetColorAt(timecode);
      solidColor.setAlpha(alpha);
      img.fill(solidColor);
    } break;
    case SOLID_TYPE_BARS:
      DrawSmpteBars(alpha);
      break;
    case SOLID_TYPE_CHECKERBOARD:
      DrawCheckerboard(timecode, alpha);
      break;
  }
}

void SolidEffect::SetType(SolidEffect::SolidType type) { solid_type->SetValueAt(0, type); }

void SolidEffect::ui_update(const QVariant& d) {
  int i = d.toInt();

  solid_color_field->SetEnabled(i == SOLID_TYPE_COLOR || i == SOLID_TYPE_CHECKERBOARD);
  checkerboard_size_field->SetEnabled(i == SOLID_TYPE_CHECKERBOARD);
}
