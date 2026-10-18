#include "trackheaderwidget.h"

#include <QCursor>
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
#include "ui/styling.h"
#include "ui/timelinewidget.h"

namespace {

constexpr int kButtonSize = 18;
constexpr int kButtonSpacing = 2;
constexpr int kRightMargin = 4;
constexpr int kLabelPadding = 6;
constexpr int kResizeRange = 3;
// Below this row height only the label is drawn
constexpr int kMinButtonRowHeight = 10;
// Audio rows at least this tall get the volume and pan line under the label and buttons (the default height is 40)
constexpr int kValueRowMinHeight = 34;
// The label and buttons line above the values line: the buttons and 1 px above and below
constexpr int kFirstLineHeight = kButtonSize + 2;
// Minimum space between the volume and the pan value
constexpr int kValueGap = 8;

const QColor kMuteAccent(224, 160, 48);
const QColor kSoloAccent(64, 176, 80);
const QColor kLockAccent(96, 118, 148);
const QColor kHiddenAccent(72, 72, 72);

QFont label_font(const QFont& base) {
  QFont f = base;
  f.setBold(true);
  return f;
}

QString volume_text(double db) {
  if (db <= amber::kTrackVolumeMinDb) return QStringLiteral("-\u221E dB");
  return (db > 0 ? QStringLiteral("+") : QString()) + QString::number(db, 'f', 1) + QStringLiteral(" dB");
}

QString pan_text(int pan) {
  if (pan == 0) return QStringLiteral("C");
  return (pan < 0 ? QStringLiteral("L%1") : QStringLiteral("R%1")).arg(qAbs(pan));
}

// LabelSlider's colour (LabelSlider::SetColor)
QColor value_accent() { return amber::styling::UseDarkIcons() ? QColor(0x00, 0x80, 0xff) : QColor(0xff, 0xc0, 0x00); }

}  // namespace

TrackHeaderWidget::TrackHeaderWidget(TimelineWidget* timeline, QScrollBar* vertical_scrollbar, QWidget* parent)
    : QWidget(parent), timeline_(timeline), vertical_scrollbar_(vertical_scrollbar), resize_track_(INT_MIN) {
  eye_icon_ = amber::icon::CreateIconFromSVG(QStringLiteral(":/icons/eye.svg"));
  eye_off_icon_ = amber::icon::CreateIconFromSVG(QStringLiteral(":/icons/eye-off.svg"));
  lock_icon_ = amber::icon::CreateIconFromSVG(QStringLiteral(":/icons/lock.svg"));
  unlock_icon_ = amber::icon::CreateIconFromSVG(QStringLiteral(":/icons/unlock.svg"));

  setMouseTracking(true);

  // the widest volume and pan texts, so the values line never clips
  const QFontMetrics value_fm(font());
  for (const QString& text : {QStringLiteral("-60.0 dB"), QStringLiteral("-12.5 dB"), QStringLiteral("+12.0 dB"),
                              volume_text(amber::kTrackVolumeMinDb)}) {
    volume_width_ = qMax(volume_width_, value_fm.horizontalAdvance(text));
  }
  for (const QString& text : {QStringLiteral("L100"), QStringLiteral("R100")}) {
    pan_width_ = qMax(pan_width_, value_fm.horizontalAdvance(text));
  }

  // label ("V88" in bold) + three buttons, or the two values if wider, scaled for the font. A fixed width.
  const QFontMetrics fm(label_font(font()));
  const int buttons_width = kLabelPadding + fm.horizontalAdvance(QStringLiteral("V88")) + kLabelPadding +
                            3 * kButtonSize + 2 * kButtonSpacing + kRightMargin;
  const int values_width = kLabelPadding + volume_width_ + kValueGap + pan_width_ + kRightMargin + 1;
  setFixedWidth(qMax(96, qMax(buttons_width, values_width)));

  connect(vertical_scrollbar_, &QScrollBar::valueChanged, this, qOverload<>(&QWidget::update));
}

void TrackHeaderWidget::track_range(int* first, int* last) {
  int video_limit = 0;
  int audio_limit = 0;
  amber::ActiveSequence->getShownTrackLimits(&video_limit, &audio_limit);
  *first = video_limit - 1;
  *last = audio_limit + 1;
}

QRect TrackHeaderWidget::row_rect(int track) {
  QRect r = timeline_->track_rect(track);
  r.setLeft(0);
  r.setWidth(width());
  return r;
}

QRect TrackHeaderWidget::first_line(int track, const QRect& row) {
  QRect volume, pan;
  if (!value_rects(track, row, &volume, &pan)) return row;
  return QRect(row.left(), volume.top() - kFirstLineHeight, row.width(), kFirstLineHeight);
}

bool TrackHeaderWidget::value_rects(int track, const QRect& row, QRect* volume, QRect* pan) {
  if (track < 0 || row.height() < kValueRowMinHeight) return false;
  const int line_height = qMin(QFontMetrics(font()).height(), row.height() - kFirstLineHeight);
  // both lines centred together in the row
  const int top = row.top() + (row.height() - kFirstLineHeight - line_height) / 2 + kFirstLineHeight;
  *volume = QRect(row.left() + kLabelPadding, top, volume_width_, line_height);
  // right-aligned with the lock button
  *pan = QRect(row.right() - kRightMargin - pan_width_ + 1, top, pan_width_, line_height);
  return true;
}

QVector<TrackHeaderWidget::Button> TrackHeaderWidget::buttons_for_row(int track, const QRect& row) {
  QVector<Button> buttons;
  if (row.height() < kMinButtonRowHeight) return buttons;

  // shrink on rows shorter than the buttons (the 15 px minimum track height)
  const QRect line = first_line(track, row);
  const int size = qMin(kButtonSize, line.height() - 2);
  const int y = line.top() + (line.height() - size) / 2;
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

bool TrackHeaderWidget::hit_value(const QPoint& pos, int* track, Value* value) {
  if (amber::ActiveSequence == nullptr) return false;
  int first, last;
  track_range(&first, &last);
  for (int t = qMax(first, 0); t <= last; t++) {
    const QRect row = row_rect(t);
    if (!row.contains(pos)) continue;
    // the bottom edge band resizes the track
    if (pos.y() >= row.top() + row.height() - kResizeRange) return false;
    QRect volume, pan;
    if (!value_rects(t, row, &volume, &pan)) return false;
    // a little wider than the texts, easier to grab
    if (volume.adjusted(-kLabelPadding / 2, 0, kValueGap / 2, 0).contains(pos)) {
      *value = kValueVolume;
    } else if (pan.adjusted(-kValueGap / 2, 0, kRightMargin, 0).contains(pos)) {
      *value = kValuePan;
    } else {
      return false;
    }
    *track = t;
    return true;
  }
  return false;
}

int TrackHeaderWidget::hit_short_audio_label(const QPoint& pos) {
  if (amber::ActiveSequence == nullptr) return INT_MIN;
  int first, last;
  track_range(&first, &last);
  for (int t = qMax(first, 0); t <= last; t++) {
    const QRect row = row_rect(t);
    if (!row.contains(pos)) continue;
    QRect volume, pan;
    if (value_rects(t, row, &volume, &pan)) return INT_MIN;
    const QVector<Button> buttons = buttons_for_row(t, row);
    if (!buttons.isEmpty() && pos.x() >= buttons.last().rect.left()) return INT_MIN;
    return t;
  }
  return INT_MIN;
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
  // its redo repaints, and rebuilds Effect Controls when a lock changes
  amber::UndoStack.push(cmd);
}

void TrackHeaderWidget::apply_drag_value() {
  TrackState s = drag_old_states_.value(drag_track_);
  if (drag_value_ == kValueVolume) {
    s.volume_db = drag_volume_tenths_ / 10.0;
  } else {
    s.pan = drag_pan_;
  }
  QMap<int, TrackState> live = drag_old_states_;
  if (s.is_default()) {
    live.remove(drag_track_);
  } else {
    live.insert(drag_track_, s);
  }
  // swap whole maps, as SetTrackStateCommand: the replaced one stays alive in drag_prev_live_ until the next move
  drag_prev_live_ = drag_seq_->track_states;
  drag_seq_->track_states = live;
  update();
}

void TrackHeaderWidget::end_value_drag(bool push_undo) {
  const SequencePtr seq = drag_seq_;
  const Value value = drag_value_;
  drag_value_ = kValueNone;
  drag_seq_ = nullptr;
  if (drag_moved_) setCursor(Qt::SizeHorCursor);  // the cursor is back where the drag started, on the value
  drag_moved_ = false;
  if (seq == nullptr) return;

  if (!push_undo) {
    drag_prev_live_ = seq->track_states;
    seq->track_states = drag_old_states_;
    update();
    return;
  }
  const QMap<int, TrackState> new_states = seq->track_states;
  if (new_states == drag_old_states_) return;
  // one entry for the whole drag; its first redo re-applies the live value
  auto* cmd = new SetTrackStateCommand(seq.get(), drag_old_states_, new_states);
  cmd->setText(value == kValueVolume ? tr("Change Track Volume") : tr("Change Track Pan"));
  amber::UndoStack.push(cmd);
}

void TrackHeaderWidget::reset_value(int track, Value value) {
  Sequence* seq = amber::ActiveSequence.get();
  if (seq == nullptr || value == kValueNone) return;
  TrackState s = seq->track_state(track);
  if (value == kValueVolume) {
    if (s.volume_db == 0.0) return;
    s.volume_db = 0.0;
  } else {
    if (s.pan == 0) return;
    s.pan = 0;
  }
  const QMap<int, TrackState> old_states = seq->track_states;
  QMap<int, TrackState> new_states = old_states;
  if (s.is_default()) {
    new_states.remove(track);
  } else {
    new_states.insert(track, s);
  }
  auto* cmd = new SetTrackStateCommand(seq, old_states, new_states);
  cmd->setText(value == kValueVolume ? tr("Reset Track Volume") : tr("Reset Track Pan"));
  amber::UndoStack.push(cmd);
}

void TrackHeaderWidget::draw_value(QPainter& p, const QRect& r, const QString& text, Qt::Alignment align,
                                   bool non_default, bool dimmed) {
  QColor color = non_default ? value_accent() : palette().color(QPalette::WindowText);
  if (dimmed) color.setAlpha(110);
  const QFontMetrics fm(p.font());
  const int text_width = fm.horizontalAdvance(text);
  const int x = (align & Qt::AlignRight) ? r.right() + 1 - text_width : r.left();
  const int baseline = r.top() + (r.height() - fm.height()) / 2 + fm.ascent();
  p.setPen(color);
  p.drawText(x, baseline, text);
  // dotted underline: draggable, as LabelSlider's underlined values
  QPen pen(color);
  pen.setStyle(Qt::DotLine);
  p.setPen(pen);
  const int y = qMin(baseline + qMax(1, fm.underlinePos()), r.bottom());
  p.drawLine(x, y, x + text_width - 1, y);
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
    QRect label_rect = first_line(t, row).adjusted(kLabelPadding, 0, 0, 0);
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

    // volume and pan, kept and dimmed like the clips on a muted or soloed-out track
    QRect volume_rect, pan_rect;
    if (value_rects(t, row, &volume_rect, &pan_rect)) {
      draw_value(p, volume_rect, volume_text(state.volume_db), Qt::AlignLeft, state.volume_db != 0.0, dimmed);
      draw_value(p, pan_rect, pan_text(state.pan), Qt::AlignRight, state.pan != 0, dimmed);
    }
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

  // buttons first: on a short row the resize band overlaps them
  int track;
  Toggle t;
  if (hit_button(pos, &track, &t)) {
    toggle(track, t, event->modifiers() & Qt::ShiftModifier);
    return;
  }

  // volume or pan: armed here, applied live from the first move, one undo entry on release. Locking doesn't block it,
  // as it doesn't block mute and solo: a lock protects edits, not the mix.
  Value value;
  if (hit_value(pos, &track, &value)) {
    const TrackState state = amber::ActiveSequence->track_state(track);
    drag_value_ = value;
    drag_track_ = track;
    drag_seq_ = amber::ActiveSequence;
    drag_old_states_ = drag_seq_->track_states;
    drag_volume_tenths_ = qRound(state.volume_db * 10.0);
    drag_pan_ = state.pan;
    drag_start_global_ = QCursor::pos();
    drag_last_global_ = drag_start_global_;
    drag_moved_ = false;
    return;
  }

  const int edge_track = resize_edge_track(pos.y());
  if (edge_track != INT_MIN) {
    resizing_ = true;
    resize_track_ = edge_track;
    resize_last_y_ = pos.y();
  }
}

void TrackHeaderWidget::mouseMoveEvent(QMouseEvent* event) {
  if (drag_value_ != kValueNone) {
    if (drag_seq_ != amber::ActiveSequence) {
      end_value_drag(false);
      return;
    }
    // the cursor's position now rather than the event's, as LabelSlider: events queued before the warp below are stale
    const QPoint global = QCursor::pos();
    const int dx = global.x() - drag_last_global_.x();
    if (dx == 0) return;
    const bool big_steps = event->modifiers() & Qt::ShiftModifier;
    if (drag_value_ == kValueVolume) {
      // 0.1 dB per pixel, 1 dB with Shift
      drag_volume_tenths_ =
          qBound(qRound(amber::kTrackVolumeMinDb * 10), drag_volume_tenths_ + dx * (big_steps ? 10 : 1),
                 qRound(amber::kTrackVolumeMaxDb * 10));
    } else {
      // 1 per pixel, 5 with Shift
      drag_pan_ = qBound(-amber::kTrackPanLimit, drag_pan_ + dx * (big_steps ? 5 : 1), amber::kTrackPanLimit);
    }
    if (!drag_moved_) {
      drag_moved_ = true;
      setCursor(Qt::BlankCursor);
    }
    apply_drag_value();
    // as LabelSlider, the hidden cursor goes back to where the drag started so it never runs into a screen edge.
    // Where warping isn't allowed (Wayland) it just moves on, and the delta from its last position still holds.
    QCursor::setPos(drag_start_global_);
    drag_last_global_ = QCursor::pos();
    return;
  }

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

  const QPoint pos = event->position().toPoint();
  int track;
  Toggle t;
  Value value;
  if (hit_button(pos, &track, &t)) {
    unsetCursor();
  } else if (hit_value(pos, &track, &value)) {
    setCursor(Qt::SizeHorCursor);
  } else if (resize_edge_track(y) != INT_MIN) {
    setCursor(Qt::SizeVerCursor);
  } else {
    unsetCursor();
  }
}

void TrackHeaderWidget::mouseReleaseEvent(QMouseEvent*) {
  if (drag_value_ != kValueNone) end_value_drag(true);
  resizing_ = false;
  resize_track_ = INT_MIN;
}

void TrackHeaderWidget::mouseDoubleClickEvent(QMouseEvent* event) {
  int track;
  Value value;
  if (event->button() == Qt::LeftButton && hit_value(event->position().toPoint(), &track, &value)) {
    reset_value(track, value);
    return;
  }
  // QWidget's default: a double-click on a button toggles it twice
  mousePressEvent(event);
}

void TrackHeaderWidget::leaveEvent(QEvent*) {
  if (!resizing_ && drag_value_ == kValueNone) unsetCursor();
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
    Value value;
    if (hit_value(help->pos(), &track, &value)) {
      QToolTip::showText(help->globalPos(),
                         value == kValueVolume
                             ? tr("Track volume: drag to change, Shift for bigger steps, double-click to reset")
                             : tr("Track pan: drag to change, Shift for bigger steps, double-click to reset"),
                         this);
    } else if ((track = hit_short_audio_label(help->pos())) != INT_MIN) {
      const TrackState state = amber::ActiveSequence->track_state(track);
      QToolTip::showText(help->globalPos(),
                         tr("Volume %1, pan %2\nMake the track taller to change them")
                             .arg(volume_text(state.volume_db), pan_text(state.pan)),
                         this);
    } else if (hit_button(help->pos(), &track, &t)) {
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
