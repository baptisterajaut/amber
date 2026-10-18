#include <QApplication>
#include <QSignalSpy>
#include <QtTest>

#include "core/gradientramp.h"
#include "ui/gradientrampeditor.h"

using namespace amber::gradientramp;

// ui/menu.cpp (the context menu) calls it; the real one lives in global/global.cpp, which pulls the app
#include "global/global.h"
void AmberGlobal::SetNativeStyling(QWidget*) {}

// Offscreen GradientRampEditor driven with real mouse event sequences (press / move / release / double-click as Qt
// delivers them), checking the ramp string and the gesture signals the Effect Controls wrapper turns into undo entries.
class TestGradientRampEditor : public QObject {
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
  static void click(QWidget* w, QPointF pos) {
    press(w, pos);
    release(w, pos);
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

  // A child of a plain window, like in Effect Controls
  std::unique_ptr<QWidget> host_;
  GradientRampEditor* make(const QString& ramp = QString()) {
    host_ = std::make_unique<QWidget>();
    GradientRampEditor* e = new GradientRampEditor(host_.get());
    e->resize(406, e->sizeHint().height());  // a 394 px bar
    host_->resize(e->size());
    e->setRamp(ramp);
    host_->show();
    if (!QTest::qWaitForWindowExposed(host_.get())) return nullptr;
    return e;
  }
  static QPointF onBar(const GradientRampEditor& e, double pos) {
    return QPointF(e.posToX(pos), e.barRect().center().y());
  }

 private slots:
  void pressOnBarAddsTheRampColour() {
    auto e = make();
    QVERIFY(e);
    QSignalSpy edited(e, &GradientRampEditor::rampEdited);
    QSignalSpy finished(e, &GradientRampEditor::editFinished);
    press(e, onBar(*e, 0.25));
    QCOMPARE(edited.count(), 1);
    QVERIFY(e->isEditing());
    const Ramp r = parse(e->ramp());
    QCOMPARE(r.size(), 1);
    QVERIFY(qAbs(r[0].pos - 0.25) < 0.003);
    // The colour the black -> white ramp had there: the picture is unchanged
    QCOMPARE(r[0].color, color_at(stops(Qt::black, Qt::white, Ramp()), r[0].pos));
    QCOMPARE(e->selected(), 0);
    release(e, onBar(*e, 0.25));
    QCOMPARE(finished.count(), 1);
    QCOMPARE(finished[0][0].toString(), QStringLiteral("Add Color Stop"));
    QCOMPARE(finished[0][1].toBool(), true);
    QVERIFY(!e->isEditing());
  }

  void clickOnAHandleSelectsWithoutEdit() {
    auto e = make(QStringLiteral("0.25 #ff0000ff;0.75 #0000ffff"));
    QVERIFY(e);
    QSignalSpy edited(e, &GradientRampEditor::rampEdited);
    QSignalSpy finished(e, &GradientRampEditor::editFinished);
    click(e, e->handleCenter(1));
    QCOMPARE(e->selected(), 1);
    QCOMPARE(edited.count(), 0);
    QCOMPARE(finished.count(), 0);
    // An end handle is fixed: no selection, no edit
    click(e, e->handleCenter(GradientRampEditor::kStartHandle));
    QCOMPARE(e->selected(), -1);
    QCOMPARE(edited.count(), 0);
  }

  void dragMovesAndPassesOthers() {
    auto e = make(QStringLiteral("0.25 #ff0000ff;0.75 #0000ffff"));
    QVERIFY(e);
    QSignalSpy finished(e, &GradientRampEditor::editFinished);
    drag(e, e->handleCenter(0), e->handleCenter(0) + QPointF(0.65 * e->barRect().width(), 0));
    const Ramp r = parse(e->ramp());
    QCOMPARE(r.size(), 2);
    QCOMPARE(r[0].color, QColor(Qt::blue));
    QCOMPARE(r[1].color, QColor(Qt::red));
    QVERIFY(qAbs(r[1].pos - 0.9) < 0.003);
    QCOMPARE(e->selected(), 1);  // the dragged stop stays selected
    QCOMPARE(finished.count(), 1);
    QCOMPARE(finished[0][0].toString(), QStringLiteral("Move Color Stop"));
    QCOMPARE(finished[0][1].toBool(), true);
  }

  void dragStaysInside() {
    auto e = make(QStringLiteral("0.5 #ff0000ff"));
    QVERIFY(e);
    drag(e, e->handleCenter(0), e->handleCenter(0) + QPointF(1000, 0));
    QCOMPARE(e->ramp(), QStringLiteral("0.9999 #ff0000ff"));
    drag(e, e->handleCenter(0), e->handleCenter(0) - QPointF(1000, 0));
    QCOMPARE(e->ramp(), QStringLiteral("0.0001 #ff0000ff"));
  }

  void dragOffTheBarRemoves() {
    auto e = make(QStringLiteral("0.25 #ff0000ff;0.75 #0000ffff"));
    QVERIFY(e);
    QSignalSpy finished(e, &GradientRampEditor::editFinished);
    const QPointF from = e->handleCenter(0);
    press(e, from);
    move(e, from + QPointF(0, 10));
    QCOMPARE(parse(e->ramp()).size(), 2);  // not far enough yet
    move(e, from + QPointF(0, 40));
    QCOMPARE(e->ramp(), QStringLiteral("0.75 #0000ffff"));  // shown without it while out
    move(e, from + QPointF(0, 5));
    QCOMPARE(parse(e->ramp()).size(), 2);  // back: it returns
    move(e, from + QPointF(0, 40));
    release(e, from + QPointF(0, 40));
    QCOMPARE(e->ramp(), QStringLiteral("0.75 #0000ffff"));
    QCOMPARE(e->selected(), -1);
    QCOMPARE(finished.count(), 1);
    QCOMPARE(finished[0][0].toString(), QStringLiteral("Remove Color Stop"));
    QCOMPARE(finished[0][1].toBool(), true);
  }

  void addThenDragOffIsNoChange() {
    auto e = make();
    QVERIFY(e);
    QSignalSpy finished(e, &GradientRampEditor::editFinished);
    drag(e, onBar(*e, 0.5), onBar(*e, 0.5) + QPointF(0, 60));
    QCOMPARE(e->ramp(), QString());
    QCOMPARE(finished.count(), 1);
    QCOMPARE(finished[0][1].toBool(), false);
  }

  void deleteKeyRemovesTheSelectedStop() {
    auto e = make(QStringLiteral("0.25 #ff0000ff;0.75 #0000ffff"));
    QVERIFY(e);
    QSignalSpy finished(e, &GradientRampEditor::editFinished);
    click(e, e->handleCenter(0));
    QTest::keyClick(e, Qt::Key_Delete);
    QCOMPARE(e->ramp(), QStringLiteral("0.75 #0000ffff"));
    QCOMPARE(finished.count(), 1);
    QCOMPARE(finished[0][0].toString(), QStringLiteral("Remove Color Stop"));
    // Nothing selected: Delete does nothing
    QTest::keyClick(e, Qt::Key_Delete);
    QCOMPARE(e->ramp(), QStringLiteral("0.75 #0000ffff"));
    QCOMPARE(finished.count(), 1);
  }

  void doubleClickAHandlePicksItsColour() {
    auto e = make(QStringLiteral("0.5 #ff0000ff"));
    QVERIFY(e);
    bool asked_alpha = false;
    e->setColorPicker([&](const QColor& initial, bool alpha, const QString&) {
      asked_alpha = alpha;
      return initial == QColor(Qt::red) ? QColor(0, 255, 0, 128) : QColor();
    });
    QSignalSpy finished(e, &GradientRampEditor::editFinished);
    doubleClick(e, e->handleCenter(0));
    QVERIFY(asked_alpha);
    QCOMPARE(e->ramp(), QStringLiteral("0.5 #00ff0080"));
    QCOMPARE(finished.count(), 1);
    QCOMPARE(finished[0][0].toString(), QStringLiteral("Change Color Stop"));
    // Cancel: nothing
    e->setColorPicker([](const QColor&, bool, const QString&) { return QColor(); });
    doubleClick(e, e->handleCenter(0));
    QCOMPARE(e->ramp(), QStringLiteral("0.5 #00ff0080"));
    QCOMPARE(finished.count(), 1);
  }

  void doubleClickTheBarAddsThenPicks() {
    auto e = make();
    QVERIFY(e);
    e->setColorPicker([](const QColor&, bool, const QString&) { return QColor(Qt::red); });
    QSignalSpy finished(e, &GradientRampEditor::editFinished);
    doubleClick(e, onBar(*e, 0.5));
    const Ramp r = parse(e->ramp());
    QCOMPARE(r.size(), 1);  // one stop, not two
    QCOMPARE(r[0].color, QColor(Qt::red));
    QCOMPARE(finished.count(), 2);
    QCOMPARE(finished[0][0].toString(), QStringLiteral("Add Color Stop"));
    QCOMPARE(finished[1][0].toString(), QStringLiteral("Change Color Stop"));
  }

  void endHandlesPickTheEndColours() {
    auto e = make();
    QVERIFY(e);
    bool asked_alpha = true;
    e->setColorPicker([&](const QColor&, bool alpha, const QString&) {
      asked_alpha = alpha;
      return QColor(Qt::red);
    });
    QSignalSpy ends(e, &GradientRampEditor::endColorEdited);
    QSignalSpy edited(e, &GradientRampEditor::rampEdited);
    // Not editable unless the owner says so
    doubleClick(e, e->handleCenter(GradientRampEditor::kEndHandle));
    QCOMPARE(ends.count(), 0);
    e->setEndsEditable(true);
    doubleClick(e, e->handleCenter(GradientRampEditor::kEndHandle));
    QCOMPARE(ends.count(), 1);
    QCOMPARE(ends[0][0].toInt(), 1);
    QCOMPARE(ends[0][1].value<QColor>(), QColor(Qt::red));
    QVERIFY(!asked_alpha);  // Start/End Color save opaque
    QCOMPARE(e->endColor(), QColor(Qt::red));
    QCOMPARE(edited.count(), 0);
    QCOMPARE(e->ramp(), QString());
  }

  void reverseMirrorsAndSwapsTheEnds() {
    auto e = make(QStringLiteral("0.2 #ff0000ff;0.5 #00ff00ff"));
    QVERIFY(e);
    e->setEndsEditable(true);
    e->setEndColors(Qt::black, Qt::white);
    QSignalSpy reversed_spy(e, &GradientRampEditor::rampReversed);
    QSignalSpy finished(e, &GradientRampEditor::editFinished);
    e->reverseRamp();
    QCOMPARE(reversed_spy.count(), 1);
    QCOMPARE(reversed_spy[0][0].toString(), QStringLiteral("0.5 #00ff00ff;0.8 #ff0000ff"));
    QCOMPARE(e->ramp(), QStringLiteral("0.5 #00ff00ff;0.8 #ff0000ff"));
    QCOMPARE(e->startColor(), QColor(Qt::white));
    QCOMPARE(e->endColor(), QColor(Qt::black));
    QCOMPARE(finished.count(), 0);  // its own signal, not a gesture
  }

  void menuActionsAreOneEditEach() {
    auto e = make();
    QVERIFY(e);
    QSignalSpy finished(e, &GradientRampEditor::editFinished);
    QCOMPARE(e->addStopAt(0.3), 0);
    QCOMPARE(e->addStopAt(0.6), 1);
    QCOMPARE(finished.count(), 2);
    e->removeStop(0);
    QCOMPARE(finished.count(), 3);
    QCOMPARE(finished[2][0].toString(), QStringLiteral("Remove Color Stop"));
    QCOMPARE(parse(e->ramp()).size(), 1);
  }

  void capAtSixteen() {
    QStringList entries;
    for (int i = 1; i <= kMaxStops; i++) entries << QStringLiteral("%1 #ff0000ff").arg(i / 20.0);
    auto e = make(entries.join(';'));
    QVERIFY(e);
    QSignalSpy edited(e, &GradientRampEditor::rampEdited);
    press(e, onBar(*e, 0.97));
    release(e, onBar(*e, 0.97));
    QCOMPARE(edited.count(), 0);
    QCOMPARE(e->addStopAt(0.97), -1);
    QCOMPARE(parse(e->ramp()).size(), kMaxStops);
  }

  void setRampIsIgnoredDuringAGesture() {
    auto e = make(QStringLiteral("0.5 #ff0000ff"));
    QVERIFY(e);
    press(e, e->handleCenter(0));
    move(e, e->handleCenter(0) + QPointF(40, 0));
    const QString dragging = e->ramp();
    e->setRamp(QString());
    QCOMPARE(e->ramp(), dragging);
    release(e, e->handleCenter(0));
    e->setRamp(QString());
    QCOMPARE(e->ramp(), QString());
  }
};

QTEST_MAIN(TestGradientRampEditor)
#include "test_gradientrampeditor.moc"
