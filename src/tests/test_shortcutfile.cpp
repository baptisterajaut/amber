#include <QtTest>

#include "core/shortcutfile.h"

class TestShortcutFile : public QObject {
  Q_OBJECT
 private slots:
  void exactMatch();
  void matchOnFirstLine();
  void prefixCollisionPicksExactAction();
  void prefixOnlyIsNotAMatch();
  void midLineOccurrenceIsSkipped();
  void missingAction();
  void crlfValueIsTrimmed();
};

void TestShortcutFile::exactMatch() {
  bool found = false;
  QString ks = amber::find_shortcut_in_file("copy\tCtrl+C\npaste\tCtrl+V", "paste", &found);
  QVERIFY(found);
  QCOMPARE(ks, QString("Ctrl+V"));
}

void TestShortcutFile::matchOnFirstLine() {
  bool found = false;
  QString ks = amber::find_shortcut_in_file("undo\tCtrl+Z\nredo\tCtrl+Y", "undo", &found);
  QVERIFY(found);
  QCOMPARE(ks, QString("Ctrl+Z"));
}

void TestShortcutFile::prefixCollisionPicksExactAction() {
  // "export" is a prefix of "exportframe", which comes first in the file
  bool found = false;
  QString ks = amber::find_shortcut_in_file("exportframe\tCtrl+E\nexport\tCtrl+X", "export", &found);
  QVERIFY(found);
  QCOMPARE(ks, QString("Ctrl+X"));
}

void TestShortcutFile::prefixOnlyIsNotAMatch() {
  bool found = true;
  QString ks = amber::find_shortcut_in_file("exportframe\tCtrl+E", "export", &found);
  QVERIFY(!found);
  QVERIFY(ks.isEmpty());
}

void TestShortcutFile::midLineOccurrenceIsSkipped() {
  // First occurrence of "export" is inside "reexport"; the real line comes after
  bool found = false;
  QString ks = amber::find_shortcut_in_file("reexport\tF5\nexport\tCtrl+X", "export", &found);
  QVERIFY(found);
  QCOMPARE(ks, QString("Ctrl+X"));
}

void TestShortcutFile::missingAction() {
  bool found = true;
  QString ks = amber::find_shortcut_in_file("undo\tCtrl+Z\nredo\tCtrl+Y", "zoomin", &found);
  QVERIFY(!found);
  QVERIFY(ks.isEmpty());
  // found may be null
  QVERIFY(amber::find_shortcut_in_file("undo\tCtrl+Z", "zoomin", nullptr).isEmpty());
}

void TestShortcutFile::crlfValueIsTrimmed() {
  const QByteArray file("undo\tCtrl+Z\r\nredo\tCtrl+Y\r\n");
  bool found = false;
  QCOMPARE(amber::find_shortcut_in_file(file, "undo", &found), QString("Ctrl+Z"));
  QVERIFY(found);
  QCOMPARE(amber::find_shortcut_in_file(file, "redo", &found), QString("Ctrl+Y"));
  QVERIFY(found);
}

QTEST_GUILESS_MAIN(TestShortcutFile)
#include "test_shortcutfile.moc"
