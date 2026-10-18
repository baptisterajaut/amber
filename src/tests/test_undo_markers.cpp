#include <QtTest>

#include "core/marker.h"
#include "engine/undo/undo_timeline.h"

namespace {

QVector<Marker> make_markers(int count) {
  QVector<Marker> v;
  for (int i = 0; i < count; i++) {
    Marker m;
    m.frame = i * 10;
    m.name = QString("m%1").arg(i);
    v.append(m);
  }
  return v;
}

}  // namespace

class TestUndoMarkers : public QObject {
  Q_OBJECT
 private slots:
  void moveUndoSurvivesReallocation();
  void editUndoSurvivesReallocation();
};

// The vector reallocates between the move and its undo (an appended marker, later undone):
// the undo must still land on the marker, not on the freed buffer.
void TestUndoMarkers::moveUndoSurvivesReallocation() {
  QVector<Marker> markers = make_markers(2);
  markers.squeeze();  // capacity == size, so the next append reallocates
  MoveMarkerAction move(&markers, 1, 10, 35);
  move.redo();
  QCOMPARE(markers.at(1).frame, 35L);

  const Marker* before = markers.constData();
  AddMarkerAction add(&markers, 100, "late");
  add.redo();
  QVERIFY(markers.constData() != before);  // the append did reallocate
  add.undo();
  QCOMPARE(markers.size(), 2);

  move.undo();
  QCOMPARE(markers.at(1).frame, 10L);
  move.redo();
  QCOMPARE(markers.at(1).frame, 35L);
}

// Marker Properties (EditMarkerAction), then a marker is added (reallocation) and undone: undo/redo of the
// edit must land on the marker, field-exact, not write through a pointer into the freed buffer.
void TestUndoMarkers::editUndoSurvivesReallocation() {
  QVector<Marker> markers = make_markers(2);
  markers.squeeze();
  const Marker original = markers.at(1);
  Marker edited = original;
  edited.name = "edited";
  edited.frame = 42;
  edited.color_label = 3;
  EditMarkerAction edit(&markers, 1, original, edited);
  edit.redo();

  const Marker* before = markers.constData();
  AddMarkerAction add(&markers, 100, "late");
  add.redo();
  QVERIFY(markers.constData() != before);
  add.undo();

  for (int pass = 0; pass < 2; pass++) {
    edit.undo();
    QCOMPARE(markers.at(1).name, QString("m1"));
    QCOMPARE(markers.at(1).frame, 10L);
    QCOMPARE(markers.at(1).color_label, 0);
    edit.redo();
    QCOMPARE(markers.at(1).name, QString("edited"));
    QCOMPARE(markers.at(1).frame, 42L);
    QCOMPARE(markers.at(1).color_label, 3);
  }
  QCOMPARE(markers.at(0).name, QString("m0"));  // neighbour untouched
  QCOMPARE(markers.size(), 2);

  EditMarkerAction stale(&markers, 5, original, edited);  // index went stale: no crash, nothing touched
  stale.redo();
  stale.undo();
  QCOMPARE(markers.size(), 2);
}

QTEST_GUILESS_MAIN(TestUndoMarkers)
#include "test_undo_markers.moc"
