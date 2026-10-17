#include "curveeditor.h"

#include <QApplication>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QToolButton>
#include <QtMath>

using namespace amber::curves;

namespace {

constexpr int kRowGap = 4;         // between the button row and the square's padding ring
constexpr int kButtonSpacing = 2;  // between the channel buttons

QColor channel_colour(int ch) {
  switch (ch) {
    case kRgb:
      return QColor(235, 235, 235);
    case kLuma:
      return QColor(165, 165, 165);
    case kRed:
      return QColor(255, 85, 85);
    case kGreen:
      return QColor(80, 215, 80);
    case kBlue:
      return QColor(95, 145, 255);
    default:
      return Qt::white;
  }
}

double distance_to_segment(QPointF p, QPointF a, QPointF b) {
  const QPointF ab = b - a;
  const double len2 = QPointF::dotProduct(ab, ab);
  double t = len2 > 0 ? QPointF::dotProduct(p - a, ab) / len2 : 0.0;
  t = qBound(0.0, t, 1.0);
  const QPointF d = p - (a + ab * t);
  return qSqrt(QPointF::dotProduct(d, d));
}

}  // namespace

CurveEditor::CurveEditor(QWidget* parent) : QWidget(parent), curves_(identity()) {
  setMouseTracking(true);
  setStatusTip(
      tr("Click the curve to add a point and drag points to shape it. Double-click a point, or drag it out of the "
         "square, to remove it."));
  QSizePolicy policy(QSizePolicy::Expanding, QSizePolicy::Preferred);
  policy.setHeightForWidth(true);
  setSizePolicy(policy);

  const QString names[kChannelCount] = {tr("RGB"), tr("Luma"), tr("R"), tr("G"), tr("B")};
  const QString tips[kChannelCount] = {tr("Master curve (red, green and blue together)"),
                                       tr("Luminance curve (keeps the colours' hue)"), tr("Red curve"),
                                       tr("Green curve"), tr("Blue curve")};
  for (int ch = 0; ch < kChannelCount; ch++) {
    QToolButton* b = new QToolButton(this);
    b->setText(names[ch]);
    b->setToolTip(tips[ch]);
    b->setCheckable(true);
    b->setAutoExclusive(true);
    b->setAutoRaise(true);
    b->setFocusPolicy(Qt::NoFocus);
    connect(b, &QToolButton::clicked, this, [this, ch]() { setChannel(ch); });
    channel_buttons_[ch] = b;
  }
  reset_button_ = new QToolButton(this);
  reset_button_->setText(tr("Reset Channel"));
  reset_button_->setToolTip(tr("Reset the selected channel's curve to a straight line"));
  reset_button_->setAutoRaise(true);
  reset_button_->setFocusPolicy(Qt::NoFocus);
  connect(reset_button_, &QToolButton::clicked, this, &CurveEditor::resetChannel);

  double_click_window_.setSingleShot(true);
  double_click_window_.setInterval(QApplication::doubleClickInterval());
  connect(&double_click_window_, &QTimer::timeout, this, &CurveEditor::finishSession);

  updateButtons();
}

void CurveEditor::setCurves(const QString& value) {
  if (session_.active) return;
  const CurveSet parsed = parse(value);
  if (parsed == curves_) return;
  curves_ = parsed;
  updateButtons();
  update();
}

QString CurveEditor::curves() const { return serialize(curves_); }

void CurveEditor::setChannel(int channel) {
  if (channel < 0 || channel >= kChannelCount) return;
  finishSession();  // a pending add (double-click window) belongs to the previous channel
  channel_ = channel;
  hover_index_ = -1;
  hover_curve_ = false;
  updateButtons();
  update();
}

int CurveEditor::topRowHeight() const {
  int h = 0;
  for (QToolButton* b : channel_buttons_) h = qMax(h, b->sizeHint().height());
  return qMax(h, reset_button_->sizeHint().height());
}

int CurveEditor::heightForWidth(int w) const {
  const int side = qBound(kMinSide, w - 2 * kPad, kMaxSide);
  return topRowHeight() + kRowGap + side + 2 * kPad;
}

QSize CurveEditor::sizeHint() const {
  const int w = kMaxSide + 2 * kPad;
  return QSize(w, heightForWidth(w));
}

int CurveEditor::buttonRowWidth() const {
  int w = reset_button_->sizeHint().width() + kRowGap * 2;
  for (QToolButton* b : channel_buttons_) w += b->sizeHint().width() + kButtonSpacing;
  return w;
}

QSize CurveEditor::minimumSizeHint() const {
  const int w = qMax(kMinSide, buttonRowWidth()) + 2 * kPad;
  return QSize(w, heightForWidth(w));
}

void CurveEditor::layoutButtons() {
  // Channel buttons from the square's left edge, Reset Channel ending at its right edge
  const int h = topRowHeight();
  int x = kPad;
  for (QToolButton* b : channel_buttons_) {
    const int w = b->sizeHint().width();
    b->setGeometry(x, 0, w, h);
    x += w + kButtonSpacing;
  }
  const int right = kPad + qBound(kMinSide, width() - 2 * kPad, kMaxSide);
  const int rw = reset_button_->sizeHint().width();
  reset_button_->setGeometry(qMax(x + kRowGap, right - rw), 0, rw, h);
}

void CurveEditor::resizeEvent(QResizeEvent* e) {
  QWidget::resizeEvent(e);
  // Width-driven height: the square follows the column width (the layouts above don't all honour heightForWidth)
  const int h = heightForWidth(width());
  if (minimumHeight() != h) setMinimumHeight(h);
  layoutButtons();
}

QRectF CurveEditor::square() const {
  const int top = topRowHeight() + kRowGap + kPad;
  const int side = qMax(1, qMin(qMin(width() - 2 * kPad, kMaxSide), height() - top - kPad));
  return QRectF(kPad, top, side, side);
}

QPointF CurveEditor::valueToWidget(QPointF v) const {
  const QRectF sq = square();
  return QPointF(sq.left() + v.x() / 255.0 * sq.width(), sq.bottom() - v.y() / 255.0 * sq.height());
}

QPointF CurveEditor::widgetToValue(QPointF pos) const {
  const QRectF sq = square();
  return QPointF((pos.x() - sq.left()) / sq.width() * 255.0, (sq.bottom() - pos.y()) / sq.height() * 255.0);
}

int CurveEditor::pointAt(QPointF pos) const {
  const Curve& c = curves_.channels[channel_];
  int best = -1;
  double best_d = kPointRadius;
  for (int i = 0; i < c.size(); i++) {
    const QPointF d = valueToWidget(c[i]) - pos;
    const double dist = qSqrt(QPointF::dotProduct(d, d));
    if (dist <= best_d) {
      best_d = dist;
      best = i;
    }
  }
  return best;
}

bool CurveEditor::onCurve(QPointF pos) const {
  const QRectF sq = square();
  if (pos.x() < sq.left() - 1 || pos.x() > sq.right() + 1) return false;
  const Curve& c = curves_.channels[channel_];
  // Distance to the curve drawn as one segment per pixel column around the cursor
  auto at = [&](double px) {
    const double v = qBound(0.0, (px - sq.left()) / sq.width() * 255.0, 255.0);
    return valueToWidget(QPointF(v, evaluate(c, v)));
  };
  for (double px = pos.x() - kCurveRadius - 1; px < pos.x() + kCurveRadius + 1; px += 1.0) {
    if (distance_to_segment(pos, at(px), at(px + 1.0)) <= kCurveRadius) return true;
  }
  return false;
}

bool CurveEditor::isInner(int index) const { return index > 0 && index < curves_.channels[channel_].size() - 1; }

void CurveEditor::updateButtons() {
  for (int ch = 0; ch < kChannelCount; ch++) {
    QToolButton* b = channel_buttons_[ch];
    b->setChecked(ch == channel_);
    // A modified channel reads bold, so a curve hidden behind another channel isn't forgotten
    QFont f = b->font();
    f.setBold(!is_identity(curves_.channels[ch]));
    b->setFont(f);
    QPalette pal = b->palette();
    if (ch >= kRed) pal.setColor(QPalette::ButtonText, channel_colour(ch));
    b->setPalette(pal);
  }
  reset_button_->setEnabled(!is_identity(curves_.channels[channel_]));
  layoutButtons();  // bold text is wider
}

void CurveEditor::updateHover(QPointF pos) {
  hovering_ = true;
  hover_pos_ = pos;
  hover_index_ = pointAt(pos);
  hover_curve_ = hover_index_ < 0 && onCurve(pos);
  if (hover_index_ >= 0) {
    setCursor(Qt::SizeAllCursor);
  } else if (hover_curve_) {
    setCursor(Qt::CrossCursor);
  } else {
    unsetCursor();
  }
  update();
}

void CurveEditor::beginSession(const QString& action) {
  finishSession();
  session_ = Session();
  session_.active = true;
  session_.start = serialize(curves_);
  session_.action = action;
  last_emitted_ = session_.start;
}

void CurveEditor::emitEdited() {
  const QString value = serialize(curves_);
  if (value == last_emitted_) return;
  last_emitted_ = value;
  session_.emitted = true;
  updateButtons();
  emit curvesEdited(value);
}

void CurveEditor::finishSession() {
  double_click_window_.stop();
  if (!session_.active) return;
  const Session done = session_;
  session_ = Session();
  updateButtons();
  update();
  if (done.emitted) emit editFinished(done.action, serialize(curves_) != done.start);
}

void CurveEditor::mousePressEvent(QMouseEvent* e) {
  if (e->button() != Qt::LeftButton) return;
  finishSession();
  const QPointF pos = e->position();
  int index = pointAt(pos);
  bool added = false;
  if (index < 0) {
    if (!onCurve(pos)) return;
    // On the curve: a new point there at the curve's current height, grabbed at once
    const int x = qBound(0, qRound(widgetToValue(pos).x()), 255);
    beginSession(tr("Add Curve Point"));
    Curve& c = curves_.channels[channel_];
    const int before = c.size();
    index = insert_point(c, x);
    added = c.size() != before;
    if (added) emitEdited();
  } else {
    beginSession(tr("Move Curve Point"));
  }
  session_.dragging = true;
  session_.index = index;
  session_.added = added;
  session_.moved = false;
  session_.grab_value = curves_.channels[channel_][index];
  session_.press_pos = pos;
  session_.curve = curves_.channels[channel_];
  if (!added) session_.action = tr("Move Curve Point");
  hover_index_ = index;
  update();
}

void CurveEditor::dragTo(QPointF pos) {
  Session& s = session_;
  const QPointF delta = pos - s.press_pos;
  if (!s.moved && qAbs(delta.x()) + qAbs(delta.y()) < kDragThreshold) return;
  s.moved = true;

  const QRectF sq = square();
  const QPoint wanted(qRound(s.grab_value.x() + delta.x() / sq.width() * 255.0),
                      qRound(s.grab_value.y() - delta.y() / sq.height() * 255.0));
  s.curve[s.index] = clamp_point(s.curve, s.index, wanted);

  const bool inner = s.index > 0 && s.index < s.curve.size() - 1;
  s.ghost = inner && (pos.y() < sq.top() - kDeleteDistance || pos.y() > sq.bottom() + kDeleteDistance);
  Curve shown = s.curve;
  if (s.ghost) shown.remove(s.index);
  curves_.channels[channel_] = shown;
  if (!s.added) s.action = s.ghost ? tr("Delete Curve Point") : tr("Move Curve Point");
  emitEdited();
}

void CurveEditor::mouseMoveEvent(QMouseEvent* e) {
  if (session_.dragging) {
    hover_pos_ = e->position();
    hovering_ = true;
    dragTo(e->position());
    update();
    return;
  }
  updateHover(e->position());
}

void CurveEditor::mouseReleaseEvent(QMouseEvent* e) {
  if (e->button() != Qt::LeftButton || !session_.dragging) return;
  dragTo(e->position());
  session_.dragging = false;
  if (session_.ghost) session_.action = tr("Delete Curve Point");
  // An add without movement waits out the double-click window: a double-click there removes the point again,
  // and the whole thing then is no change at all
  if (session_.added && !session_.moved) {
    double_click_window_.start();
  } else {
    finishSession();
  }
  updateHover(e->position());
}

void CurveEditor::mouseDoubleClickEvent(QMouseEvent* e) {
  if (e->button() != Qt::LeftButton) return;
  const int index = pointAt(e->position());
  if (!isInner(index)) {
    mousePressEvent(e);  // endpoints, the curve and empty space: a plain press
    return;
  }
  // The point the first click just added stays in that click's session (net: no change); any other point gets its own
  if (session_.active && !(session_.added && index == session_.index)) finishSession();
  if (!session_.active) beginSession(tr("Delete Curve Point"));
  double_click_window_.stop();
  session_.dragging = false;
  remove_point(curves_.channels[channel_], index);
  emitEdited();
  finishSession();
  updateHover(e->position());
}

void CurveEditor::leaveEvent(QEvent* e) {
  QWidget::leaveEvent(e);
  hovering_ = false;
  hover_index_ = -1;
  hover_curve_ = false;
  update();
}

void CurveEditor::resetChannel() {
  if (is_identity(curves_.channels[channel_])) return;
  beginSession(tr("Reset Curve Channel"));
  curves_.channels[channel_] = identity_curve();
  emitEdited();
  finishSession();
}

void CurveEditor::paintEvent(QPaintEvent*) {
  QPainter p(this);
  p.setRenderHint(QPainter::Antialiasing);
  const QRectF sq = square();
  const bool enabled = isEnabled();

  p.fillRect(sq, QColor(22, 22, 22));

  // Grid in quarters and the identity diagonal, both faint
  p.setPen(QPen(QColor(255, 255, 255, 26), 1));
  for (int i = 1; i < 4; i++) {
    const double x = sq.left() + sq.width() * i / 4.0;
    const double y = sq.top() + sq.height() * i / 4.0;
    p.drawLine(QPointF(x, sq.top()), QPointF(x, sq.bottom()));
    p.drawLine(QPointF(sq.left(), y), QPointF(sq.right(), y));
  }
  QPen diagonal(QColor(255, 255, 255, 50), 1, Qt::DashLine);
  p.setPen(diagonal);
  p.drawLine(sq.bottomLeft(), sq.topRight());

  auto curve_path = [&](const Curve& c) {
    QPainterPath path;
    const int steps = qMax(2, int(sq.width()));
    for (int i = 0; i <= steps; i++) {
      const double v = 255.0 * i / steps;
      const QPointF pt = valueToWidget(QPointF(v, evaluate(c, v)));
      if (i == 0) {
        path.moveTo(pt);
      } else {
        path.lineTo(pt);
      }
    }
    return path;
  };

  // The other channels' non-identity curves, faint, behind the selected one
  p.setBrush(Qt::NoBrush);
  for (int ch = 0; ch < kChannelCount; ch++) {
    if (ch == channel_ || is_identity(curves_.channels[ch])) continue;
    QColor col = channel_colour(ch);
    col.setAlpha(100);
    p.setPen(QPen(col, 1));
    p.drawPath(curve_path(curves_.channels[ch]));
  }

  QColor col = channel_colour(channel_);
  if (!enabled) col.setAlpha(110);
  const Curve& c = curves_.channels[channel_];
  p.setPen(QPen(col, 1.6));
  p.drawPath(curve_path(c));

  // Points: hollow, filled when hovered or grabbed
  const int active = session_.dragging ? (session_.ghost ? -1 : session_.index) : hover_index_;
  for (int i = 0; i < c.size(); i++) {
    const QPointF pt = valueToWidget(c[i]);
    const bool hot = enabled && i == active;
    p.setPen(QPen(col, 1.4));
    p.setBrush(hot ? QBrush(col) : QBrush(QColor(22, 22, 22)));
    p.drawEllipse(pt, hot ? 4.5 : 3.5, hot ? 4.5 : 3.5);
  }

  // Hovering the curve: where a click would add a point
  if (enabled && hovering_ && hover_curve_ && !session_.dragging) {
    const double x = qBound(0.0, widgetToValue(hover_pos_).x(), 255.0);
    QColor faded = col;
    faded.setAlpha(150);
    p.setPen(Qt::NoPen);
    p.setBrush(faded);
    p.drawEllipse(valueToWidget(QPointF(x, evaluate(c, x))), 2.5, 2.5);
  }

  // A point dragged out to be removed: a faded ghost at the square's edge, under the cursor
  QString readout;
  if (session_.dragging && session_.ghost) {
    const bool above = hover_pos_.y() < sq.top();
    const QPointF ghost(qBound(sq.left(), hover_pos_.x(), sq.right()),
                        above ? sq.top() - kPad / 2.0 : sq.bottom() + kPad / 2.0);
    QColor faded = col;
    faded.setAlpha(110);
    p.setPen(QPen(faded, 1.2, Qt::DashLine));
    p.setBrush(Qt::NoBrush);
    p.drawEllipse(ghost, 4.5, 4.5);
    readout = tr("Release to remove");
  } else if (session_.dragging) {
    const QPoint v = c.value(session_.index);
    readout = QStringLiteral("%1 → %2").arg(v.x()).arg(v.y());
  } else if (hovering_ && enabled && hover_index_ >= 0 && hover_index_ < c.size()) {
    readout = QStringLiteral("%1 → %2").arg(c[hover_index_].x()).arg(c[hover_index_].y());
  } else if (hovering_ && enabled && hover_curve_) {
    const int x = qBound(0, qRound(widgetToValue(hover_pos_).x()), 255);
    readout = QStringLiteral("%1 → %2").arg(x).arg(qRound(evaluate(c, x)));
  }

  if (!readout.isEmpty()) {
    const QFontMetrics fm = p.fontMetrics();
    QRectF box(0, 0, fm.horizontalAdvance(readout) + 8, fm.height() + 4);
    // Next to the cursor, flipped to the other side near the square's right or top edge, kept inside the widget
    QPointF at = hover_pos_ + QPointF(12, -box.height() - 6);
    if (at.x() + box.width() > sq.right()) at.setX(hover_pos_.x() - box.width() - 12);
    if (at.y() < sq.top()) at.setY(hover_pos_.y() + 14);
    at.setX(qBound(0.0, at.x(), width() - box.width()));
    at.setY(qBound(0.0, at.y(), height() - box.height()));
    box.moveTopLeft(at);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(0, 0, 0, 190));
    p.drawRoundedRect(box, 3, 3);
    p.setPen(QColor(230, 230, 230));
    p.drawText(box, Qt::AlignCenter, readout);
  }

  p.setPen(QPen(QColor(90, 90, 90), 1));
  p.setBrush(Qt::NoBrush);
  p.drawRect(sq);
}
