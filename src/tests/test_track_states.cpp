// Track header states (Sequence::track_states): the getters the render, audio and lock paths read, the undo command
// the header pushes, and the lock helpers.

#include <QtTest>

#include "engine/clip.h"
#include "engine/sequence.h"
#include "engine/undo/undo_timeline.h"
#include "tests/test_appcontext_stub.h"

namespace {

TrackState make_state(bool hidden, bool muted, bool solo, bool locked) {
  TrackState t;
  t.hidden = hidden;
  t.muted = muted;
  t.solo = solo;
  t.locked = locked;
  return t;
}

ClipPtr add_clip(Sequence& s, int track, long in, long out) {
  ClipPtr c = std::make_shared<Clip>(&s);
  c->set_track(track);
  c->set_timeline_in(in);
  c->set_timeline_out(out);
  s.clips.append(c);
  return c;
}

}  // namespace

class TestTrackStates : public QObject {
  Q_OBJECT

 private:
  TestAppContext ctx_;

 private slots:
  void initTestCase() { amber::app_ctx = &ctx_; }
  void cleanupTestCase() { amber::app_ctx = nullptr; }

  void defaultsEverywhere() {
    Sequence s;
    QVERIFY(s.track_states.isEmpty());
    QVERIFY(s.track_visible_in_render(-1));
    QVERIFY(s.track_audible(0));
    QVERIFY(!s.track_locked(-3));
    QVERIFY(!s.track_locked(5));
  }

  void setDefaultStateErasesKey() {
    Sequence s;
    s.set_track_state(-2, make_state(true, false, false, false));
    QCOMPARE(s.track_states.size(), 1);
    s.set_track_state(-2, TrackState());
    QVERIFY(s.track_states.isEmpty());
    s.set_track_state(3, TrackState());  // never stored
    QVERIFY(s.track_states.isEmpty());
  }

  void audibleWithoutSolo() {
    Sequence s;
    s.set_track_state(0, make_state(false, true, false, false));
    QVERIFY(!s.track_audible(0));
    QVERIFY(s.track_audible(1));
  }

  void audibleWithSolo() {
    Sequence s;
    s.set_track_state(1, make_state(false, false, true, false));
    QVERIFY(!s.track_audible(0));  // silenced by A2's solo
    QVERIFY(s.track_audible(1));
    QVERIFY(!s.track_audible(2));
    // mute wins over solo on the same track
    s.set_track_state(1, make_state(false, true, true, false));
    QVERIFY(!s.track_audible(1));
    QVERIFY(!s.track_audible(0));
  }

  void flagsOfTheOtherKindAreIgnored() {
    Sequence s;
    // hidden on an audio track, muted/solo on a video track: no effect
    s.set_track_state(0, make_state(true, false, false, false));
    s.set_track_state(-1, make_state(false, true, true, false));
    QVERIFY(s.track_audible(0));
    QVERIFY(s.track_audible(1));  // a video "solo" doesn't silence audio
    QVERIFY(s.track_visible_in_render(-1));
    QVERIFY(!s.track_visible_in_render(0));  // audio tracks are never rendered as video
    QVERIFY(!s.track_audible(-1));           // nor video tracks as audio
  }

  void copyKeepsStates() {
    Sequence s;
    s.width = 64;  // no default initializers on these, and copy() reads them
    s.height = 64;
    s.frame_rate = 30.0;
    s.audio_frequency = 48000;
    s.audio_layout = 3;
    s.set_track_state(-2, make_state(true, false, false, false));
    s.set_track_state(1, make_state(false, false, true, true));
    SequencePtr copy = s.copy();
    QCOMPARE(copy->track_states.size(), 2);
    QVERIFY(copy->track_state(-2).hidden);
    QVERIFY(copy->track_state(1).solo);
    QVERIFY(copy->track_state(1).locked);
  }

  void firstLockedTrack() {
    Sequence s;
    QVERIFY(!s.first_locked_track({-2, -1, 0, 1}).has_value());
    s.set_track_state(0, make_state(false, false, false, true));
    s.set_track_state(-2, make_state(false, false, false, true));
    QCOMPARE(s.first_locked_track({-1, 0, -2}).value(), 0);
    QCOMPARE(s.first_locked_track({-2, 0}).value(), -2);
    QVERIFY(!s.first_locked_track({-1, 1}).has_value());
    QVERIFY(!s.first_locked_track({}).has_value());
  }

  void lockedClipIndexes() {
    Sequence s;
    add_clip(s, -1, 0, 10);
    add_clip(s, 0, 0, 10);
    s.clips.append(nullptr);  // deleted clip slot
    add_clip(s, 0, 20, 30);
    QVERIFY(s.locked_clip_indexes().isEmpty());
    s.set_track_state(0, make_state(false, false, false, true));
    QCOMPARE(s.locked_clip_indexes(), (QVector<int>{1, 3}));
  }

  // Premiere leaves locked tracks in place on a ripple; the ignore list is taken when the action is built, so a lock
  // change before undo/redo doesn't break the round trip
  void rippleLeavesLockedTracksInPlace() {
    Sequence s;
    ClipPtr v1 = add_clip(s, -1, 20, 30);
    ClipPtr a1 = add_clip(s, 0, 20, 30);
    ClipPtr a2 = add_clip(s, 1, 20, 30);
    ClipPtr before = add_clip(s, 1, 0, 10);  // before the ripple point: never moves
    s.set_track_state(0, make_state(false, false, false, true));

    RippleAction ripple(&s, 15, 5, {});
    s.set_track_state(0, TrackState());  // unlocking after the build changes nothing
    ripple.redo();
    QCOMPARE(v1->timeline_in(), 25L);
    QCOMPARE(a2->timeline_in(), 25L);
    QCOMPARE(a2->timeline_out(), 35L);
    QCOMPARE(a1->timeline_in(), 20L);
    QCOMPARE(a1->timeline_out(), 30L);
    QCOMPARE(before->timeline_in(), 0L);

    ripple.undo();
    QCOMPARE(v1->timeline_in(), 20L);
    QCOMPARE(a2->timeline_in(), 20L);
    QCOMPARE(a2->timeline_out(), 30L);
    QCOMPARE(a1->timeline_in(), 20L);

    // a caller's own ignore list is kept
    s.set_track_state(0, make_state(false, false, false, true));
    RippleAction ripple2(&s, 15, 5, {2});
    ripple2.redo();
    QCOMPARE(v1->timeline_in(), 25L);
    QCOMPARE(a1->timeline_in(), 20L);
    QCOMPARE(a2->timeline_in(), 20L);
    ripple2.undo();
    QCOMPARE(v1->timeline_in(), 20L);
  }

  void undoCommandSwapsMapAndDropsLockedSelections() {
    Sequence s;
    Selection sel;
    sel.in = 0;
    sel.out = 10;
    sel.track = 0;
    s.selections.append(sel);
    sel.track = -1;
    s.selections.append(sel);

    QMap<int, TrackState> old_states = s.track_states;
    QMap<int, TrackState> new_states;
    new_states.insert(0, make_state(false, false, false, true));
    new_states.insert(1, make_state(false, true, false, false));
    SetTrackStateCommand cmd(&s, old_states, new_states);
    cmd.redo();
    QCOMPARE(s.track_states.size(), 2);
    QVERIFY(s.track_locked(0));
    QVERIFY(!s.track_audible(1));
    QCOMPARE(s.selections.size(), 1);  // the A1 selection went with the lock
    QCOMPARE(s.selections.at(0).track, -1);
    cmd.undo();
    QVERIFY(s.track_states.isEmpty());
    QCOMPARE(s.selections.size(), 1);  // selections aren't undone
    cmd.redo();
    QVERIFY(s.track_locked(0));
  }
};

QTEST_GUILESS_MAIN(TestTrackStates)
#include "test_track_states.moc"
