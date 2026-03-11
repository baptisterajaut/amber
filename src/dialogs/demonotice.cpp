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

#include "demonotice.h"

#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QDialogButtonBox>
#include <QPainter>
#include <QScreen>
#include <QSvgRenderer>

DemoNotice::DemoNotice(QWidget *parent) :
  QDialog(parent)
{
  setWindowTitle(tr("Welcome to Amber!"));

  QVBoxLayout* vlayout = new QVBoxLayout(this);

  QHBoxLayout* layout = new QHBoxLayout();
  layout->setContentsMargins(10, 10, 10, 10);
  layout->setSpacing(20);

  QLabel* icon = new QLabel(this);
  int logo_size = qMin(QGuiApplication::primaryScreen()->availableSize().width(),
                        QGuiApplication::primaryScreen()->availableSize().height()) / 6;
  if (logo_size < 128) logo_size = 128;
  QSvgRenderer renderer(QStringLiteral(":/icons/amber-logo.svg"));
  QPixmap pix(logo_size, logo_size);
  pix.fill(Qt::transparent);
  QPainter painter(&pix);
  renderer.render(&painter);
  icon->setPixmap(pix);
  icon->setAlignment(Qt::AlignCenter);
  layout->addWidget(icon);

  QLabel* text = new QLabel("<html><head/><body><p>"
                            "<span style=\" font-size:14pt;\">"
                            + tr("Welcome to Amber!")
                            + "</span></p><p>"
                            + tr("Amber is a fork of Olive 0.1, ported to Qt 6 and FFmpeg 7/8. "
                                 "Original code by the Olive Team.")
                            + "</p><p>"
                            + tr("Amber is based on Olive 0.1, which was self-proclaimed alpha software. "
                                 "In my experience it works quite well, plus I squashed a lot of bugs already. "
                                 "Please report any issue on GitHub if you encounter one: %1").arg("<a href=\"https://github.com/baptisterajaut/amber\"><span style=\" text-decoration: underline; color:#007af4;\">github.com/baptisterajaut/amber</span></a>")
                            + "</p><p>"
                            + tr("Olive is free open-source software released under the GNU GPL.")
                            + "</p></body></html>", this);
  text->setWordWrap(true);
  layout->addWidget(text);

  vlayout->addLayout(layout);

  QDialogButtonBox* buttons = new QDialogButtonBox(QDialogButtonBox::Ok, this);
  buttons->setCenterButtons(true);
  connect(buttons, SIGNAL(accepted()), this, SLOT(accept()));
  vlayout->addWidget(buttons);
}
