#ifndef CURVEEDITOR_H
#define CURVEEDITOR_H

#include <QTimer>
#include <QWidget>

#include "core/curves.h"

class QToolButton;

// Square tone curve editor (the Curves effect's field widget): five channel buttons (RGB, Luma, R, G, B), a Reset
// Channel button and the curve area. Knows nothing about effects: it shows a canonical curves string
// (core/curves.h) and reports the user's edits.
//
// Mouse: press on a point grabs it, press on the curve adds a point there and grabs it, press elsewhere does
// nothing. Double-click or drag more than 20 px out of the square vertically removes an inner point. Every gesture
// is one session: curvesEdited() live while it lasts, then editFinished() once (only if anything was emitted).
class CurveEditor : public QWidget {
  Q_OBJECT
 public:
  explicit CurveEditor(QWidget* parent = nullptr);

  // Ignored while a gesture is in progress (it would clobber the drag)
  void setCurves(const QString& value);
  QString curves() const;
  bool isEditing() const { return session_.active; }

  int channel() const { return channel_; }
  void setChannel(int channel);

  // Curve value (0-255 both axes) to widget coordinates, for tests
  QPointF valueToWidget(QPointF value) const;

  bool hasHeightForWidth() const override { return true; }
  int heightForWidth(int w) const override;
  QSize sizeHint() const override;
  QSize minimumSizeHint() const override;

  static constexpr int kPad = 8;  // ring around the square, so its edges and the delete zone show
  static constexpr int kMinSide = 160;
  static constexpr int kMaxSide = 360;
  static constexpr int kPointRadius = 8;      // a point is under the cursor within this distance
  static constexpr int kCurveRadius = 6;      // the curve is under the cursor within this distance
  static constexpr int kDeleteDistance = 20;  // an inner point dragged this far out of the square is removed
  static constexpr int kDragThreshold = 3;    // a grabbed point only moves once the mouse moved this far

 signals:
  // The full canonical curves string, live during a gesture
  void curvesEdited(const QString& value);
  // A gesture ended: `action` names it for the undo stack, `changed` is false when the curves are back where the
  // gesture started (a click with no movement, an added point removed again by the double-click)
  void editFinished(const QString& action, bool changed);

 protected:
  void paintEvent(QPaintEvent* e) override;
  void resizeEvent(QResizeEvent* e) override;
  void mousePressEvent(QMouseEvent* e) override;
  void mouseMoveEvent(QMouseEvent* e) override;
  void mouseReleaseEvent(QMouseEvent* e) override;
  void mouseDoubleClickEvent(QMouseEvent* e) override;
  void leaveEvent(QEvent* e) override;

 private:
  struct Session {
    bool active = false;
    bool emitted = false;
    QString start;   // canonical string when the gesture started
    QString action;  // undo text
    // point drag
    bool dragging = false;
    int index = -1;
    bool added = false;
    bool moved = false;
    bool ghost = false;
    QPoint grab_value;
    QPointF press_pos;
    amber::curves::Curve curve;  // the channel with the dragged point, ghost or not
  };

  QRectF square() const;
  int topRowHeight() const;
  int buttonRowWidth() const;
  void layoutButtons();
  QPointF widgetToValue(QPointF pos) const;
  int pointAt(QPointF pos) const;
  bool onCurve(QPointF pos) const;
  bool isInner(int index) const;
  void updateHover(QPointF pos);
  void updateButtons();

  void beginSession(const QString& action);
  void emitEdited();
  void finishSession();

  void dragTo(QPointF pos);
  void resetChannel();

  amber::curves::CurveSet curves_;
  int channel_ = amber::curves::kRgb;
  Session session_;
  QString last_emitted_;
  QTimer double_click_window_;  // after an add without movement: a double-click may still remove the point

  QToolButton* channel_buttons_[amber::curves::kChannelCount] = {};
  QToolButton* reset_button_ = nullptr;

  // hover state
  bool hovering_ = false;
  QPointF hover_pos_;
  int hover_index_ = -1;
  bool hover_curve_ = false;
};

#endif  // CURVEEDITOR_H
