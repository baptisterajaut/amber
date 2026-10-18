#include "trackheaderwidget.h"

#include <QHelpEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QScrollBar>
#include <QToolTip>
#include <QWheelEvent>

#include <climits>

#include "engine/sequence.h"
#include "engine/undo/undo_timeline.h"
#include "engine/undo/undostack.h"
#include "panels/panels.h"
#include "panels/timeline.h"
#include "ui/icons.h"
#include "ui/timelinewidget.h"

namespace {

constexpr int kButtonSize = 18;
constexpr int kButtonSpacing = 2;
constexpr int kRightMargin = 4;
constexpr int kLabelPadding = 6;
constexpr int kResizeRange = 3;
// Below this row height only the label is drawn
constexpr int kMinButtonRowHeight = 10;

const QColor kMuteAccent(224, 160, 48);
const QColor kSoloAccent(64, 176, 80);
const QColor kLockAccent(96, 118, 148);
const QColor kHiddenAccent(72, 72, 72);

QFont label_font(const QFont& base) {
  QFont f = base;
  f.setBold(true);
  return f;
}

}  // namespace

TrackHeaderWidget::TrackHeaderWidget(TimelineWidget* timeline, QScrollBar* vertical_scrollbar, QWidget* parent)
    : QWidget(parent), timeline_(timeline), vertical_scrollbar_(vertical_scrollbar), resize_track_(INT_MIN) {
  eye_icon_ = amber::icon::CreateIconFromSVG(QStringLiteral(":/icons/eye.svg"));
  eye_off_icon_ = amber::icon::CreateIconFromSVG(QStringLiteral(":/icons/eye-off.svg"));
  lock_icon_ = amber::icon::CreateIconFromSVG(QStringLiteral(":/icons/lock.svg"));
  unlock_icon_ = amber::icon::CreateIconFromSVG(QStringLiteral(":/icons/unlock.svg"));

  setMouseTracking(true);

  // label ("V88" in bold) + three buttons, scaled for the font
  const QFontMetrics fm(label_font(font()));
  setFixedWidth(qMax(96, kLabelPadding + fm.horizontalAdvance(QStringLiteral("V88")) + kLabelPadding + 3 * kButtonSize +
                             2 * kButtonSpacing + kRightMargin));

  connect(vertical_scrollbar_, &QScrollBar::valueChanged, this, qOverload<>(&QWidget::update));
}

void TrackHeaderWidget::track_range(int* first, int* last) {
  int video_limit = 0;
  int audio_limit = 0;
  amber::ActiveSequence->getTrackLimits(&video_limit, &audio_limit);
  *first = video_limit - 1;
  *last = audio_limit + 1;
}

QRect TrackHeaderWidget::row_rect(int track) {
  QRect r = timeline_->track_rect(track);
  r.setLeft(0);
  r.setWidth(width());
  return r;
}

QVector<TrackHeaderWidget::Button> TrackHeaderWidget::buttons_for_row(int track, const QRect& row) {
  QVector<Button> buttons;
  if (row.height() < kMinButtonRowHeight) return buttons;

  // shrink on rows shorter than the buttons (the 15 px minimum track height)
  const int size = qMin(kButtonSize, row.height() - 2);
  const int y = row.top() + (row.height() - size) / 2;
  int x = row.right() - kRightMargin - size + 1;

  // right to left: the lock is always the rightmost
  QVector<Toggle> toggles;
  if (track < 0) {
    toggles = {kToggleLock, kToggleEye};
  } else {
    toggles = {kToggleLock, kToggleSolo, kToggleMute};
  }
  for (Toggle t : toggles) {
    buttons.append({t, QRect(x, y, size, size)});
    x -= size + kButtonSpacing;
  }
  return buttons;
}

bool TrackHeaderWidget::hit_button(const QPoint& pos, int* track, Toggle* toggle) {
  if (amber::ActiveSequence == nullptr) return false;
  int first, last;
  track_range(&first, &last);
  for (int t = first; t <= last; t++) {
    const QRect row = row_rect(t);
    if (!row.contains(pos)) continue;
    for (const Button& b : buttons_for_row(t, row)) {
      if (b.rect.contains(pos)) {
        *track = t;
        *toggle = b.toggle;
        return true;
      }
    }
    return false;
  }
  return false;
}

int TrackHeaderWidget::resize_edge_track(int y) {
  if (amber::ActiveSequence == nullptr) return INT_MIN;
  int first, last;
  track_range(&first, &last);
  for (int t = first; t <= last; t++) {
    const QRect row = row_rect(t);
    const int edge = (t < 0) ? row.top() : row.top() + row.height();
    if (qAbs(y - edge) <= kResizeRange) return t;
  }
  return INT_MIN;
}

void TrackHeaderWidget::toggle(int track, Toggle toggle, bool all_of_kind) {
  Sequence* seq = amber::ActiveSequence.get();
  if (seq == nullptr || toggle == kToggleNone) return;

  auto flag = [toggle](TrackState& s) -> bool& {
    switch (toggle) {
      case kToggleEye:
        return s.hidden;
      case kToggleMute:
        return s.muted;
      case kToggleSolo:
        return s.solo;
      default:
        return s.locked;
    }
  };

  TrackState clicked = seq->track_state(track);
  const bool value = !flag(clicked);

  // Shift+click (Premiere's shortcut): the new value goes to every shown track of the same kind
  QVector<int> targets;
  if (all_of_kind) {
    int first, last;
    track_range(&first, &last);
    for (int t = first; t <= last; t++) {
      if ((t < 0) == (track < 0)) targets.append(t);
    }
  } else {
    targets.append(track);
  }

  const QMap<int, TrackState> old_states = seq->track_states;
  QMap<int, TrackState> new_states = old_states;
  for (int t : targets) {
    TrackState s = new_states.value(t);
    flag(s) = value;
    if (s.is_default()) {
      new_states.remove(t);
    } else {
      new_states.insert(t, s);
    }
  }
  if (new_states == old_states) return;

  auto* cmd = new SetTrackStateCommand(seq, old_states, new_states);
  switch (toggle) {
    case kToggleEye:
      cmd->setText(tr("Toggle Track Visibility"));
      break;
    case kToggleMute:
      cmd->setText(tr("Toggle Track Mute"));
      break;
    case kToggleSolo:
      cmd->setText(tr("Toggle Track Solo"));
      break;
    default:
      cmd->setText(tr("Toggle Track Lock"));
      break;
  }
  amber::UndoStack.push(cmd);

  // a lock drops selections: rebuild Effect Controls from what is left
  update_ui(toggle == kToggleLock);
}

void TrackHeaderWidget::draw_button(QPainter& p, const Button& b, bool on) {
  const QRectF r = QRectF(b.rect).adjusted(0.5, 0.5, -0.5, -0.5);
  QColor text_color = palette().color(QPalette::WindowText);

  QColor accent;
  switch (b.toggle) {
    case kToggleEye:
      accent = kHiddenAccent;
      break;
    case kToggleMute:
      accent = kMuteAccent;
      break;
    case kToggleSolo:
      accent = kSoloAccent;
      break;
    default:
      accent = kLockAccent;
      break;
  }

  p.setRenderHint(QPainter::Antialiasing, true);
  if (on) {
    p.setPen(accent.darker(130));
    p.setBrush(accent);
  } else {
    QColor outline = text_color;
    outline.setAlpha(90);
    p.setPen(outline);
    p.setBrush(Qt::NoBrush);
  }
  p.drawRoundedRect(r, 3, 3);
  p.setRenderHint(QPainter::Antialiasing, false);

  const QRect inner = b.rect.adjusted(3, 3, -3, -3);
  switch (b.toggle) {
    case kToggleEye:
      // hidden: the crossed-out eye, dimmed
      (on ? eye_off_icon_ : eye_icon_).paint(&p, inner, Qt::AlignCenter, on ? QIcon::Disabled : QIcon::Normal);
      break;
    case kToggleLock:
      (on ? lock_icon_ : unlock_icon_).paint(&p, inner, Qt::AlignCenter, on ? QIcon::Normal : QIcon::Disabled);
      break;
    default: {
      QFont f = label_font(font());
      f.setPixelSize(qMax(6, b.rect.height() * 2 / 3));
      p.setFont(f);
      if (on) {
        p.setPen(Qt::black);
      } else {
        QColor c = text_color;
        c.setAlpha(160);
        p.setPen(c);
      }
      p.drawText(b.rect, Qt::AlignCenter, b.toggle == kToggleMute ? QStringLiteral("M") : QStringLiteral("S"));
      break;
    }
  }
}

void TrackHeaderWidget::paintEvent(QPaintEvent*) {
  QPainter p(this);
  if (amber::ActiveSequence == nullptr) return;
  Sequence* seq = amber::ActiveSequence.get();

  // same V/A zone tints and seam as the clip area
  const int seam = timeline_->seam_screen_y();
  p.fillRect(0, 0, width(), qBound(0, seam, height()), QColor(255, 255, 255, 6));
  p.fillRect(0, qBound(0, seam, height()), width(), height(), QColor(0, 0, 0, 10));

  const QFont base_font = font();
  const QFont bold = label_font(base_font);
  const QColor text_color = palette().color(QPalette::WindowText);

  int first, last;
  track_range(&first, &last);
  for (int t = first; t <= last; t++) {
    const QRect row = row_rect(t);
    if (row.bottom() < 0 || row.top() >= height()) continue;

    // header cell body, a little lighter than the empty clip area
    p.fillRect(row, QColor(255, 255, 255, 10));

    // 1 px separator on the edge away from the seam, where the clip area draws its track lines
    p.setPen(QColor(0, 0, 0, 96));
    const int line_y = (t < 0) ? row.top() - 1 : row.top() + row.height();
    p.drawLine(0, line_y, width(), line_y);

    const TrackState state = seq->track_state(t);
    const bool dimmed = (t < 0) ? !seq->track_visible_in_render(t) : !seq->track_audible(t);

    const QVector<Button> buttons = buttons_for_row(t, row);
    QRect label_rect = row.adjusted(kLabelPadding, 0, 0, 0);
    if (!buttons.isEmpty()) label_rect.setRight(buttons.last().rect.left() - kButtonSpacing);
    QColor label_color = text_color;
    if (dimmed) label_color.setAlpha(110);
    p.setFont(bold);
    p.setPen(label_color);
    p.drawText(label_rect, Qt::AlignLeft | Qt::AlignVCenter, amber::track_label(t));

    for (const Button& b : buttons) {
      bool on = false;
      switch (b.toggle) {
        case kToggleEye:
          on = state.hidden;
          break;
        case kToggleMute:
          on = state.muted;
          break;
        case kToggleSolo:
          on = state.solo;
          break;
        default:
          on = state.locked;
          break;
      }
      draw_button(p, b, on);
    }
    p.setFont(base_font);
  }

  // seam accent, as the clip area
  if (seam >= 0 && seam < height()) {
    p.setPen(QColor(0, 0, 0, 200));
    p.drawLine(0, seam, width(), seam);
    p.drawLine(0, seam - 1, width(), seam - 1);
    p.setPen(QColor(255, 255, 255, 40));
    p.drawLine(0, seam + 1, width(), seam + 1);
  }

  // right border against the clip area
  p.setPen(QColor(0, 0, 0, 128));
  p.drawLine(width() - 1, 0, width() - 1, height());
}

void TrackHeaderWidget::mousePressEvent(QMouseEvent* event) {
  if (amber::ActiveSequence == nullptr || event->button() != Qt::LeftButton) return;
  const QPoint pos = event->position().toPoint();

  const int edge_track = resize_edge_track(pos.y());
  if (edge_track != INT_MIN) {
    resizing_ = true;
    resize_track_ = edge_track;
    resize_last_y_ = pos.y();
    return;
  }

  int track;
  Toggle t;
  if (hit_button(pos, &track, &t)) {
    toggle(track, t, event->modifiers() & Qt::ShiftModifier);
  }
}

void TrackHeaderWidget::mouseMoveEvent(QMouseEvent* event) {
  if (amber::ActiveSequence == nullptr) return;
  const int y = event->position().toPoint().y();

  if (resizing_) {
    // same rule as the clip area (TimelineWidget::mouseMoveMovingInit): video tracks grow upwards, audio downwards
    const int diff = y - resize_last_y_;
    int new_height = panel_timeline->GetTrackHeight(resize_track_);
    new_height = (resize_track_ < 0) ? new_height - diff : new_height + diff;
    panel_timeline->SetTrackHeight(resize_track_, qMax(new_height, amber::timeline::kTrackMinHeight));
    resize_last_y_ = y;
    panel_timeline->repaint_timeline();
    return;
  }

  if (resize_edge_track(y) != INT_MIN) {
    setCursor(Qt::SizeVerCursor);
  } else {
    unsetCursor();
  }
}

void TrackHeaderWidget::mouseReleaseEvent(QMouseEvent*) {
  resizing_ = false;
  resize_track_ = INT_MIN;
}

void TrackHeaderWidget::leaveEvent(QEvent*) {
  if (!resizing_) unsetCursor();
}

void TrackHeaderWidget::wheelEvent(QWheelEvent* event) {
  // vertical scroll only, by the same step as the clip area
  vertical_scrollbar_->setValue(vertical_scrollbar_->value() +
                                vertical_scrollbar_->singleStep() * event->angleDelta().y() / -120);
  event->accept();
}

bool TrackHeaderWidget::event(QEvent* event) {
  if (event->type() == QEvent::ToolTip) {
    auto* help = static_cast<QHelpEvent*>(event);
    int track;
    Toggle t;
    if (hit_button(help->pos(), &track, &t)) {
      QString text;
      switch (t) {
        case kToggleEye:
          text = tr("Toggle Track Visibility");
          break;
        case kToggleMute:
          text = tr("Mute Track");
          break;
        case kToggleSolo:
          text = tr("Solo Track");
          break;
        default:
          text = tr("Toggle Track Lock");
          break;
      }
      text += QStringLiteral("\n") +
              (track < 0 ? tr("Shift+click: all video tracks") : tr("Shift+click: all audio tracks"));
      QToolTip::showText(help->globalPos(), text, this);
    } else {
      QToolTip::hideText();
      event->ignore();
    }
    return true;
  }
  return QWidget::event(event);
}
