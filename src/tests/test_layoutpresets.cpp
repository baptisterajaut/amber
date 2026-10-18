#include <QTemporaryDir>
#include <QtTest>

#include "ui/layoutpresets.h"

namespace {
void write_raw(const QString& path, const QByteArray& bytes) {
  QFile f(path);
  QVERIFY(f.open(QFile::WriteOnly));
  f.write(bytes);
}

amber::LayoutData sample(const QString& name) {
  amber::LayoutData data;
  data.name = name;
  data.state = QByteArray("\x00\x01state", 7);
  data.panels.append({"panel_timeline", QByteArray("heights")});
  return data;
}
}  // namespace

class TestLayoutPresets : public QObject {
  Q_OBJECT
 private slots:
  void testSanitize() {
    QCOMPARE(amber::sanitize_layout_name("My Layout_2-b"), QString("My Layout_2-b"));
    QCOMPARE(amber::sanitize_layout_name("../a/b.c"), QString("___a_b_c"));
    QCOMPARE(amber::sanitize_layout_name(QString::fromUtf8("Étalonnage & co")), QString("_talonnage _ co"));
  }

  void testRoundTrip() {
    QTemporaryDir dir;
    const QString path = dir.filePath("x.xml");
    QVERIFY(amber::write_layout_file(path, sample("Étalonnage & <co>")));

    amber::LayoutData read;
    QVERIFY(amber::read_layout_file(path, &read));
    QCOMPARE(read.name, QString("Étalonnage & <co>"));
    QCOMPARE(read.state, sample(QString()).state);
    QCOMPARE(read.panels.size(), 1);
    QCOMPARE(read.panels.at(0).first, QString("panel_timeline"));
    QCOMPARE(read.panels.at(0).second, QByteArray("heights"));
  }

  void testUnnamedFileFormat() {
    // The exit-time "layout" file: no name attribute, same bytes as the writer that lived in MainWindow::closeEvent()
    QTemporaryDir dir;
    const QString path = dir.filePath("layout");
    QVERIFY(amber::write_layout_file(path, sample(QString())));
    QFile f(path);
    QVERIFY(f.open(QFile::ReadOnly));
    QCOMPARE(f.readAll(), QByteArray("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
                                     "<layout>\n"
                                     "    <panels>AAFzdGF0ZQ==</panels>\n"
                                     "    <panel name=\"panel_timeline\">aGVpZ2h0cw==</panel>\n"
                                     "</layout>\n"));
  }

  void testRejectsGarbage() {
    QTemporaryDir dir;
    amber::LayoutData read;
    QVERIFY(!amber::read_layout_file(dir.filePath("missing.xml"), &read));

    write_raw(dir.filePath("text.xml"), "not xml at all");
    QVERIFY(!amber::read_layout_file(dir.filePath("text.xml"), &read));

    write_raw(dir.filePath("root.xml"), "<project><panels>AAE=</panels></project>");
    QVERIFY(!amber::read_layout_file(dir.filePath("root.xml"), &read));

    write_raw(dir.filePath("nostate.xml"), "<layout name=\"x\"></layout>");
    QVERIFY(!amber::read_layout_file(dir.filePath("nostate.xml"), &read));

    write_raw(dir.filePath("truncated.xml"), "<layout><panels>AAE=</panels><panel name=\"a\">");
    QVERIFY(!amber::read_layout_file(dir.filePath("truncated.xml"), &read));
  }

  void testListSortedByNameWithFallback() {
    QTemporaryDir dir;
    QVERIFY(amber::write_layout_file(dir.filePath("a.xml"), sample("zebra")));
    QVERIFY(amber::write_layout_file(dir.filePath("b.xml"), sample("Apple")));
    // no name attribute: listed under its base name
    QVERIFY(amber::write_layout_file(dir.filePath("mango.xml"), sample(QString())));
    write_raw(dir.filePath("garbage.xml"), "garbage");
    // not *.xml
    QVERIFY(amber::write_layout_file(dir.filePath("other.txt"), sample("other")));

    const QVector<amber::LayoutEntry> list = amber::list_layouts(QDir(dir.path()));
    QCOMPARE(list.size(), 3);
    QCOMPARE(list.at(0).name, QString("Apple"));
    QCOMPARE(list.at(0).path, dir.filePath("b.xml"));
    QCOMPARE(list.at(1).name, QString("mango"));
    QCOMPARE(list.at(2).name, QString("zebra"));
  }

  // Names that sanitize alike used to share one file: saving the second replaced the first
  void testFileNamesDontCollide() {
    QTemporaryDir dir;
    const QDir d(dir.path());
    const QStringList names = {
        QString::fromUtf8("色補正"), QString::fromUtf8("編集"), "Edit!", "Edit?", "Edit", "edit", "A/B", "A_B"};
    QStringList files;
    for (const QString& name : names) {
      const QString path = amber::layout_file_for(d, name);
      QVERIFY2(!QFileInfo::exists(path), qPrintable(name));  // a new name never lands on another layout's file
      QCOMPARE(QFileInfo(path).absolutePath(), d.absolutePath());
      QVERIFY(amber::write_layout_file(path, sample(name)));
      files.append(QFileInfo(path).fileName().toLower());  // apart on case-insensitive file systems too
    }
    files.removeDuplicates();
    QCOMPARE(files.size(), names.size());

    // Stable: the same name finds its file again (Replace), and every layout reads back under its own name
    for (const QString& name : names) {
      amber::LayoutData read;
      QVERIFY(amber::read_layout_file(amber::layout_file_for(d, name), &read));
      QCOMPARE(read.name, name);
    }
    QCOMPARE(amber::list_layouts(d).size(), names.size());
  }

  // A layout saved by an older version as "<sanitized>.xml" still loads, and saving under its name replaces it
  void testOldSchemeFileIsReused() {
    QTemporaryDir dir;
    const QDir d(dir.path());
    QVERIFY(amber::write_layout_file(d.filePath("_talonnage.xml"), sample(QString::fromUtf8("Étalonnage"))));
    QCOMPARE(amber::layout_file_for(d, QString::fromUtf8("Étalonnage")), d.filePath("_talonnage.xml"));
    QVERIFY(amber::layout_file_for(d, QString::fromUtf8("Ètalonnage")) != d.filePath("_talonnage.xml"));
    QCOMPARE(amber::list_layouts(d).size(), 1);
  }

  void testListMissingDir() { QVERIFY(amber::list_layouts(QDir("/nonexistent/amber/layouts")).isEmpty()); }
};

QTEST_MAIN(TestLayoutPresets)
#include "test_layoutpresets.moc"
