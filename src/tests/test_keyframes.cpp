#include <QtTest>

#include "core/math.h"
#include "effects/effect.h"
#include "effects/effectrow.h"
#include "effects/fields/doublefield.h"
#include "engine/clip.h"
#include "engine/sequence.h"
#include "engine/undo/undo_effect.h"
#include "tests/test_appcontext_stub.h"

// Hosts one DoubleField in a real Sequence -> Clip -> Effect -> EffectRow chain, headless.
// Keyframe times are sequence frames at 30 fps; GetValueAt() takes seconds.
class TestKeyframes : public QObject {
  Q_OBJECT
 private slots:
  void initTestCase();
  void cleanupTestCase();
  void init();

  void bezierThenHoldMatchesBezierThenLinear();
  void singleKeyframeIsConstant();
  void twoLinearKeysMidpoint();
  void holdThenLinearStaysFlat();
  void bezierThenLinearUsesPostHandle();
  void identicalTimesAreDeterministic();
  void memberChangeUndoSurvivesReallocation();
  void memberChangeIgnoresStaleIndex();

 private:
  static EffectKeyframe key(long time, double value, int type);
  double value_at_frame(long frame);

  TestAppContext ctx_;
  SequencePtr seq_;
  ClipPtr clip_;
  EffectMeta meta_;
  EffectPtr effect_;
  DoubleField* field_{nullptr};
};

void TestKeyframes::initTestCase() {
  amber::app_ctx = &ctx_;  // Effect::FieldChanged and ~Effect call into it

  seq_ = std::make_shared<Sequence>();
  seq_->frame_rate = 30.0;
  seq_->width = 64;
  seq_->height = 64;

  clip_ = std::make_shared<Clip>(seq_.get());
  clip_->set_timeline_in(0);
  clip_->set_timeline_out(100);
  clip_->set_clip_in(0);
  clip_->set_track(-1);

  // Bare Effect: empty filename, so the constructor parses no XML and builds no rows
  meta_.name = "Keyframe Test";
  meta_.internal = -1;
  meta_.type = EFFECT_TYPE_EFFECT;
  meta_.subtype = EFFECT_TYPE_VIDEO;
  effect_ = std::make_shared<Effect>(clip_.get(), &meta_);

  EffectRow* row = new EffectRow(effect_.get(), "Value");
  field_ = new DoubleField(row, "value");
  row->SetKeyframingInternal(true);
}

void TestKeyframes::cleanupTestCase() {
  effect_.reset();
  clip_.reset();
  seq_.reset();
  amber::app_ctx = nullptr;
}

void TestKeyframes::init() { field_->keyframes.clear(); }

EffectKeyframe TestKeyframes::key(long time, double value, int type) {
  EffectKeyframe k;
  k.time = time;
  k.data = value;
  k.type = type;
  return k;
}

double TestKeyframes::value_at_frame(long frame) { return field_->GetValueAt(double(frame) / 30.0).toDouble(); }

// B6 regression pin: a Hold key after a Bezier key must not change the Bezier's easing
void TestKeyframes::bezierThenHoldMatchesBezierThenLinear() {
  EffectKeyframe k0 = key(0, 0.0, EFFECT_KEYFRAME_BEZIER);
  k0.post_handle_x = 10;
  k0.post_handle_y = 50;
  field_->keyframes.append(k0);
  field_->keyframes.append(key(30, 100.0, EFFECT_KEYFRAME_LINEAR));
  const double linear_after = value_at_frame(15);

  field_->keyframes[1].type = EFFECT_KEYFRAME_HOLD;
  const double hold_after = value_at_frame(15);

  QCOMPARE(hold_after, linear_after);
}

void TestKeyframes::singleKeyframeIsConstant() {
  field_->keyframes.append(key(10, 5.0, EFFECT_KEYFRAME_LINEAR));
  QCOMPARE(value_at_frame(0), 5.0);
  QCOMPARE(value_at_frame(10), 5.0);
  QCOMPARE(value_at_frame(20), 5.0);
}

void TestKeyframes::twoLinearKeysMidpoint() {
  field_->keyframes.append(key(0, 0.0, EFFECT_KEYFRAME_LINEAR));
  field_->keyframes.append(key(30, 100.0, EFFECT_KEYFRAME_LINEAR));
  QCOMPARE(value_at_frame(15), 50.0);
}

void TestKeyframes::holdThenLinearStaysFlat() {
  field_->keyframes.append(key(0, 10.0, EFFECT_KEYFRAME_HOLD));
  field_->keyframes.append(key(30, 100.0, EFFECT_KEYFRAME_LINEAR));
  QCOMPARE(value_at_frame(15), 10.0);
  QCOMPARE(value_at_frame(29), 10.0);
  QCOMPARE(value_at_frame(30), 100.0);
}

void TestKeyframes::bezierThenLinearUsesPostHandle() {
  EffectKeyframe k0 = key(0, 0.0, EFFECT_KEYFRAME_BEZIER);
  k0.post_handle_x = 10;
  k0.post_handle_y = 50;
  field_->keyframes.append(k0);
  field_->keyframes.append(key(30, 100.0, EFFECT_KEYFRAME_LINEAR));

  // Quadratic through (0,0) -> handle (10,50) -> (30,100), t solved from x = 15
  const double expected = quad_from_t(0.0, 50.0, 100.0, quad_t_from_x(15.0, 0.0, 10.0, 30.0));
  const double v = value_at_frame(15);
  QVERIFY2(qAbs(v - expected) < 1e-9, qPrintable(QString("got %1, expected %2").arg(v).arg(expected)));
  QVERIFY2(qAbs(v - 50.0) > 1.0, "the handle must pull the curve away from the linear midpoint");
}

void TestKeyframes::identicalTimesAreDeterministic() {
  field_->keyframes.append(key(10, 1.0, EFFECT_KEYFRAME_LINEAR));
  field_->keyframes.append(key(10, 2.0, EFFECT_KEYFRAME_LINEAR));
  field_->keyframes.append(key(20, 3.0, EFFECT_KEYFRAME_LINEAR));

  // The first keyframe listed at a time wins, every time
  QCOMPARE(value_at_frame(10), 1.0);
  QCOMPARE(value_at_frame(10), 1.0);
  QCOMPARE(value_at_frame(0), 1.0);
  QCOMPARE(value_at_frame(15), 2.0);  // lerp(1, 3, 0.5) from the first key at frame 10
}

// A keyframe appended between an edit and its undo reallocates the vector: the undo must still
// land on the edited keyframe (it used to write through a pointer into the freed buffer).
void TestKeyframes::memberChangeUndoSurvivesReallocation() {
  field_->keyframes.append(key(0, 0.0, EFFECT_KEYFRAME_LINEAR));
  field_->keyframes.append(key(30, 100.0, EFFECT_KEYFRAME_LINEAR));
  field_->keyframes.squeeze();  // capacity == size, so the next append reallocates

  KeyframeMemberChange move(field_, 1, KeyframeMemberChange::kTime, qlonglong(30), qlonglong(45));
  KeyframeMemberChange type(field_, 1, KeyframeMemberChange::kType, EFFECT_KEYFRAME_LINEAR, EFFECT_KEYFRAME_BEZIER);
  move.redo();
  type.redo();
  QCOMPARE(field_->keyframes.at(1).time, 45L);
  QCOMPARE(field_->keyframes.at(1).type, int(EFFECT_KEYFRAME_BEZIER));

  // Same sequence as GraphView's click-add: append, then push a KeyframeAdd for it
  const EffectKeyframe* before = field_->keyframes.constData();
  field_->keyframes.append(key(60, 0.0, EFFECT_KEYFRAME_LINEAR));
  QVERIFY(field_->keyframes.constData() != before);  // the append did reallocate
  KeyframeAdd add(field_, 2);
  add.redo();
  add.undo();
  QCOMPARE(field_->keyframes.size(), 2);

  type.undo();
  move.undo();
  QCOMPARE(field_->keyframes.at(1).time, 30L);
  QCOMPARE(field_->keyframes.at(1).type, int(EFFECT_KEYFRAME_LINEAR));
  move.redo();
  QCOMPARE(field_->keyframes.at(1).time, 45L);
}

void TestKeyframes::memberChangeIgnoresStaleIndex() {
  field_->keyframes.append(key(0, 0.0, EFFECT_KEYFRAME_LINEAR));
  KeyframeMemberChange stale(field_, 3, KeyframeMemberChange::kTime, qlonglong(0), qlonglong(10));
  QTest::ignoreMessage(QtWarningMsg, "KeyframeMemberChange: keyframe index out of range");
  stale.redo();
  QCOMPARE(field_->keyframes.size(), 1);
  QCOMPARE(field_->keyframes.at(0).time, 0L);
}

QTEST_GUILESS_MAIN(TestKeyframes)
#include "test_keyframes.moc"
