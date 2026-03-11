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

#include "aboutdialog.h"

#include <QGuiApplication>
#include <QLabel>
#include <QScreen>
#include <QPainter>
#include <QSvgRenderer>
#include <QVBoxLayout>
#include <QDialogButtonBox>

#include "global/global.h"

AboutDialog::AboutDialog(QWidget *parent) :
  QDialog(parent)
{
  setWindowTitle("About Amber");

  QVBoxLayout* layout = new QVBoxLayout(this);
  layout->setSpacing(20);

  // Logo (rendered from SVG at screen-appropriate size)
  QLabel* logo = new QLabel(this);
  int logo_size = qMin(QGuiApplication::primaryScreen()->availableSize().width(),
                        QGuiApplication::primaryScreen()->availableSize().height()) / 6;
  if (logo_size < 128) logo_size = 128;
  QSvgRenderer renderer(QStringLiteral(":/icons/amber-logo.svg"));
  QPixmap pix(logo_size, logo_size);
  pix.fill(Qt::transparent);
  QPainter painter(&pix);
  renderer.render(&painter);
  logo->setPixmap(pix);
  logo->setAlignment(Qt::AlignCenter);
  layout->addWidget(logo);

  // Construct About text
  QLabel* label =
      new QLabel(QString("<html><head/><body>"
                         "<p><b>%1</b></p>"
                         "<p>%2</p>"
                         "<p>%3</p>"
                         "<p>%4</p>"
                         "</body></html>").arg(olive::AppName,
                                               tr("Fork of Olive 0.1 — ported to Qt 6 and FFmpeg 7/8. "
                                                  "Original code by the Olive Team."),
                                               tr("Amber is a free non-linear video editor protected by the GNU GPL."),
                                               "<a href=\"https://github.com/baptisterajaut/amber\">"
                                               "<span style=\" text-decoration: underline; color:#007af4;\">"
                                               "github.com/baptisterajaut/amber</span></a>"
), this);

  // Set text formatting
  label->setAlignment(Qt::AlignCenter);
  label->setTextInteractionFlags(Qt::TextSelectableByMouse);
  label->setCursor(Qt::IBeamCursor);
  label->setWordWrap(true);
  layout->addWidget(label);

  QDialogButtonBox* buttons = new QDialogButtonBox(QDialogButtonBox::Ok, this);
  buttons->setCenterButtons(true);
  layout->addWidget(buttons);

  connect(buttons, SIGNAL(accepted()), this, SLOT(accept()));
}
