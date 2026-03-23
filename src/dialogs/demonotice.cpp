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

#include <QCheckBox>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QDialogButtonBox>
#include <QPainter>
#include <QScreen>
#include <QSvgRenderer>

#include "global/config.h"

DemoNotice::DemoNotice(QWidget *parent) :
  QDialog(parent)
{
  setWindowTitle(tr("Welcome to Amber!"));
  setMinimumWidth(520);
  setSizeGripEnabled(true);

  QVBoxLayout* vlayout = new QVBoxLayout(this);

  QHBoxLayout* layout = new QHBoxLayout();
  layout->setContentsMargins(10, 10, 10, 10);
  layout->setSpacing(20);

  QLabel* icon = new QLabel(this);
  int logo_size = 128;
  qreal dpr = 1.0;
  if (QScreen* screen = QGuiApplication::primaryScreen()) {
    logo_size = qMin(screen->availableSize().width(), screen->availableSize().height()) / 6;
    if (logo_size < 128) logo_size = 128;
    dpr = screen->devicePixelRatio();
  }
  QSvgRenderer renderer(QStringLiteral(":/icons/amber-logo.svg"));
  QPixmap pix(logo_size * dpr, logo_size * dpr);
  pix.setDevicePixelRatio(dpr);
  pix.fill(Qt::transparent);
  QPainter painter(&pix);
  renderer.render(&painter, QRectF(0, 0, logo_size, logo_size));
  icon->setPixmap(pix);
  icon->setAlignment(Qt::AlignCenter);
  layout->addWidget(icon);

  QLabel* text = new QLabel("<html><head/><body>"
                            "<p><span style=\" font-size:14pt;\">"
                            + tr("Welcome to Amber!")
                            + "</span></p><p>"
                            + tr("Amber is a fork of Olive 0.1 ported to Qt 6 and modern FFmpeg. "
                                 "The original codebase was considered alpha-quality, but it has proven "
                                 "to be quite reliable in practice. Many bugs from the original code have "
                                 "been fixed along the way.")
                            + "</p><p>"
                            + tr("The original struck a rare balance between power and simplicity: "
                                 "a video editor where every feature you need is right where you expect it. "
                                 "I never found anything quite like it since, so I brought it back.")
                            + "</p><p>"
                            + tr("If you run into any issues, please report them on GitHub: %1")
                                .arg("<a href=\"https://github.com/baptisterajaut/amber\">"
                                     "<span style=\" text-decoration: underline; color:#007af4;\">"
                                     "github.com/baptisterajaut/amber</span></a>")
                            + "</p></body></html>", this);
  text->setWordWrap(true);
  layout->addWidget(text);

  vlayout->addLayout(layout);

  dont_show_again_ = new QCheckBox(tr("Don't show this message again"), this);
  vlayout->addWidget(dont_show_again_);

  QDialogButtonBox* buttons = new QDialogButtonBox(QDialogButtonBox::Ok, this);
  buttons->setCenterButtons(true);
  connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
  vlayout->addWidget(buttons);
}

void DemoNotice::accept() {
  if (dont_show_again_->isChecked()) {
    amber::CurrentConfig.show_welcome_dialog = false;
  }
  QDialog::accept();
}
