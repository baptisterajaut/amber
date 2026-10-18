#include <QtTest>

#include "core/path.h"

class TestPath : public QObject {
  Q_OBJECT
 private slots:
  void testGetAppPathNotEmpty() {
    QVERIFY(!get_app_path().isEmpty());
  }

  void testGetDataPathNotEmpty() {
    QVERIFY(!get_data_path().isEmpty());
  }

  void testGetConfigPathNotEmpty() {
    QVERIFY(!get_config_path().isEmpty());
  }

  void testEffectsPathsNotEmpty() {
    QList<QString> paths = get_effects_paths();
    QVERIFY(!paths.isEmpty());
  }

  void testConfigPathEndsWithAppName() {
    // In test context, AppConfigLocation uses the test binary name, not "amber".
    // Verify the path ends with whatever QCoreApplication::applicationName() is.
    QString path = get_config_path();
    QString appName = QCoreApplication::applicationName();
    QVERIFY2(path.endsWith(appName) || path.endsWith(appName + "/"),
             qPrintable(QString("config path '%1' should end with app name '%2'").arg(path, appName)));
  }

  void testEffectsPathContainsEffects() {
    QList<QString> paths = get_effects_paths();
    bool found = false;
    for (const QString& p : paths) {
      if (p.contains("effects")) {
        found = true;
        break;
      }
    }
    QVERIFY2(found, "at least one effects path should contain 'effects'");
  }

  void testLanguagePathsNotEmpty() {
    QVERIFY(!get_language_paths().isEmpty());
  }

  void testDataDir() {
    QVERIFY(!get_data_dir().path().isEmpty());
  }

  void testConfigDir() {
    QVERIFY(!get_config_dir().path().isEmpty());
  }

  void testFileHashDeterministic() {
    QString hash1 = get_file_hash("/tmp/amber_test_nonexistent_xyz");
    QString hash2 = get_file_hash("/tmp/amber_test_nonexistent_xyz");
    QCOMPARE(hash1, hash2);
  }

  void testSameFilePath() {
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    QDir dir(tmp.path());
    QVERIFY(dir.mkdir("sub"));
    QFile f(dir.filePath("clip.mov"));
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.close();

    QVERIFY(same_file_path(dir.filePath("clip.mov"), dir.filePath("sub/../clip.mov")));
    QVERIFY(same_file_path(dir.filePath("missing.mov"), dir.filePath("./missing.mov")));
    QVERIFY(!same_file_path(dir.filePath("clip.mov"), dir.filePath("sub/clip.mov")));
#ifdef Q_OS_UNIX
    QVERIFY(QFile::link(dir.filePath("clip.mov"), dir.filePath("link.mov")));
    QVERIFY(same_file_path(dir.filePath("link.mov"), dir.filePath("clip.mov")));
#endif
  }

  // A .mov proxied into its own folder must not get its own path (the proxy writer truncates the output first)
  void testProxyPathNeverTheSource() {
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    QDir dir(tmp.path());
    QString source = dir.filePath("clip.mov");
    QFile f(source);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.close();

    // default location (Proxy subfolder): plain name
    QCOMPARE(proxy_file_path(source, dir.filePath("Proxy"), {source}), dir.filePath("Proxy/clip.mov"));
    // custom location = source folder
    QCOMPARE(proxy_file_path(source, tmp.path(), {source}), dir.filePath("clip.proxy.mov"));
    QCOMPARE(proxy_file_path(source, tmp.path() + "/", {}), dir.filePath("clip.proxy.mov"));
    // another source of the same batch is avoided too, and the suffix counts up
    QString other = dir.filePath("clip.mp4");
    QCOMPARE(proxy_file_path(other, tmp.path(), {source, other}), dir.filePath("clip.proxy.mov"));
    QCOMPARE(proxy_file_path(other, tmp.path(), {source, other, dir.filePath("clip.proxy.mov")}),
             dir.filePath("clip.proxy2.mov"));
  }

  // Only the last suffix is dropped: dotted names keep distinct proxies
  void testProxyPathKeepsDottedName() {
    QCOMPARE(proxy_file_path("/media/2024.05.01_trip.mp4", "/media/Proxy", {}),
             QString("/media/Proxy/2024.05.01_trip.mov"));
    QCOMPARE(proxy_file_path("/media/2024.05.02_trip.mp4", "/media/Proxy", {"/media/Proxy/2024.05.01_trip.mov"}),
             QString("/media/Proxy/2024.05.02_trip.mov"));
    // same name, different container: the second one is told apart
    QCOMPARE(proxy_file_path("/media/clip.mov", "/media/Proxy", {"/media/clip.mp4", "/media/Proxy/clip.mov"}),
             QString("/media/Proxy/clip.proxy.mov"));
  }
};

QTEST_GUILESS_MAIN(TestPath)
#include "test_path.moc"
