#include <QApplication>
#include <QSignalSpy>
#include <QToolButton>
#include <QtTest>

#include "core/curves.h"
#include "ui/curveeditor.h"

using namespace amber::curves;

// Offscreen CurveEditor driven with real mouse event sequences (press / move / release / double-click as Qt delivers
// them), checking the curves string and the gesture signals the Effect Controls wrapper turns into undo entries.
class TestCurveEditor : public QObject {
  Q_OBJECT

  static void send(QWidget* w, QEvent::Type type, QPointF pos, Qt::MouseButton button, Qt::MouseButtons buttons) {
    QMouseEvent e(type, pos, w->mapToGlobal(pos), button, buttons, Qt::NoModifier);
    QApplication::sendEvent(w, &e);
  }
  static void press(QWidget* w, QPointF pos) { send(w, QEvent::MouseButtonPress, pos, Qt::LeftButton, Qt::LeftButton); }
  static void move(QWidget* w, QPointF pos) { send(w, QEvent::MouseMove, pos, Qt::NoButton, Qt::LeftButton); }
  static void release(QWidget* w, QPointF pos) {
    send(w, QEvent::MouseButtonRelease, pos, Qt::LeftButton, Qt::NoButton);
  }
  static void doubleClick(QWidget* w, QPointF pos) {
    press(w, pos);
    release(w, pos);
    send(w, QEvent::MouseButtonDblClick, pos, Qt::LeftButton, Qt::LeftButton);
    release(w, pos);
  }
  // Moves in a few steps, like a real drag
  static void drag(QWidget* w, QPointF from, QPointF to) {
    press(w, from);
    for (int i = 1; i <= 4; i++) move(w, from + (to - from) * (i / 4.0));
    release(w, to);
  }

  static QString with(Channel ch, Curve points) {
    CurveSet s = identity();
    s.channels[ch] = points;
    return serialize(s);
  }
  static Curve master(const CurveEditor& e) { return parse(e.curves()).channels[kRgb]; }

  // A child of a plain window, like in Effect Controls (a top-level editor's own min-height updates warn offscreen)
  std::unique_ptr<QWidget> host_;
  CurveEditor* make() {
    host_ = std::make_unique<QWidget>();
    CurveEditor* e = new CurveEditor(host_.get());
    const int w = CurveEditor::kMaxSide + 2 * CurveEditor::kPad;
    e->resize(w, e->heightForWidth(w));
    host_->resize(e->size());
    host_->show();
    if (!QTest::qWaitForWindowExposed(host_.get())) return nullptr;
    return e;
  }
  static double unit(const CurveEditor& e) {  // pixels per curve unit
    return (e.valueToWidget(QPointF(255, 0)).x() - e.valueToWidget(QPointF(0, 0)).x()) / 255.0;
  }

 private slots:
  void squareFollowsWidth() {
    CurveEditor e;
    QVERIFY(e.hasHeightForWidth());
    QCOMPARE(e.heightForWidth(1000) - e.heightForWidth(376), 0);  // capped at 360
    QCOMPARE(e.heightForWidth(276) - e.heightForWidth(176), 100);
    QCOMPARE(e.heightForWidth(100), e.heightForWidth(176));  // floor at 160
  }

  void pressOnCurveAddsAPoint() {
    auto e = make();
    QVERIFY(e);
    QSignalSpy edited(e, &CurveEditor::curvesEdited);
    QSignalSpy finished(e, &CurveEditor::editFinished);
    const QPointF at = e->valueToWidget(QPointF(64, 64));
    press(e, at);
    QCOMPARE(master(*e), Curve({{0, 0}, {64, 64}, {255, 255}}));
    QCOMPARE(edited.count(), 1);
    QVERIFY(e->isEditing());
    release(e, at);
    // An add without movement waits for a possible double-click before ending the gesture
    QCOMPARE(finished.count(), 0);
    QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, QApplication::doubleClickInterval() + 1000);
    QCOMPARE(finished.at(0).at(0).toString(), QStringLiteral("Add Curve Point"));
    QCOMPARE(finished.at(0).at(1).toBool(), true);
    QVERIFY(!e->isEditing());
  }

  // An add without movement waits for a double-click, but anything that could act on the undo stack or tear the
  // editor down first ends it, so the add always gets its undo entry before
  void pendingAddEndsOnDestroyKeyOrHide_data() {
    QTest::addColumn<int>("how");
    QTest::newRow("destroyed") << 0;
    QTest::newRow("key (Ctrl+Z)") << 1;
    QTest::newRow("press elsewhere") << 2;
    QTest::newRow("hidden") << 3;
  }
  void pendingAddEndsOnDestroyKeyOrHide() {
    QFETCH(int, how);
    auto e = make();
    QVERIFY(e);
    QSignalSpy finished(e, &CurveEditor::editFinished);
    const QPointF at = e->valueToWidget(QPointF(64, 64));
    press(e, at);
    release(e, at);
    QCOMPARE(finished.count(), 0);
    if (how == 0) {
      delete e;
    } else if (how == 1) {
      QTest::keyClick(host_.get(), Qt::Key_Z, Qt::ControlModifier);
    } else if (how == 2) {
      send(host_.get(), QEvent::MouseButtonPress, QPointF(1, 1), Qt::LeftButton, Qt::LeftButton);
    } else {
      e->hide();
    }
    QCOMPARE(finished.count(), 1);
    QCOMPARE(finished.at(0).at(0).toString(), QStringLiteral("Add Curve Point"));
    QCOMPARE(finished.at(0).at(1).toBool(), true);
    if (how != 0) QVERIFY(!e->isEditing());
  }

  void addedPointIsDraggedAtOnce() {
    auto e = make();
    QVERIFY(e);
    QSignalSpy finished(e, &CurveEditor::editFinished);
    const QPointF at = e->valueToWidget(QPointF(64, 64));
    drag(e, at, at + QPointF(0, -40 * unit(*e)));
    QCOMPARE(master(*e), Curve({{0, 0}, {64, 104}, {255, 255}}));
    QCOMPARE(finished.count(), 1);  // moved: no double-click wait
    QCOMPARE(finished.at(0).at(0).toString(), QStringLiteral("Add Curve Point"));
    QCOMPARE(finished.at(0).at(1).toBool(), true);
  }

  void pressAwayFromTheCurveDoesNothing() {
    auto e = make();
    QVERIFY(e);
    QSignalSpy edited(e, &CurveEditor::curvesEdited);
    press(e, e->valueToWidget(QPointF(200, 40)));
    move(e, e->valueToWidget(QPointF(210, 50)));
    release(e, e->valueToWidget(QPointF(210, 50)));
    QCOMPARE(edited.count(), 0);
    QCOMPARE(e->curves(), identity_string());
  }

  void pointWinsOverCurve() {
    // Pressing right next to a point grabs it (8 px) even though the curve is also under the cursor (6 px)
    auto e = make();
    QVERIFY(e);
    e->setCurves(with(kRgb, {{0, 0}, {128, 128}, {255, 255}}));
    QSignalSpy finished(e, &CurveEditor::editFinished);
    const QPointF at = e->valueToWidget(QPointF(128, 128)) + QPointF(5, 5);
    drag(e, at, at + QPointF(0, -20 * unit(*e)));
    QCOMPARE(master(*e).size(), 3);
    QCOMPARE(master(*e).at(1), QPoint(128, 148));
    QCOMPARE(finished.at(0).at(0).toString(), QStringLiteral("Move Curve Point"));
  }

  void clickWithoutMovementChangesNothing() {
    auto e = make();
    QVERIFY(e);
    const QString start = with(kRgb, {{0, 0}, {128, 160}, {255, 255}});
    e->setCurves(start);
    QSignalSpy edited(e, &CurveEditor::curvesEdited);
    QSignalSpy finished(e, &CurveEditor::editFinished);
    const QPointF at = e->valueToWidget(QPointF(128, 160));
    press(e, at);
    move(e, at + QPointF(1, 1));  // jitter under the drag threshold
    release(e, at + QPointF(1, 1));
    QCOMPARE(e->curves(), start);
    QCOMPARE(edited.count(), 0);
    QCOMPARE(finished.count(), 0);
  }

  void innerPointsDontCrossNeighbours() {
    auto e = make();
    QVERIFY(e);
    e->setCurves(with(kRgb, {{0, 0}, {100, 100}, {150, 150}, {255, 255}}));
    drag(e, e->valueToWidget(QPointF(100, 100)), e->valueToWidget(QPointF(220, 100)));
    QCOMPARE(master(*e).at(1), QPoint(149, 100));
  }

  void endpointsMoveInwardAndVertically() {
    auto e = make();
    QVERIFY(e);
    e->setCurves(with(kRgb, {{0, 0}, {100, 100}, {255, 255}}));
    // Left endpoint: horizontally up to its neighbour, never past it
    drag(e, e->valueToWidget(QPointF(0, 0)), e->valueToWidget(QPointF(180, 0)));
    QCOMPARE(master(*e).first(), QPoint(99, 0));
    // ...and back out to the edge, not beyond
    drag(e, e->valueToWidget(QPointF(99, 0)), e->valueToWidget(QPointF(-40, 30)));
    QCOMPARE(master(*e).first(), QPoint(0, 30));
    // Right endpoint: vertically free (output white), dragging far out of the square clamps instead of deleting
    drag(e, e->valueToWidget(QPointF(255, 255)), e->valueToWidget(QPointF(255, -200)));
    QCOMPARE(master(*e).size(), 3);
    QCOMPARE(master(*e).last(), QPoint(255, 0));
  }

  void doubleClickRemovesAnInnerPoint() {
    auto e = make();
    QVERIFY(e);
    e->setCurves(with(kRgb, {{0, 0}, {128, 192}, {255, 255}}));
    QSignalSpy finished(e, &CurveEditor::editFinished);
    doubleClick(e, e->valueToWidget(QPointF(128, 192)));
    QCOMPARE(master(*e), identity_curve());
    QCOMPARE(finished.count(), 1);
    QCOMPARE(finished.at(0).at(0).toString(), QStringLiteral("Delete Curve Point"));
    QCOMPARE(finished.at(0).at(1).toBool(), true);
  }

  void doubleClickOnBareCurveIsNoChange() {
    // The first press adds a point, the double-click removes it again: net no change, so no undo entry
    auto e = make();
    QVERIFY(e);
    const QString start = with(kRgb, {{0, 0}, {200, 220}, {255, 255}});
    e->setCurves(start);
    QSignalSpy finished(e, &CurveEditor::editFinished);
    const int y = qRound(evaluate(parse(start).channels[kRgb], 80));
    doubleClick(e, e->valueToWidget(QPointF(80, y)));
    QCOMPARE(e->curves(), start);
    QCOMPARE(finished.count(), 1);
    QCOMPARE(finished.at(0).at(1).toBool(), false);
    QVERIFY(!e->isEditing());
  }

  void doubleClickOnEndpointKeepsIt() {
    auto e = make();
    QVERIFY(e);
    QSignalSpy finished(e, &CurveEditor::editFinished);
    doubleClick(e, e->valueToWidget(QPointF(0, 0)));
    QCOMPARE(e->curves(), identity_string());
    QCOMPARE(finished.count(), 0);
  }

  void dragOutRemovesAndDragBackKeeps() {
    auto e = make();
    QVERIFY(e);
    e->setCurves(with(kRgb, {{0, 0}, {128, 192}, {255, 255}}));
    QSignalSpy finished(e, &CurveEditor::editFinished);
    const QPointF at = e->valueToWidget(QPointF(128, 192));
    const QPointF below_square = e->valueToWidget(QPointF(128, 0)) + QPointF(0, CurveEditor::kDeleteDistance + 10);
    press(e, at);
    move(e, at + QPointF(0, 10));
    move(e, below_square);
    QCOMPARE(master(*e), identity_curve());  // ghost: out of the curve while outside
    QVERIFY(e->isEditing());
    move(e, e->valueToWidget(QPointF(128, 100)));  // back in: kept, where the cursor is
    QCOMPARE(master(*e).size(), 3);
    QCOMPARE(master(*e).at(1), QPoint(128, 100));
    release(e, e->valueToWidget(QPointF(128, 100)));
    QCOMPARE(finished.count(), 1);
    QCOMPARE(finished.at(0).at(0).toString(), QStringLiteral("Move Curve Point"));

    // Out and released there: removed
    drag(e, e->valueToWidget(QPointF(128, 100)), below_square);
    QCOMPARE(master(*e), identity_curve());
    QCOMPARE(finished.count(), 2);
    QCOMPARE(finished.at(1).at(0).toString(), QStringLiteral("Delete Curve Point"));
    QCOMPARE(finished.at(1).at(1).toBool(), true);

    // Not far enough out (inside the 20 px): kept, clamped to the bottom
    e->setCurves(with(kRgb, {{0, 0}, {128, 192}, {255, 255}}));
    drag(e, e->valueToWidget(QPointF(128, 192)), e->valueToWidget(QPointF(128, 0)) + QPointF(0, 10));
    QCOMPARE(master(*e).at(1), QPoint(128, 0));
  }

  void setCurvesIgnoredWhileDragging() {
    auto e = make();
    QVERIFY(e);
    const QPointF at = e->valueToWidget(QPointF(64, 64));
    press(e, at);
    move(e, at + QPointF(0, -20));
    const QString during = e->curves();
    e->setCurves(identity_string());  // what the Effect Controls refresh would push back mid-drag
    QCOMPARE(e->curves(), during);
    release(e, at + QPointF(0, -20));
    e->setCurves(identity_string());
    QCOMPARE(e->curves(), identity_string());
  }

  void channelsAreIndependent() {
    auto e = make();
    QVERIFY(e);
    e->setChannel(kRed);
    drag(e, e->valueToWidget(QPointF(255, 255)), e->valueToWidget(QPointF(255, 0)));
    const CurveSet s = parse(e->curves());
    QCOMPARE(s.channels[kRed], Curve({{0, 0}, {255, 0}}));
    QCOMPARE(s.channels[kRgb], identity_curve());
    QCOMPARE(s.channels[kLuma], identity_curve());
  }

  void resetChannelResetsOnlyTheSelectedOne() {
    auto e = make();
    QVERIFY(e);
    CurveSet s = identity();
    s.channels[kRgb] = {{0, 0}, {128, 192}, {255, 255}};
    s.channels[kBlue] = {{0, 40}, {255, 255}};
    e->setCurves(serialize(s));
    e->setChannel(kBlue);
    QSignalSpy finished(e, &CurveEditor::editFinished);
    QToolButton* reset = nullptr;
    for (QToolButton* b : e->findChildren<QToolButton*>()) {
      if (b->text() == QStringLiteral("Reset Channel")) reset = b;
    }
    QVERIFY(reset);
    QVERIFY(reset->isEnabled());
    reset->click();
    s.channels[kBlue] = identity_curve();
    QCOMPARE(e->curves(), serialize(s));
    QCOMPARE(finished.count(), 1);
    QCOMPARE(finished.at(0).at(0).toString(), QStringLiteral("Reset Curve Channel"));
    QVERIFY(!reset->isEnabled());  // nothing left to reset on this channel
  }
};

QTEST_MAIN(TestCurveEditor)
#include "test_curveeditor.moc"
