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

#include "viewerwindow.h"

#include <QMutex>
#include <QKeyEvent>
#include <QPainter>
#include <QApplication>
#include <QMenuBar>
#include <QShortcut>
#include <QOpenGLFunctions>
#include <QOpenGLContext>

#include <QDebug>

#include "mainwindow.h"
#include "rendering/matrixutil.h"
#include "rendering/quadbuffer.h"

ViewerWindow::ViewerWindow(QWidget *parent) :
  QOpenGLWidget(parent, Qt::Window)
  
{
  setMouseTracking(true);

  fullscreen_msg_timer.setInterval(2000);
  connect(&fullscreen_msg_timer, &QTimer::timeout, this, &ViewerWindow::fullscreen_msg_timeout);
}

void ViewerWindow::set_texture(GLuint t, double iar, QMutex* imutex) {
  texture = t;
  ar = iar;
  mutex = imutex;
  update();
}

void ViewerWindow::shortcut_copier(QVector<QShortcut*>& shortcuts, QMenu* menu) {
  QList<QAction*> menu_action = menu->actions();
  for (auto i : menu_action) {
    if (i->menu() != nullptr) {
      shortcut_copier(shortcuts, i->menu());
    } else if (!i->isSeparator() && !i->shortcut().isEmpty()) {
      QShortcut* sc = new QShortcut(this);
      sc->setKey(i->shortcut());
      connect(sc, &QShortcut::activated, i, &QAction::trigger);
      shortcuts.append(sc);
    }
  }
}

void ViewerWindow::showEvent(QShowEvent *)
{
  // Here, we copy all shortcuts from the MainWindow to this window. I don't like this solution, but messing around
  // with Qt's event system proved fruitless. Also setting the shortcuts to ApplicationShortcut rather than
  // WindowShortcut caused issues elsewhere (shortcuts being picked up in comboboxes and dialog boxes - we only
  // want the shortcuts to be shared to this window). Therefore, this and shortcut_copier() are so far the best
  // solutions I can find.

  // Clear any existing shortcuts in case they've changed since the last showing
  for (auto shortcut : shortcuts_) {
    delete shortcut;
  }
  shortcuts_.clear();

  // Recursively copy all shortcuts from MainWindow to this window
  QList<QAction*> menubar_actions = olive::MainWindow->menuBar()->actions();
  for (auto menubar_action : menubar_actions) {
    shortcut_copier(shortcuts_, menubar_action->menu());
  }
}

void ViewerWindow::keyPressEvent(QKeyEvent *e) {
  if (e->key() == Qt::Key_Escape) {
    hide();
  }
}

void ViewerWindow::mousePressEvent(QMouseEvent *e) {
  if (show_fullscreen_msg && fullscreen_msg_rect.contains(e->position().toPoint())) {
    hide();
  }
}

void ViewerWindow::mouseMoveEvent(QMouseEvent *) {
  fullscreen_msg_timer.start();
  if (!show_fullscreen_msg) {
    show_fullscreen_msg = true;
    update();
  }
}

void ViewerWindow::initializeGL() {
  passthrough_program_ = new QOpenGLShaderProgram(this);
  passthrough_program_->addShaderFromSourceFile(QOpenGLShader::Vertex, ":/internalshaders/passthrough.vert");
  passthrough_program_->addShaderFromSourceFile(QOpenGLShader::Fragment, ":/internalshaders/passthrough.frag");
  passthrough_program_->bindAttributeLocation("a_position", 0);
  passthrough_program_->bindAttributeLocation("a_texcoord", 1);
  passthrough_program_->link();

}

void ViewerWindow::paintGL() {
  if (texture > 0) {
    if (mutex != nullptr) mutex->lock();

    QOpenGLFunctions* f = QOpenGLContext::currentContext()->functions();

    glClearColor(0.0, 0.0, 0.0, 0.0);
    glClear(GL_COLOR_BUFFER_BIT);

    glBindTexture(GL_TEXTURE_2D, texture);

    double top = 0;
    double left = 0;
    double right = 1;
    double bottom = 1;

    double widget_ar = double(width()) / double(height());

    if (widget_ar > ar) {
      double w = 1.0 * ar / widget_ar;
      left = (1.0 - w)*0.5;
      right = left + w;
    } else {
      double h = 1.0 / ar * widget_ar;
      top = (1.0 - h)*0.5;
      bottom = top + h;
    }

    passthrough_program_->bind();
    passthrough_program_->setUniformValue("mvp_matrix", MatrixUtil::ortho(0, 1, 0, 1));
    passthrough_program_->setUniformValue("tex", 0);
    passthrough_program_->setUniformValue("color_mult", QVector4D(1, 1, 1, 1));

    float coords[8] = {
      float(left), float(top),
      float(left), float(bottom),
      float(right), float(bottom),
      float(right), float(top),
    };
    // FBO texture is Y-inverted: texcoord Y is flipped to compensate
    float texcoords[8] = {
      0, 1,
      0, 0,
      1, 0,
      1, 1,
    };
    QuadBuffer::draw(f, coords, texcoords);

    passthrough_program_->release();

    glBindTexture(GL_TEXTURE_2D, 0);

    if (mutex != nullptr) mutex->unlock();
  }

  if (show_fullscreen_msg) {
    QPainter p(this);

    QFont f = p.font();
    f.setPointSize(24);
    p.setFont(f);

    QFontMetrics fm(f);

    QString fs_str = tr("Exit Fullscreen");

    p.setPen(Qt::white);
    p.setBrush(QColor(0, 0, 0, 128));

    int text_width = fm.horizontalAdvance(fs_str);
    int text_x = (width()/2)-(text_width/2);
    int text_y = fm.height()+fm.ascent();

    int rect_padding = 8;

    fullscreen_msg_rect = QRect(text_x-rect_padding,
                                fm.height()-rect_padding,
                                text_width+rect_padding+rect_padding,
                                fm.height()+rect_padding+rect_padding);

    p.drawRect(fullscreen_msg_rect);

    p.drawText(text_x, text_y, fs_str);
  }

  // Force alpha to 1.0 so Wayland compositing doesn't show through transparent pixels
  glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_TRUE);
  glClearColor(0, 0, 0, 1);
  glClear(GL_COLOR_BUFFER_BIT);
  glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
}

void ViewerWindow::fullscreen_msg_timeout() {
  fullscreen_msg_timer.stop();
  if (show_fullscreen_msg) {
    show_fullscreen_msg = false;
    update();
  }
}
