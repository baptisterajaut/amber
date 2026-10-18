#include <QCheckBox>
#include <QMouseEvent>
#include <QtTest>

#include "effects/effect.h"
#include "effects/effectrow.h"
#include "effects/fields/boolfield.h"
#include "effects/fields/colorfield.h"
#include "effects/fields/combofield.h"
#include "effects/fields/curvefield.h"
#include "effects/fields/doublefield.h"
#include "effects/fields/gradientrampfield.h"
#include "effects/ui/boolfieldwidget.h"
#include "effects/ui/combofieldwidget.h"
#include "effects/ui/curvefieldwidget.h"
#include "effects/ui/doublefieldwidget.h"
#include "effects/ui/gradientrampfieldwidget.h"
#include "engine/clip.h"
#include "engine/sequence.h"
#include "engine/undo/comboaction.h"
#include "engine/undo/undo_effect.h"
#include "engine/undo/undostack.h"
#include "global/global.h"
#include "tests/test_appcontext_stub.h"
#include "ui/comboboxex.h"
#include "ui/curveeditor.h"
#include "ui/gradientrampeditor.h"
#include "ui/labelslider.h"

// ui/menu.cpp (LabelSlider's context menu) calls it; the real one lives in global/global.cpp, which pulls the app
void AmberGlobal::SetNativeStyling(QWidget*) {}

// Several clips selected with the same effect: Effect Controls shows one row per field and attaches every clip's
// field to it (EffectUI::AddAdditionalEffect). One edit on that row must be one undo entry covering all of them.
class TestFieldWidgets : public QObject {
  Q_OBJECT
 private slots:
  void initTestCase();
  void cleanupTestCase();
  void init();
  void cleanup();

  void boolToggleIsOneEntry();
  void doubleTypedValueIsOneEntry();
  void doubleDragIsOneEntry();
  void doubleUnchangedFieldStaysOut();
  void doubleSameValueAddsNoEntry();
  void comboPickIsOneEntry();
  void curveGestureIsOneEntry();
  void rampGestureIsOneEntry();
  void rampEndColorIsOneEntry();
  void rampReverseIsOneEntry();
  void singleClipEntryIsUnchanged();

 private:
  enum Row { kBool, kDouble, kCombo, kCurve, kStart, kEnd, kRamp };

  // The way EffectUI builds a shared row: the first clip's wrapper creates the widget, the second attaches to it
  template <class W>
  QWidget* SharedWidget(Row row);
  EffectField* FieldA(Row row) { return effect_a_->row(row)->Field(0); }
  EffectField* FieldB(Row row) { return effect_b_->row(row)->Field(0); }
  EffectPtr MakeEffect(Clip* clip);
  void SendMouse(QWidget* w, QEvent::Type type, Qt::MouseButtons buttons);

  TestAppContext ctx_;
  SequencePtr seq_;
  ClipPtr clip_a_;
  ClipPtr clip_b_;
  EffectMeta meta_;
  EffectPtr effect_a_;
  EffectPtr effect_b_;
  QObject* wrappers_{nullptr};
  QList<QWidget*> widgets_;
};

void TestFieldWidgets::initTestCase() {
  amber::app_ctx = &ctx_;  // Effect::FieldChanged and ~Effect call into it

  seq_ = std::make_shared<Sequence>();
  seq_->frame_rate = 30.0;
  seq_->width = 64;
  seq_->height = 64;

  for (ClipPtr* c : {&clip_a_, &clip_b_}) {
    *c = std::make_shared<Clip>(seq_.get());
    (*c)->set_timeline_in(0);
    (*c)->set_timeline_out(100);
    (*c)->set_clip_in(0);
    (*c)->set_track(-1);
  }

  // Bare Effect: empty filename, so the constructor parses no XML and builds no rows
  meta_.name = "Field Widget Test";
  meta_.internal = -1;
  meta_.type = EFFECT_TYPE_EFFECT;
  meta_.subtype = EFFECT_TYPE_VIDEO;
}

void TestFieldWidgets::cleanupTestCase() {
  clip_a_.reset();
  clip_b_.reset();
  seq_.reset();
  amber::app_ctx = nullptr;
}

EffectPtr TestFieldWidgets::MakeEffect(Clip* clip) {
  EffectPtr e = std::make_shared<Effect>(clip, &meta_);
  new BoolField(new EffectRow(e.get(), "Bool"), "bool");
  new DoubleField(new EffectRow(e.get(), "Double"), "double");
  ComboField* combo = new ComboField(new EffectRow(e.get(), "Combo"), "combo");
  combo->AddItem("A", 0);
  combo->AddItem("B", 1);
  combo->AddItem("C", 2);
  new CurveField(new EffectRow(e.get(), "Curve"), "curve");
  ColorField* start = new ColorField(new EffectRow(e.get(), "Start"), "start_color");
  start->SetValueAt(0, QColor(Qt::black));
  ColorField* end = new ColorField(new EffectRow(e.get(), "End"), "end_color");
  end->SetValueAt(0, QColor(Qt::white));
  GradientRampField* ramp = new GradientRampField(new EffectRow(e.get(), "Ramp", true, false), "ramp");
  ramp->SetEndFields(start, end);
  return e;
}

void TestFieldWidgets::init() {
  effect_a_ = MakeEffect(clip_a_.get());
  effect_b_ = MakeEffect(clip_b_.get());
  wrappers_ = new QObject();
  amber::UndoStack.clear();
}

void TestFieldWidgets::cleanup() {
  amber::UndoStack.clear();  // its commands point into the fields
  qDeleteAll(widgets_);
  widgets_.clear();
  delete wrappers_;
  effect_a_.reset();
  effect_b_.reset();
}

template <class W>
QWidget* TestFieldWidgets::SharedWidget(Row row) {
  QWidget* w = (new W(FieldA(row), wrappers_))->CreateWidget();
  (new W(FieldB(row), wrappers_))->CreateWidget(w);
  widgets_.append(w);
  return w;
}

void TestFieldWidgets::SendMouse(QWidget* w, QEvent::Type type, Qt::MouseButtons buttons) {
  QMouseEvent ev(type, QPointF(2, 2), w->mapToGlobal(QPointF(2, 2)), Qt::LeftButton, buttons, Qt::NoModifier);
  QCoreApplication::sendEvent(w, &ev);
}

void TestFieldWidgets::boolToggleIsOneEntry() {
  QCheckBox* cb = static_cast<QCheckBox*>(SharedWidget<BoolFieldWidget>(kBool));
  BoolField* a = static_cast<BoolField*>(FieldA(kBool));
  BoolField* b = static_cast<BoolField*>(FieldB(kBool));
  QVERIFY(!a->GetBoolAt(0) && !b->GetBoolAt(0));

  cb->toggle();
  QCOMPARE(amber::UndoStack.count(), 1);
  QCOMPARE(amber::UndoStack.text(0), QStringLiteral("Change Value"));
  QVERIFY(a->GetBoolAt(0) && b->GetBoolAt(0));

  amber::UndoStack.undo();
  QVERIFY(!a->GetBoolAt(0) && !b->GetBoolAt(0));
  amber::UndoStack.redo();
  QVERIFY(a->GetBoolAt(0) && b->GetBoolAt(0));
}

void TestFieldWidgets::doubleTypedValueIsOneEntry() {
  LabelSlider* ls = static_cast<LabelSlider*>(SharedWidget<DoubleFieldWidget>(kDouble));
  DoubleField* a = static_cast<DoubleField*>(FieldA(kDouble));
  DoubleField* b = static_cast<DoubleField*>(FieldB(kDouble));

  // What LabelSlider::ShowDialog() does with an accepted value
  ls->SetValue(5.0);
  emit ls->valueChanged(5.0);
  QCOMPARE(amber::UndoStack.count(), 1);
  QCOMPARE(amber::UndoStack.text(0), QStringLiteral("Change Value"));
  QCOMPARE(a->GetDoubleAt(0), 5.0);
  QCOMPARE(b->GetDoubleAt(0), 5.0);

  amber::UndoStack.undo();
  QCOMPARE(a->GetDoubleAt(0), 0.0);
  QCOMPARE(b->GetDoubleAt(0), 0.0);
  amber::UndoStack.redo();
  QCOMPARE(a->GetDoubleAt(0), 5.0);
  QCOMPARE(b->GetDoubleAt(0), 5.0);
}

void TestFieldWidgets::doubleDragIsOneEntry() {
  LabelSlider* ls = static_cast<LabelSlider*>(SharedWidget<DoubleFieldWidget>(kDouble));
  DoubleField* a = static_cast<DoubleField*>(FieldA(kDouble));
  DoubleField* b = static_cast<DoubleField*>(FieldB(kDouble));

  SendMouse(ls, QEvent::MouseButtonPress, Qt::LeftButton);
  SendMouse(ls, QEvent::MouseMove, Qt::LeftButton);  // the first move turns the press into a drag
  QVERIFY(ls->IsDragging());

  // Later moves, whatever the cursor did: the entry stays open until release
  for (double v : {1.0, 2.0, 3.0}) {
    ls->SetValue(v);
    emit ls->valueChanged(v);
  }
  QCOMPARE(amber::UndoStack.count(), 0);
  QCOMPARE(a->GetDoubleAt(0), 3.0);
  QCOMPARE(b->GetDoubleAt(0), 3.0);

  SendMouse(ls, QEvent::MouseButtonRelease, Qt::NoButton);
  QVERIFY(!ls->IsDragging());
  QCOMPARE(amber::UndoStack.count(), 1);
  QCOMPARE(amber::UndoStack.text(0), QStringLiteral("Change Value"));

  amber::UndoStack.undo();
  QCOMPARE(a->GetDoubleAt(0), 0.0);
  QCOMPARE(b->GetDoubleAt(0), 0.0);
  amber::UndoStack.redo();
  QCOMPARE(a->GetDoubleAt(0), 3.0);
  QCOMPARE(b->GetDoubleAt(0), 3.0);
}

void TestFieldWidgets::doubleUnchangedFieldStaysOut() {
  LabelSlider* ls = static_cast<LabelSlider*>(SharedWidget<DoubleFieldWidget>(kDouble));
  DoubleField* a = static_cast<DoubleField*>(FieldA(kDouble));
  DoubleField* b = static_cast<DoubleField*>(FieldB(kDouble));
  a->SetValueAt(0, 5.0);

  // Only the second clip's field changes: one entry, holding that field alone
  ls->SetValue(5.0);
  emit ls->valueChanged(5.0);
  QCOMPARE(amber::UndoStack.count(), 1);
  QVERIFY(dynamic_cast<const KeyframeDataChange*>(amber::UndoStack.command(0)) != nullptr);
  QCOMPARE(b->GetDoubleAt(0), 5.0);

  amber::UndoStack.undo();
  QCOMPARE(a->GetDoubleAt(0), 5.0);
  QCOMPARE(b->GetDoubleAt(0), 0.0);
}

void TestFieldWidgets::doubleSameValueAddsNoEntry() {
  LabelSlider* ls = static_cast<LabelSlider*>(SharedWidget<DoubleFieldWidget>(kDouble));
  FieldA(kDouble)->SetValueAt(0, 5.0);
  FieldB(kDouble)->SetValueAt(0, 5.0);

  ls->SetValue(5.0);
  emit ls->valueChanged(5.0);
  QCOMPARE(amber::UndoStack.count(), 0);
}

void TestFieldWidgets::comboPickIsOneEntry() {
  ComboBoxEx* cb = static_cast<ComboBoxEx*>(SharedWidget<ComboFieldWidget>(kCombo));
  EffectField* a = FieldA(kCombo);
  EffectField* b = FieldB(kCombo);
  a->SetValueAt(0, 0);
  b->SetValueAt(0, 0);

  // What a user pick does: the index moves, then activated()
  cb->setCurrentIndex(2);
  emit cb->activated(2);
  QCOMPARE(amber::UndoStack.count(), 1);
  QCOMPARE(a->GetValueAt(0).toInt(), 2);
  QCOMPARE(b->GetValueAt(0).toInt(), 2);

  amber::UndoStack.undo();
  QCOMPARE(a->GetValueAt(0).toInt(), 0);
  QCOMPARE(b->GetValueAt(0).toInt(), 0);
  amber::UndoStack.redo();
  QCOMPARE(a->GetValueAt(0).toInt(), 2);
  QCOMPARE(b->GetValueAt(0).toInt(), 2);
}

void TestFieldWidgets::curveGestureIsOneEntry() {
  CurveEditor* editor = static_cast<CurveEditor*>(SharedWidget<CurveFieldWidget>(kCurve));
  EffectField* a = FieldA(kCurve);
  EffectField* b = FieldB(kCurve);
  const QString identity = a->GetValueAt(0).toString();
  const QString edited = QStringLiteral("edited curves");

  emit editor->curvesEdited(edited);
  emit editor->editFinished(QStringLiteral("Change Curve"), true);
  QCOMPARE(amber::UndoStack.count(), 1);
  QCOMPARE(a->GetValueAt(0).toString(), edited);
  QCOMPARE(b->GetValueAt(0).toString(), edited);

  amber::UndoStack.undo();
  QCOMPARE(a->GetValueAt(0).toString(), identity);
  QCOMPARE(b->GetValueAt(0).toString(), identity);
}

void TestFieldWidgets::rampGestureIsOneEntry() {
  GradientRampEditor* editor = static_cast<GradientRampEditor*>(SharedWidget<GradientRampFieldWidget>(kRamp));
  EffectField* a = FieldA(kRamp);
  EffectField* b = FieldB(kRamp);
  const QString edited = QStringLiteral("0.5 #ff0000ff");

  emit editor->rampEdited(QStringLiteral("0.4 #ff0000ff"));
  emit editor->rampEdited(edited);
  emit editor->editFinished(QStringLiteral("Move Color Stop"), true);
  QCOMPARE(amber::UndoStack.count(), 1);
  QCOMPARE(amber::UndoStack.text(0), QStringLiteral("Move Color Stop"));
  QCOMPARE(a->GetValueAt(0).toString(), edited);
  QCOMPARE(b->GetValueAt(0).toString(), edited);

  amber::UndoStack.undo();
  QCOMPARE(a->GetValueAt(0).toString(), QString());
  QCOMPARE(b->GetValueAt(0).toString(), QString());

  // A gesture back where it started: restored, no entry
  emit editor->rampEdited(edited);
  emit editor->editFinished(QStringLiteral("Add Color Stop"), false);
  QCOMPARE(amber::UndoStack.count(), 1);
  QCOMPARE(amber::UndoStack.index(), 0);
  QCOMPARE(a->GetValueAt(0).toString(), QString());
  QCOMPARE(b->GetValueAt(0).toString(), QString());
}

void TestFieldWidgets::rampEndColorIsOneEntry() {
  GradientRampEditor* editor = static_cast<GradientRampEditor*>(SharedWidget<GradientRampFieldWidget>(kRamp));
  emit editor->endColorEdited(1, QColor(Qt::green));
  QCOMPARE(amber::UndoStack.count(), 1);
  QCOMPARE(FieldA(kEnd)->GetValueAt(0).value<QColor>(), QColor(Qt::green));
  QCOMPARE(FieldB(kEnd)->GetValueAt(0).value<QColor>(), QColor(Qt::green));
  QCOMPARE(FieldA(kStart)->GetValueAt(0).value<QColor>(), QColor(Qt::black));
  amber::UndoStack.undo();
  QCOMPARE(FieldA(kEnd)->GetValueAt(0).value<QColor>(), QColor(Qt::white));
  QCOMPARE(FieldB(kEnd)->GetValueAt(0).value<QColor>(), QColor(Qt::white));
}

void TestFieldWidgets::rampReverseIsOneEntry() {
  GradientRampEditor* editor = static_cast<GradientRampEditor*>(SharedWidget<GradientRampFieldWidget>(kRamp));
  FieldB(kStart)->SetValueAt(0, QColor(Qt::red));
  FieldB(kEnd)->SetValueAt(0, QColor(Qt::blue));
  FieldA(kRamp)->SetValueAt(0, QStringLiteral("0.2 #00ff00ff"));
  editor->setRamp(FieldA(kRamp)->GetValueAt(0).toString());
  editor->setEndColors(Qt::black, Qt::white);

  editor->reverseRamp();
  // One entry: both clips' stops, and each clip's own ends swapped
  QCOMPARE(amber::UndoStack.count(), 1);
  QCOMPARE(amber::UndoStack.text(0), QStringLiteral("Reverse Color Ramp"));
  QCOMPARE(FieldA(kRamp)->GetValueAt(0).toString(), QStringLiteral("0.8 #00ff00ff"));
  QCOMPARE(FieldB(kRamp)->GetValueAt(0).toString(), QStringLiteral("0.8 #00ff00ff"));
  QCOMPARE(FieldA(kStart)->GetValueAt(0).value<QColor>(), QColor(Qt::white));
  QCOMPARE(FieldA(kEnd)->GetValueAt(0).value<QColor>(), QColor(Qt::black));
  QCOMPARE(FieldB(kStart)->GetValueAt(0).value<QColor>(), QColor(Qt::blue));
  QCOMPARE(FieldB(kEnd)->GetValueAt(0).value<QColor>(), QColor(Qt::red));

  amber::UndoStack.undo();
  QCOMPARE(FieldA(kRamp)->GetValueAt(0).toString(), QStringLiteral("0.2 #00ff00ff"));
  QCOMPARE(FieldB(kRamp)->GetValueAt(0).toString(), QString());
  QCOMPARE(FieldA(kStart)->GetValueAt(0).value<QColor>(), QColor(Qt::black));
  QCOMPARE(FieldB(kEnd)->GetValueAt(0).value<QColor>(), QColor(Qt::blue));
}

void TestFieldWidgets::singleClipEntryIsUnchanged() {
  // No attached field: the wrapper pushes its own KeyframeDataChange, as before
  QCheckBox* cb = static_cast<QCheckBox*>((new BoolFieldWidget(FieldA(kBool), wrappers_))->CreateWidget());
  widgets_.append(cb);

  cb->toggle();
  QCOMPARE(amber::UndoStack.count(), 1);
  QCOMPARE(amber::UndoStack.text(0), QStringLiteral("Change Value"));
  QVERIFY(dynamic_cast<const KeyframeDataChange*>(amber::UndoStack.command(0)) != nullptr);
  QVERIFY(FieldA(kBool)->GetValueAt(0).toBool());
  QVERIFY(!FieldB(kBool)->GetValueAt(0).toBool());
}

QTEST_MAIN(TestFieldWidgets)
#include "test_fieldwidgets.moc"
