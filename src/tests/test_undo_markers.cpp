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

bool same_markers(const QVector<Marker>& a, const QVector<Marker>& b) {
  if (a.size() != b.size()) return false;
  for (int i = 0; i < a.size(); i++) {
    if (a.at(i).frame != b.at(i).frame || a.at(i).name != b.at(i).name) return false;
  }
  return true;
}

}  // namespace

class TestUndoMarkers : public QObject {
  Q_OBJECT
 private slots:
  void addAtEmptyTimeAppends();
  void addAtExistingTimeRenames();
  void moveRedoUndo();
  void deleteIsIndexOrderIndependent();
};

void TestUndoMarkers::addAtEmptyTimeAppends() {
  QVector<Marker> markers = make_markers(1);  // m0 at frame 0
  AddMarkerAction action(&markers, 20, "new");
  action.redo();
  QCOMPARE(markers.size(), 2);
  QCOMPARE(markers.last().frame, 20L);
  QCOMPARE(markers.last().name, QString("new"));
  action.undo();
  QCOMPARE(markers.size(), 1);
  QCOMPARE(markers.at(0).name, QString("m0"));
}

void TestUndoMarkers::addAtExistingTimeRenames() {
  QVector<Marker> markers = make_markers(2);  // m0 at 0, m1 at 10
  AddMarkerAction action(&markers, 10, "renamed");
  action.redo();
  QCOMPARE(markers.size(), 2);
  QCOMPARE(markers.at(1).name, QString("renamed"));
  action.undo();
  QCOMPARE(markers.size(), 2);
  QCOMPARE(markers.at(1).name, QString("m1"));
}

void TestUndoMarkers::moveRedoUndo() {
  QVector<Marker> markers = make_markers(2);
  MoveMarkerAction action(&markers[1], 10, 35);
  action.redo();
  QCOMPARE(markers.at(1).frame, 35L);
  action.undo();
  QCOMPARE(markers.at(1).frame, 10L);
  action.redo();
  QCOMPARE(markers.at(1).frame, 35L);
}

void TestUndoMarkers::deleteIsIndexOrderIndependent() {
  const QVector<QVector<int>> orders = {{5, 2}, {2, 5}};
  for (const QVector<int>& order : orders) {
    QVector<Marker> markers = make_markers(7);
    const QVector<Marker> original = markers;

    DeleteMarkerAction action(&markers);
    action.markers = order;

    action.redo();
    QCOMPARE(markers.size(), 5);
    QCOMPARE(markers.at(2).name, QString("m3"));  // m2 and m5 are gone
    QCOMPARE(markers.at(4).name, QString("m6"));
    const QVector<Marker> after_delete = markers;

    action.undo();
    QVERIFY(same_markers(markers, original));

    action.redo();
    QVERIFY(same_markers(markers, after_delete));
    action.undo();
    QVERIFY(same_markers(markers, original));
    action.redo();
    QVERIFY(same_markers(markers, after_delete));
  }
}

QTEST_GUILESS_MAIN(TestUndoMarkers)
#include "test_undo_markers.moc"
