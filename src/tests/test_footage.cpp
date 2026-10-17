#include <QtTest>

#include <QTemporaryDir>

#include "project/footage.h"
#include "project/previewgenerator.h"

// footage.cpp's only outside call (Footage::reset()); no generator is ever set here
void PreviewGenerator::cancel() {}

class TestFootage : public QObject {
  Q_OBJECT
 private slots:
  // Preview decodes a finished proxy, export always the original; a missing or unset proxy falls back to the original
  void decodeUrlProxyOnlyForPreview() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString proxy = dir.filePath("proxy.mov");
    Footage f;
    f.url = dir.filePath("original.mov");

    QCOMPARE(f.decode_url(false), f.url);  // no proxy

    f.proxy = true;  // being generated: flagged, no path yet
    QCOMPARE(f.decode_url(false), f.url);

    f.proxy_path = proxy;  // path recorded, file missing
    QCOMPARE(f.decode_url(false), f.url);

    QFile file(proxy);
    QVERIFY(file.open(QFile::WriteOnly));
    file.close();
    QCOMPARE(f.decode_url(false), proxy);
    QCOMPARE(f.decode_url(true), f.url);
  }
};

QTEST_GUILESS_MAIN(TestFootage)
#include "test_footage.moc"
