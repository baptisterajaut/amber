#include <QTemporaryDir>
#include <QtTest>

#include "global/config.h"

class TestConfig : public QObject {
  Q_OBJECT

  static Config load_from(const QByteArray& xml) {
    QTemporaryDir dir;
    const QString path = dir.filePath("config.xml");
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly)) qFatal("can't write %s", qPrintable(path));
    f.write(xml);
    f.close();
    Config cfg;
    cfg.load(path);
    return cfg;
  }

 private slots:
  // The first setting used to be swallowed when nothing separated it from the root element
  void firstSettingReadWithoutWhitespace() {
    const Config cfg = load_from(
        "<?xml version=\"1.0\"?><Configuration><PreviewResolutionDivider>4</PreviewResolutionDivider>"
        "<ScrubHalfResolution>2</ScrubHalfResolution></Configuration>");
    QCOMPARE(cfg.preview_resolution_divider, 4);
    QCOMPARE(cfg.scrub_half_resolution, 2);
  }

  void indentedFileStillReads() {
    const Config cfg = load_from(
        "<?xml version=\"1.0\"?>\n<Configuration>\n  <PreviewResolutionDivider>2</PreviewResolutionDivider>\n"
        "  <ShowFrei0rEffects>1</ShowFrei0rEffects>\n</Configuration>\n");
    QCOMPARE(cfg.preview_resolution_divider, 2);
    QVERIFY(cfg.show_frei0r_effects);
  }
};

QTEST_GUILESS_MAIN(TestConfig)
#include "test_config.moc"
