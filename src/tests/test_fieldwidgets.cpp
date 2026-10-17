#include <QCheckBox>
#include <QMouseEvent>
#include <QtTest>

#include "effects/effect.h"
#include "effects/effectrow.h"
#include "effects/fields/boolfield.h"
#include "effects/fields/combofield.h"
#include "effects/fields/doublefield.h"
#include "engine/clip.h"
#include "engine/undo/comboaction.h"
#include "engine/undo/undo_effect.h"
#include "engine/undo/undostack.h"
#include "ui/comboboxex.h"
#include "ui/labelslider.h"

// Several clips selected with the same effect: Effect Controls shows one row per field and attaches every clip's
// field to it (EffectUI::AddAdditionalEffect). One edit on that row must be one undo entry covering all of them.
// Effect, EffectRow and Clip come from test_fieldwidgets_stubs.cpp; the fields and the undo commands are the real ones.
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
  void singleClipEntryIsUnchanged();
  void followerStaysWithItsWidget();

 private:
  struct Fields {
    Effect* effect;
    BoolField* b;
    DoubleField* d;
    ComboField* c;
  };

  // The way EffectUI builds a shared row: the first clip's field creates the widget, the second attaches to it
  QWidget* SharedWidget(EffectField* a, EffectField* b);
  Fields MakeEffect(Clip* clip);
  void SendMouse(QWidget* w, QEvent::Type type, Qt::MouseButtons buttons);

  Clip* clip_a_{nullptr};
  Clip* clip_b_{nullptr};
  EffectMeta meta_;
  Fields a_{};
  Fields b_{};
  QList<QWidget*> widgets_;
};

void TestFieldWidgets::initTestCase() {
  // No sequence: EffectField::Now() is 0 and nothing keyframes
  clip_a_ = new Clip(nullptr);
  clip_b_ = new Clip(nullptr);

  meta_.name = "Field Widget Test";
  meta_.internal = -1;
  meta_.type = EFFECT_TYPE_EFFECT;
  meta_.subtype = EFFECT_TYPE_VIDEO;
}

void TestFieldWidgets::cleanupTestCase() {
  delete clip_a_;
  delete clip_b_;
}

TestFieldWidgets::Fields TestFieldWidgets::MakeEffect(Clip* clip) {
  Fields f;
  f.effect = new Effect(clip, &meta_);
  f.b = new BoolField(new EffectRow(f.effect, "Bool"), "bool");
  f.d = new DoubleField(new EffectRow(f.effect, "Double"), "double");
  f.c = new ComboField(new EffectRow(f.effect, "Combo"), "combo");
  f.c->AddItem("A", 0);
  f.c->AddItem("B", 1);
  f.c->AddItem("C", 2);
  return f;
}

void TestFieldWidgets::init() {
  a_ = MakeEffect(clip_a_);
  b_ = MakeEffect(clip_b_);
  amber::UndoStack.clear();
}

void TestFieldWidgets::cleanup() {
  amber::UndoStack.clear();  // its commands point into the fields
  qDeleteAll(widgets_);
  widgets_.clear();
  delete a_.effect;
  delete b_.effect;
}

QWidget* TestFieldWidgets::SharedWidget(EffectField* a, EffectField* b) {
  QWidget* w = a->CreateWidget();
  b->CreateWidget(w);
  widgets_.append(w);
  return w;
}

void TestFieldWidgets::SendMouse(QWidget* w, QEvent::Type type, Qt::MouseButtons buttons) {
  QMouseEvent ev(type, QPointF(2, 2), w->mapToGlobal(QPointF(2, 2)), Qt::LeftButton, buttons, Qt::NoModifier);
  QCoreApplication::sendEvent(w, &ev);
}

void TestFieldWidgets::boolToggleIsOneEntry() {
  QCheckBox* cb = static_cast<QCheckBox*>(SharedWidget(a_.b, b_.b));
  QVERIFY(!a_.b->GetBoolAt(0) && !b_.b->GetBoolAt(0));

  cb->toggle();
  QCOMPARE(amber::UndoStack.count(), 1);
  QCOMPARE(amber::UndoStack.text(0), QStringLiteral("Change Value"));
  QVERIFY(a_.b->GetBoolAt(0) && b_.b->GetBoolAt(0));

  amber::UndoStack.undo();
  QVERIFY(!a_.b->GetBoolAt(0) && !b_.b->GetBoolAt(0));
  amber::UndoStack.redo();
  QVERIFY(a_.b->GetBoolAt(0) && b_.b->GetBoolAt(0));
}

void TestFieldWidgets::doubleTypedValueIsOneEntry() {
  LabelSlider* ls = static_cast<LabelSlider*>(SharedWidget(a_.d, b_.d));

  // What LabelSlider::ShowDialog() does with an accepted value
  ls->SetValue(5.0);
  emit ls->valueChanged(5.0);
  QCOMPARE(amber::UndoStack.count(), 1);
  QCOMPARE(amber::UndoStack.text(0), QStringLiteral("Change Value"));
  QCOMPARE(a_.d->GetDoubleAt(0), 5.0);
  QCOMPARE(b_.d->GetDoubleAt(0), 5.0);

  amber::UndoStack.undo();
  QCOMPARE(a_.d->GetDoubleAt(0), 0.0);
  QCOMPARE(b_.d->GetDoubleAt(0), 0.0);
  amber::UndoStack.redo();
  QCOMPARE(a_.d->GetDoubleAt(0), 5.0);
  QCOMPARE(b_.d->GetDoubleAt(0), 5.0);
}

void TestFieldWidgets::doubleDragIsOneEntry() {
  LabelSlider* ls = static_cast<LabelSlider*>(SharedWidget(a_.d, b_.d));

  SendMouse(ls, QEvent::MouseButtonPress, Qt::LeftButton);
  SendMouse(ls, QEvent::MouseMove, Qt::LeftButton);  // the first move turns the press into a drag
  QVERIFY(ls->IsDragging());

  // Later moves, whatever the cursor did: the entry stays open until release
  for (double v : {1.0, 2.0, 3.0}) {
    ls->SetValue(v);
    emit ls->valueChanged(v);
  }
  QCOMPARE(amber::UndoStack.count(), 0);
  QCOMPARE(a_.d->GetDoubleAt(0), 3.0);
  QCOMPARE(b_.d->GetDoubleAt(0), 3.0);

  SendMouse(ls, QEvent::MouseButtonRelease, Qt::NoButton);
  QVERIFY(!ls->IsDragging());
  QCOMPARE(amber::UndoStack.count(), 1);
  QCOMPARE(amber::UndoStack.text(0), QStringLiteral("Change Value"));

  amber::UndoStack.undo();
  QCOMPARE(a_.d->GetDoubleAt(0), 0.0);
  QCOMPARE(b_.d->GetDoubleAt(0), 0.0);
  amber::UndoStack.redo();
  QCOMPARE(a_.d->GetDoubleAt(0), 3.0);
  QCOMPARE(b_.d->GetDoubleAt(0), 3.0);
}

void TestFieldWidgets::doubleUnchangedFieldStaysOut() {
  LabelSlider* ls = static_cast<LabelSlider*>(SharedWidget(a_.d, b_.d));
  a_.d->SetValueAt(0, 5.0);

  // Only the second clip's field changes: one entry, holding that field alone
  ls->SetValue(5.0);
  emit ls->valueChanged(5.0);
  QCOMPARE(amber::UndoStack.count(), 1);
  QVERIFY(dynamic_cast<const KeyframeDataChange*>(amber::UndoStack.command(0)) != nullptr);
  QCOMPARE(b_.d->GetDoubleAt(0), 5.0);

  amber::UndoStack.undo();
  QCOMPARE(a_.d->GetDoubleAt(0), 5.0);
  QCOMPARE(b_.d->GetDoubleAt(0), 0.0);
}

void TestFieldWidgets::doubleSameValueAddsNoEntry() {
  LabelSlider* ls = static_cast<LabelSlider*>(SharedWidget(a_.d, b_.d));
  a_.d->SetValueAt(0, 5.0);
  b_.d->SetValueAt(0, 5.0);

  ls->SetValue(5.0);
  emit ls->valueChanged(5.0);
  QCOMPARE(amber::UndoStack.count(), 0);
}

void TestFieldWidgets::comboPickIsOneEntry() {
  ComboBoxEx* cb = static_cast<ComboBoxEx*>(SharedWidget(a_.c, b_.c));
  a_.c->SetValueAt(0, 0);
  b_.c->SetValueAt(0, 0);

  // What a user pick does: the index moves, then activated()
  cb->setCurrentIndex(2);
  emit cb->activated(2);
  QCOMPARE(amber::UndoStack.count(), 1);
  QCOMPARE(a_.c->GetValueAt(0).toInt(), 2);
  QCOMPARE(b_.c->GetValueAt(0).toInt(), 2);

  amber::UndoStack.undo();
  QCOMPARE(a_.c->GetValueAt(0).toInt(), 0);
  QCOMPARE(b_.c->GetValueAt(0).toInt(), 0);
  amber::UndoStack.redo();
  QCOMPARE(a_.c->GetValueAt(0).toInt(), 2);
  QCOMPARE(b_.c->GetValueAt(0).toInt(), 2);
}

void TestFieldWidgets::singleClipEntryIsUnchanged() {
  // No attached field: the field pushes its own KeyframeDataChange, as before
  QCheckBox* cb = static_cast<QCheckBox*>(a_.b->CreateWidget());
  widgets_.append(cb);

  cb->toggle();
  QCOMPARE(amber::UndoStack.count(), 1);
  QCOMPARE(amber::UndoStack.text(0), QStringLiteral("Change Value"));
  QVERIFY(dynamic_cast<const KeyframeDataChange*>(amber::UndoStack.command(0)) != nullptr);
  QVERIFY(a_.b->GetBoolAt(0));
  QVERIFY(!b_.b->GetBoolAt(0));
}

void TestFieldWidgets::followerStaysWithItsWidget() {
  // Effect Controls rebuilt for the first clip alone: the field it attached before must not follow the new widget
  SharedWidget(a_.b, b_.b);
  QCheckBox* cb = static_cast<QCheckBox*>(a_.b->CreateWidget());
  widgets_.append(cb);

  cb->toggle();
  QCOMPARE(amber::UndoStack.count(), 1);
  QVERIFY(dynamic_cast<const KeyframeDataChange*>(amber::UndoStack.command(0)) != nullptr);
  QVERIFY(a_.b->GetBoolAt(0));
  QVERIFY(!b_.b->GetBoolAt(0));
}

QTEST_MAIN(TestFieldWidgets)
#include "test_fieldwidgets.moc"
