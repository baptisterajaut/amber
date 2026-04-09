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
  setWindowTitle(tr("Amber 2.0 Preview"));
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
                            + tr("Amber 2.0 Preview — Early Development Build")
                            + "</span></p><p>"
                            + tr("This is a pre-alpha build of Amber 2.0. Features are incomplete, "
                                 "may change without notice, or may not work at all.")
                            + "</p><p>"
                            + tr("<b>For production work, use Amber 1.x (stable).</b> "
                                 "If you are here to test new 2.0 features, expect rough edges and "
                                 "do not rely on this build for anything important.")
                            + "</p><p>"
                            + tr("Bug reports and feedback are welcome on GitHub: %1")
                                .arg("<a href=\"https://github.com/baptisterajaut/amber\">"
                                     "<span style=\" text-decoration: underline; color:#007af4;\">"
                                     "github.com/baptisterajaut/amber</span></a>")
                            + "</p></body></html>", this);
  text->setWordWrap(true);
  layout->addWidget(text);

  vlayout->addLayout(layout);

  QDialogButtonBox* buttons = new QDialogButtonBox(QDialogButtonBox::Ok, this);
  buttons->setCenterButtons(true);
  connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
  vlayout->addWidget(buttons);
}

