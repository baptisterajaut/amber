#ifndef TRACKHEADERWIDGET_H
#define TRACKHEADERWIDGET_H

#include <QIcon>
#include <QVector>
#include <QWidget>

class QScrollBar;
class TimelineWidget;

// Premiere-like track header column left of the timeline tracks: the V1/A1 label, an eye (video hide), M/S (audio
// mute/solo) and a lock per row, from the active sequence's track_states. Painted with QPainter only, its rows reuse
// the TimelineWidget's geometry so they follow scrolling, zoom and track resize by repainting.
// CBA: no track names, target toggles or header menu; add with per-track volume/pan
class TrackHeaderWidget : public QWidget {
  Q_OBJECT
 public:
  TrackHeaderWidget(TimelineWidget* timeline, QScrollBar* vertical_scrollbar, QWidget* parent = nullptr);

 protected:
  void paintEvent(QPaintEvent* event) override;
  void mousePressEvent(QMouseEvent* event) override;
  void mouseMoveEvent(QMouseEvent* event) override;
  void mouseReleaseEvent(QMouseEvent* event) override;
  void leaveEvent(QEvent* event) override;
  void wheelEvent(QWheelEvent* event) override;
  bool event(QEvent* event) override;

 private:
  enum Toggle { kToggleNone, kToggleEye, kToggleMute, kToggleSolo, kToggleLock };
  struct Button {
    Toggle toggle;
    QRect rect;
  };

  // The rows painted: the sequence's track range plus the one empty track on each side, as the clip area
  void track_range(int* first, int* last);
  QRect row_rect(int track);
  QVector<Button> buttons_for_row(int track, const QRect& row);
  bool hit_button(const QPoint& pos, int* track, Toggle* toggle);
  // The track whose outer edge (top of a video row, bottom of an audio row) is within 3 px of y, the edge the clip
  // area resizes from; INT_MIN if none
  int resize_edge_track(int y);
  void toggle(int track, Toggle toggle, bool all_of_kind);
  void draw_button(QPainter& p, const Button& b, bool on);

  TimelineWidget* timeline_;
  QScrollBar* vertical_scrollbar_;
  QIcon eye_icon_;
  QIcon eye_off_icon_;
  QIcon lock_icon_;
  QIcon unlock_icon_;

  int resize_track_;
  int resize_last_y_{0};
  bool resizing_{false};
};

#endif  // TRACKHEADERWIDGET_H
