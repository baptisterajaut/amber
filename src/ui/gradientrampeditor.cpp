#include "gradientrampeditor.h"

#include <QColorDialog>
#include <QContextMenuEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPointer>

#include "ui/menu.h"

using namespace amber::gradientramp;

namespace {

constexpr int kChecker = 6;  // checkerboard square, px

bool is_delete_key(const QKeyEvent* k) {
  return k->modifiers() == Qt::NoModifier && (k->key() == Qt::Key_Delete || k->key() == Qt::Key_Backspace);
}

}  // namespace

GradientRampEditor::GradientRampEditor(QWidget* parent) : QWidget(parent) {
  setMouseTracking(true);
  setFocusPolicy(Qt::ClickFocus);  // Delete removes the selected stop
  setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
  setToolTip(
      tr("Click to add a color stop, drag a stop to move it, double-click to change its color, drag it off the bar "
         "to remove it"));
  picker_ = [](const QColor& initial, bool alpha, const QString& title) {
    // No parent: a dialog parented to the editor would be deleted under its own exec() if Effect Controls is
    // rebuilt meanwhile (as ColorButton does)
    return QColorDialog::getColor(initial, nullptr, title,
                                  alpha ? QColorDialog::ShowAlphaChannel : QColorDialog::ColorDialogOptions());
  };
}

GradientRampEditor::~GradientRampEditor() { finishSession(); }

void GradientRampEditor::setRamp(const QString& value) {
  if (session_.active) return;
  const Ramp parsed = parse(value);
  if (parsed == ramp_) return;
  ramp_ = parsed;
  selected_ = -1;  // an outside change (undo, another clip) may have moved the stops: a kept index would point elsewhere
  if (last_added_ >= ramp_.size()) last_added_ = -1;
  update();
}

QString GradientRampEditor::ramp() const { return serialize(ramp_); }

void GradientRampEditor::setEndColors(const QColor& start, const QColor& end) {
  if (start == start_ && end == end_) return;
  start_ = start;
  end_ = end;
  update();
}

QSize GradientRampEditor::sizeHint() const { return QSize(240, kTop + kBarHeight + kHandleHeight + 3); }

QSize GradientRampEditor::minimumSizeHint() const { return QSize(4 * kPad + 40, sizeHint().height()); }

QRectF GradientRampEditor::barRect() const { return QRectF(kPad, kTop, qMax(1, width() - 2 * kPad), kBarHeight); }

double GradientRampEditor::posToX(double pos) const {
  const QRectF bar = barRect();
  return bar.left() + pos * bar.width();
}

double GradientRampEditor::xToPos(double x) const {
  const QRectF bar = barRect();
  return (x - bar.left()) / bar.width();
}

QPointF GradientRampEditor::handleCenter(int index) const {
  double pos = 0.0;
  if (index == kEndHandle) {
    pos = 1.0;
  } else if (index >= 0 && index < ramp_.size()) {
    pos = ramp_[index].pos;
  }
  return QPointF(posToX(pos), barRect().bottom() + kHandleHeight * 0.65);
}

QPolygonF GradientRampEditor::handleShape(double x) const {
  const double top = barRect().bottom();
  return QPolygonF({QPointF(x, top), QPointF(x + kHandleHalfWidth, top + kHandleHeight),
                    QPointF(x - kHandleHalfWidth, top + kHandleHeight)});
}

int GradientRampEditor::handleAt(QPointF pos) const {
  const double top = barRect().bottom();
  if (pos.y() < top - 1 || pos.y() > top + kHandleHeight + 2) return -1;
  auto hit = [&](double p) { return qAbs(pos.x() - posToX(p)) <= kHandleHalfWidth + 1; };
  // The selected stop is drawn on top: it wins, then the later ones, then the fixed ends
  if (selected_ >= 0 && selected_ < ramp_.size() && hit(ramp_[selected_].pos)) return selected_;
  for (int i = ramp_.size() - 1; i >= 0; i--) {
    if (hit(ramp_[i].pos)) return i;
  }
  if (hit(0.0)) return kStartHandle;
  if (hit(1.0)) return kEndHandle;
  return -1;
}

bool GradientRampEditor::onBar(QPointF pos) const { return barRect().contains(pos); }

int GradientRampEditor::insertSorted(Ramp& r, const RampStop& s) {
  int i = 0;
  while (i < r.size() && r[i].pos <= s.pos) i++;
  r.insert(i, s);
  return i;
}

void GradientRampEditor::beginSession(const QString& action) {
  finishSession();
  session_ = Session();
  session_.active = true;
  session_.start = serialize(ramp_);
  session_.action = action;
  last_emitted_ = session_.start;
}

void GradientRampEditor::emitEdited() {
  const QString value = serialize(ramp_);
  if (value == last_emitted_) return;
  last_emitted_ = value;
  session_.emitted = true;
  emit rampEdited(value);
}

void GradientRampEditor::finishSession() {
  if (!session_.active) return;
  const Session done = session_;
  session_ = Session();
  update();
  if (done.emitted) emit editFinished(done.action, serialize(ramp_) != done.start);
}

void GradientRampEditor::grabStop(int index, QPointF pos, bool added) {
  if (!session_.active) beginSession(tr("Move Color Stop"));
  Session& s = session_;
  s.dragging = true;
  s.added = added;
  s.moved = false;
  s.ghost = false;
  s.grab_pos = ramp_[index].pos;
  s.press_pos = pos;
  s.stop = ramp_[index];
  s.others = ramp_;
  s.others.remove(index);
}

void GradientRampEditor::dragTo(QPointF pos) {
  Session& s = session_;
  const QPointF delta = pos - s.press_pos;
  if (!s.moved && qAbs(delta.x()) + qAbs(delta.y()) < kDragThreshold) return;
  s.moved = true;

  const QRectF bar = barRect();
  s.stop.pos = clamp_pos(s.grab_pos + delta.x() / bar.width());
  s.ghost = pos.y() > bar.bottom() + kHandleHeight + kRemoveDistance || pos.y() < bar.top() - kRemoveDistance;
  // Passing another stop re-sorts at once: the string, the picture and the handles stay in order
  ramp_ = s.others;
  selected_ = s.ghost ? -1 : insertSorted(ramp_, s.stop);
  if (!s.added) s.action = s.ghost ? tr("Remove Color Stop") : tr("Move Color Stop");
  setCursor(s.ghost ? Qt::ForbiddenCursor : Qt::SizeHorCursor);
  emitEdited();
  update();
}

void GradientRampEditor::updateHover(QPointF pos) {
  hovering_ = true;
  hover_pos_ = pos;
  const int h = handleAt(pos);
  if (h >= 0) {
    setCursor(Qt::SizeHorCursor);
  } else if ((h == kStartHandle || h == kEndHandle) && ends_editable_) {
    setCursor(Qt::PointingHandCursor);
  } else if (onBar(pos) && ramp_.size() < kMaxStops) {
    setCursor(Qt::CrossCursor);
  } else {
    unsetCursor();
  }
  update();
}

void GradientRampEditor::mousePressEvent(QMouseEvent* e) {
  if (e->button() != Qt::LeftButton) return;
  finishSession();
  last_added_ = -1;
  const QPointF pos = e->position();
  const int h = handleAt(pos);
  if (h >= 0) {
    selected_ = h;
    grabStop(h, pos, false);
  } else if (h == kStartHandle || h == kEndHandle) {
    selected_ = -1;
  } else if (onBar(pos)) {
    if (ramp_.size() >= kMaxStops) return;
    // A stop with the colour the ramp already has there: adding it doesn't change the picture
    beginSession(tr("Add Color Stop"));
    const double p = clamp_pos(xToPos(pos.x()));
    const int index = insertSorted(ramp_, {p, color_at(stops(start_, end_, ramp_), p)});
    selected_ = index;
    emitEdited();
    grabStop(index, pos, true);
  } else {
    selected_ = -1;
  }
  update();
}

void GradientRampEditor::mouseMoveEvent(QMouseEvent* e) {
  if (session_.dragging) {
    dragTo(e->position());
    return;
  }
  updateHover(e->position());
}

void GradientRampEditor::mouseReleaseEvent(QMouseEvent* e) {
  if (e->button() != Qt::LeftButton || !session_.dragging) return;
  dragTo(e->position());
  session_.dragging = false;
  if (session_.added && !session_.moved) last_added_ = selected_;
  finishSession();
  updateHover(e->position());
}

void GradientRampEditor::mouseDoubleClickEvent(QMouseEvent* e) {
  if (e->button() != Qt::LeftButton) return;
  const QPointF pos = e->position();
  const int h = handleAt(pos);
  if (h >= 0) {
    finishSession();
    selected_ = h;
    pickStopColor(h);
  } else if (h == kStartHandle || h == kEndHandle) {
    finishSession();
    if (ends_editable_) pickEndColor(h == kStartHandle ? 0 : 1);
  } else if (onBar(pos) && last_added_ >= 0 && last_added_ < ramp_.size()) {
    // The first click of this double-click added a stop: pick its colour
    finishSession();
    const int index = last_added_;
    last_added_ = -1;
    pickStopColor(index);
  } else {
    mousePressEvent(e);
  }
}

void GradientRampEditor::pickStopColor(int index) {
  if (index < 0 || index >= ramp_.size()) return;
  const QColor old = ramp_[index].color;
  QPointer<GradientRampEditor> guard(this);
  const QColor c = picker_(old, true, tr("Stop Color"));
  if (guard.isNull() || !c.isValid() || c == old || index >= ramp_.size() || session_.active) return;
  beginSession(tr("Change Color Stop"));
  ramp_[index].color = c;
  emitEdited();
  finishSession();
}

void GradientRampEditor::pickEndColor(int which) {
  const QColor old = which == 0 ? start_ : end_;
  QPointer<GradientRampEditor> guard(this);
  // Opaque: Start Color and End Color save as #rrggbb
  const QColor c = picker_(old, false, which == 0 ? tr("Start Color") : tr("End Color"));
  if (guard.isNull() || !c.isValid() || c == old) return;
  (which == 0 ? start_ : end_) = c;
  update();
  emit endColorEdited(which, c);
}

int GradientRampEditor::addStopAt(double pos) {
  if (ramp_.size() >= kMaxStops) return -1;
  beginSession(tr("Add Color Stop"));
  const double p = clamp_pos(pos);
  const int index = insertSorted(ramp_, {p, color_at(stops(start_, end_, ramp_), p)});
  selected_ = index;
  emitEdited();
  finishSession();
  return index;
}

void GradientRampEditor::removeStop(int index) {
  if (index < 0 || index >= ramp_.size()) return;
  beginSession(tr("Remove Color Stop"));
  ramp_.remove(index);
  selected_ = -1;
  last_added_ = -1;
  emitEdited();
  finishSession();
}

void GradientRampEditor::reverseRamp() {
  finishSession();
  if (ramp_.isEmpty() && (!ends_editable_ || start_ == end_)) return;
  ramp_ = reversed(ramp_);
  if (ends_editable_) std::swap(start_, end_);
  if (selected_ >= 0) selected_ = ramp_.size() - 1 - selected_;
  last_added_ = -1;
  update();
  emit rampReversed(serialize(ramp_));
}

bool GradientRampEditor::event(QEvent* e) {
  // Delete and Backspace are application shortcuts too (delete the selected clips): with a stop selected, they're ours
  if (e->type() == QEvent::ShortcutOverride && selected_ >= 0 && is_delete_key(static_cast<QKeyEvent*>(e))) {
    e->accept();
    return true;
  }
  return QWidget::event(e);
}

void GradientRampEditor::keyPressEvent(QKeyEvent* e) {
  if (is_delete_key(e) && selected_ >= 0 && !session_.dragging) {
    removeStop(selected_);
    e->accept();
    return;
  }
  QWidget::keyPressEvent(e);
}

void GradientRampEditor::contextMenuEvent(QContextMenuEvent* e) {
  finishSession();
  const QPointF pos = e->pos();
  const int h = handleAt(pos);
  Menu menu(this);
  QAction* change = nullptr;
  QAction* remove = nullptr;
  QAction* add = nullptr;
  if (h >= 0) {
    selected_ = h;
    update();
    change = menu.addAction(tr("Change Color..."));
    remove = menu.addAction(tr("Remove Stop"));
  } else {
    if ((h == kStartHandle || h == kEndHandle) && ends_editable_) change = menu.addAction(tr("Change Color..."));
    add = menu.addAction(tr("Add Stop Here"));
    add->setEnabled(onBar(pos) && ramp_.size() < kMaxStops);
  }
  menu.addSeparator();
  QAction* reverse = menu.addAction(tr("Reverse Ramp"));
  reverse->setEnabled(!ramp_.isEmpty() || (ends_editable_ && start_ != end_));

  QPointer<GradientRampEditor> guard(this);
  QAction* chosen = menu.exec(e->globalPos());
  if (guard.isNull() || chosen == nullptr) return;
  if (chosen == change) {
    if (h >= 0) {
      pickStopColor(h);
    } else {
      pickEndColor(h == kStartHandle ? 0 : 1);
    }
  } else if (chosen == remove) {
    removeStop(h);
  } else if (chosen == add) {
    addStopAt(xToPos(pos.x()));
  } else if (chosen == reverse) {
    reverseRamp();
  }
}

void GradientRampEditor::leaveEvent(QEvent* e) {
  QWidget::leaveEvent(e);
  hovering_ = false;
  update();
}

void GradientRampEditor::hideEvent(QHideEvent* e) {
  finishSession();  // collapsed effect, Effect Controls rebuilt for another clip, ...
  QWidget::hideEvent(e);
}

void GradientRampEditor::paintEvent(QPaintEvent*) {
  QPainter p(this);
  const QRectF bar = barRect();
  const bool enabled = isEnabled();
  if (!enabled) p.setOpacity(0.45);

  // Checkerboard under the ramp, so alpha shows
  p.save();
  p.setClipRect(bar);
  for (int y = 0; y * kChecker < bar.height(); y++) {
    for (int x = 0; x * kChecker < bar.width(); x++) {
      p.fillRect(QRectF(bar.left() + x * kChecker, bar.top() + y * kChecker, kChecker, kChecker),
                 (x + y) % 2 ? QColor(153, 153, 153) : QColor(204, 204, 204));
    }
  }
  QLinearGradient g(bar.left(), 0, bar.right(), 0);
  g.setStops(stops(start_, end_, ramp_));
  p.fillRect(bar, g);
  p.restore();

  p.setRenderHint(QPainter::Antialiasing);
  QColor line = palette().color(QPalette::WindowText);
  line.setAlpha(110);
  p.setPen(QPen(line, 1));
  p.setBrush(Qt::NoBrush);
  p.drawRect(bar.adjusted(-0.5, -0.5, 0.5, 0.5));

  const QColor text = palette().color(QPalette::WindowText);
  const QColor accent = palette().color(QPalette::Highlight);
  auto draw_handle = [&](double pos, QColor fill, bool selected, bool hot, bool ghost) {
    fill.setAlpha(255);  // the bar shows the alpha, the handle the hue
    QColor outline = text;
    outline.setAlpha(hot ? 255 : 170);
    const QPolygonF shape = handleShape(posToX(pos));
    p.save();
    if (ghost) p.setOpacity(p.opacity() * 0.35);
    if (selected) {
      // An accent ring around a ring of the background, so it reads on a stop of the accent's own hue too
      p.setPen(QPen(accent, 4.5, Qt::SolidLine, Qt::SquareCap, Qt::MiterJoin));
      p.setBrush(Qt::NoBrush);
      p.drawPolygon(shape);
      outline = palette().color(QPalette::Window);
    }
    QPen pen(outline, selected ? 1.2 : (hot ? 1.4 : 1.0));
    if (ghost) pen.setStyle(Qt::DashLine);
    pen.setJoinStyle(Qt::MiterJoin);
    p.setPen(pen);
    p.setBrush(fill);
    p.drawPolygon(shape);
    p.restore();
  };

  const int hover = enabled && hovering_ && !session_.dragging ? handleAt(hover_pos_) : -1;
  draw_handle(0.0, start_, false, hover == kStartHandle && ends_editable_, false);
  draw_handle(1.0, end_, false, hover == kEndHandle && ends_editable_, false);
  for (int i = 0; i < ramp_.size(); i++) {
    if (i != selected_) draw_handle(ramp_[i].pos, ramp_[i].color, false, i == hover, false);
  }
  if (selected_ >= 0 && selected_ < ramp_.size()) {
    draw_handle(ramp_[selected_].pos, ramp_[selected_].color, enabled, false, false);
  }
  // A stop dragged off the bar: faded where it would be, removed on release
  if (session_.dragging && session_.ghost) draw_handle(session_.stop.pos, session_.stop.color, false, false, true);
}
