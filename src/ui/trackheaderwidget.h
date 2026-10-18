#ifndef TRACKHEADERWIDGET_H
#define TRACKHEADERWIDGET_H

#include <QIcon>
#include <QMap>
#include <QVector>
#include <QWidget>

#include "engine/sequence_fwd.h"
#include "engine/trackstate.h"

class QScrollBar;
class TimelineWidget;

// Premiere-like track header column left of the timeline tracks: the V1/A1 label, an eye (video hide), M/S (audio
// mute/solo) and a lock per row, from the active sequence's track_states. Audio rows tall enough get a second line
// with the track volume and pan, dragged like a LabelSlider. Painted with QPainter only, its rows reuse the
// TimelineWidget's geometry so they follow scrolling, zoom and track resize by repainting.
// CBA: no track names, target toggles or header menu; add if asked
class TrackHeaderWidget : public QWidget {
  Q_OBJECT
 public:
  TrackHeaderWidget(TimelineWidget* timeline, QScrollBar* vertical_scrollbar, QWidget* parent = nullptr);

 protected:
  void paintEvent(QPaintEvent* event) override;
  void mousePressEvent(QMouseEvent* event) override;
  void mouseMoveEvent(QMouseEvent* event) override;
  void mouseReleaseEvent(QMouseEvent* event) override;
  void mouseDoubleClickEvent(QMouseEvent* event) override;
  void leaveEvent(QEvent* event) override;
  void wheelEvent(QWheelEvent* event) override;
  bool event(QEvent* event) override;

 private:
  enum Toggle { kToggleNone, kToggleEye, kToggleMute, kToggleSolo, kToggleLock };
  struct Button {
    Toggle toggle;
    QRect rect;
  };
  enum Value { kValueNone, kValueVolume, kValuePan };

  // The rows painted: the sequence's track range plus the one empty track on each side, as the clip area
  void track_range(int* first, int* last);
  QRect row_rect(int track);
  // The line holding the label and buttons: the whole row, or its top part when the values line shows
  QRect first_line(int track, const QRect& row);
  // The volume (left) and pan (right) value areas of an audio row at least kValueRowMinHeight tall; false otherwise
  bool value_rects(int track, const QRect& row, QRect* volume, QRect* pan);
  QVector<Button> buttons_for_row(int track, const QRect& row);
  bool hit_button(const QPoint& pos, int* track, Toggle* toggle);
  bool hit_value(const QPoint& pos, int* track, Value* value);
  // The audio row whose label is under pos while the values line is hidden; INT_MIN if none
  int hit_short_audio_label(const QPoint& pos);
  // The track whose outer edge (top of a video row, bottom of an audio row) is within 3 px of y, the edge the clip
  // area resizes from; INT_MIN if none
  int resize_edge_track(int y);
  void toggle(int track, Toggle toggle, bool all_of_kind);
  void draw_button(QPainter& p, const Button& b, bool on);
  void draw_value(QPainter& p, const QRect& r, const QString& text, Qt::Alignment align, bool non_default, bool dimmed);
  // Sets one track's volume or pan on the dragged sequence, live and without an undo entry
  void apply_drag_value();
  void end_value_drag(bool push_undo);
  void reset_value(int track, Value value);

  TimelineWidget* timeline_;
  QScrollBar* vertical_scrollbar_;
  QIcon eye_icon_;
  QIcon eye_off_icon_;
  QIcon lock_icon_;
  QIcon unlock_icon_;

  int resize_track_;
  int resize_last_y_{0};
  bool resizing_{false};

  // fixed widths of the value areas, from the widest texts
  int volume_width_{0};
  int pan_width_{0};

  // Volume/pan drag: armed on press, live on the first move, pushed as one undo entry on release
  Value drag_value_{kValueNone};
  int drag_track_{0};
  SequencePtr drag_seq_;
  QMap<int, TrackState> drag_old_states_;
  // The live map the sequence pointed at before the current one: the render and cacher threads read track_states
  // unlocked, so a replaced map is freed one move later, not while a reader may still be in it
  QMap<int, TrackState> drag_prev_live_;
  int drag_volume_tenths_{0};
  int drag_pan_{0};
  QPoint drag_start_global_;
  QPoint drag_last_global_;
  bool drag_moved_{false};
};

#endif  // TRACKHEADERWIDGET_H
