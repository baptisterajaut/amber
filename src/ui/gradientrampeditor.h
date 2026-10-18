#ifndef GRADIENTRAMPEDITOR_H
#define GRADIENTRAMPEDITOR_H

#include <QWidget>

#include <functional>

#include "core/gradientramp.h"

// Colour ramp bar (the Gradient effect's Color Ramp field widget): the ramp painted over a checkerboard, with a
// triangular handle under it per stop. Knows nothing about effects: it shows a canonical ramp string
// (core/gradientramp.h) between two end colours and reports the user's edits.
//
// The end handles show the start/end colours and are fixed; double-clicking one picks that colour (endColorEdited).
// Mouse on the bar: a press adds a stop with the colour the ramp has there (the picture doesn't change), selects it and
// grabs it. A press on a handle selects and grabs it; dragging moves it (positions stay inside (0, 1), a stop may pass
// others), dragging it more than 20 px off the bar vertically removes it on release. Double-click a handle to pick its
// colour, Delete removes the selected stop, right-click for Add Stop Here / Remove Stop / Reverse Ramp.
//
// Every gesture is one session: rampEdited() live while it lasts, then editFinished() once (only if anything was
// emitted). Reverse Ramp is its own signal, so the owner can swap the end colours in the same undo step.
class GradientRampEditor : public QWidget {
  Q_OBJECT
 public:
  explicit GradientRampEditor(QWidget* parent = nullptr);
  // Ends a pending gesture (editFinished) so its edit isn't left without an undo entry
  ~GradientRampEditor() override;

  // Ignored while a gesture is in progress (it would clobber the drag)
  void setRamp(const QString& value);
  QString ramp() const;
  void setEndColors(const QColor& start, const QColor& end);
  QColor startColor() const { return start_; }
  QColor endColor() const { return end_; }
  // Whether double-clicking an end handle picks its colour (the owner can route it to a field)
  void setEndsEditable(bool editable) { ends_editable_ = editable; }

  bool isEditing() const { return session_.active; }
  int selected() const { return selected_; }  // index of the selected intermediate stop, -1 for none

  // The colour picker, QColorDialog by default; tests replace it. Returns an invalid colour on cancel.
  using ColorPicker = std::function<QColor(const QColor& initial, bool alpha, const QString& title)>;
  void setColorPicker(ColorPicker picker) { picker_ = std::move(picker); }

  // Geometry, for tests
  QRectF barRect() const;
  double posToX(double pos) const;
  double xToPos(double x) const;
  // Centre of a handle's triangle; index kStartHandle / kEndHandle for the ends
  QPointF handleCenter(int index) const;

  // Context menu actions, one undo entry each
  int addStopAt(double pos);  // the new stop's index, -1 at the cap
  void removeStop(int index);
  void reverseRamp();

  QSize sizeHint() const override;
  QSize minimumSizeHint() const override;

  static constexpr int kStartHandle = -2;
  static constexpr int kEndHandle = -3;
  static constexpr int kPad = 6;  // left/right, so the end handles fit
  static constexpr int kTop = 2;  // above the bar
  static constexpr int kBarHeight = 24;
  static constexpr int kHandleHeight = 10;
  static constexpr int kHandleHalfWidth = 5;
  static constexpr int kRemoveDistance = 20;  // a stop dragged this far off the bar vertically is removed
  static constexpr int kDragThreshold = 3;    // a grabbed stop only moves once the mouse moved this far

 signals:
  // The full canonical ramp string, live during a gesture
  void rampEdited(const QString& value);
  // A gesture ended: `action` names it for the undo stack, `changed` is false when the ramp is back where the gesture
  // started (a click on a handle with no movement, a stop added and dragged off again)
  void editFinished(const QString& action, bool changed);
  // An end handle's colour was picked: `end` 0 = start, 1 = end
  void endColorEdited(int end, const QColor& color);
  // Reverse Ramp: `value` is the mirrored stops; the end colours are to be swapped with it
  void rampReversed(const QString& value);

 protected:
  void paintEvent(QPaintEvent* e) override;
  void mousePressEvent(QMouseEvent* e) override;
  void mouseMoveEvent(QMouseEvent* e) override;
  void mouseReleaseEvent(QMouseEvent* e) override;
  void mouseDoubleClickEvent(QMouseEvent* e) override;
  void keyPressEvent(QKeyEvent* e) override;
  void contextMenuEvent(QContextMenuEvent* e) override;
  void leaveEvent(QEvent* e) override;
  void hideEvent(QHideEvent* e) override;
  bool event(QEvent* e) override;

 private:
  struct Session {
    bool active = false;
    bool emitted = false;
    QString start;   // canonical string when the gesture started
    QString action;  // undo text
    // stop drag
    bool dragging = false;
    bool added = false;
    bool moved = false;
    bool ghost = false;
    double grab_pos = 0;
    QPointF press_pos;
    amber::gradientramp::RampStop stop{0, QColor()};  // the dragged stop
    amber::gradientramp::Ramp others;                 // the ramp without it
  };

  int handleAt(QPointF pos) const;
  bool onBar(QPointF pos) const;
  QPolygonF handleShape(double x) const;
  void updateHover(QPointF pos);
  // Inserts after the stops at the same position, returns the index
  static int insertSorted(amber::gradientramp::Ramp& r, const amber::gradientramp::RampStop& s);

  void beginSession(const QString& action);
  void emitEdited();
  void finishSession();

  void grabStop(int index, QPointF pos, bool added);
  void dragTo(QPointF pos);
  void pickStopColor(int index);
  void pickEndColor(int which);

  amber::gradientramp::Ramp ramp_;
  QColor start_ = Qt::black;
  QColor end_ = Qt::white;
  bool ends_editable_ = false;
  int selected_ = -1;
  int last_added_ = -1;  // a stop the last click added without moving, for a double-click on the bar
  Session session_;
  QString last_emitted_;
  ColorPicker picker_;

  // hover state: the handle under it is looked up when painting, so it follows the ramp changing under a still mouse
  bool hovering_ = false;
  QPointF hover_pos_;
};

#endif  // GRADIENTRAMPEDITOR_H
