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
};

QTEST_GUILESS_MAIN(TestPath)
#include "test_path.moc"
