#include <QTemporaryDir>
#include <QtTest>

#include "core/headlessappcontext.h"
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

  // Why the subtitle editor compares the committed text with the cell's before parsing it: in the Milliseconds view at
  // a non-integer rate the frame -> text -> frame round trip is off (h/m/s split at qRound(fps), rebuilt at the real
  // rate). Should the conversion become symmetric, drop the QEXPECT_FAIL.
  void millisecondsViewRoundTripIsAsymmetric() {
    const double rate = 23.976024;
    const QString shown = frame_to_timecode(2400, amber::kTimecodeMilliseconds, rate);
    QCOMPARE(shown, QString("100000"));
    QCOMPARE(frame_to_timecode(2400, amber::kTimecodeMilliseconds, rate), shown);  // the cell text is stable
    QEXPECT_FAIL("", "timecode_to_frame() does not invert frame_to_timecode() in the Milliseconds view", Continue);
    QCOMPARE(timecode_to_frame(shown, amber::kTimecodeMilliseconds, rate), 2400L);
    // Integer rates and the other views round-trip
    QCOMPARE(
        timecode_to_frame(frame_to_timecode(2400, amber::kTimecodeMilliseconds, 24), amber::kTimecodeMilliseconds, 24),
        2400L);
    QCOMPARE(timecode_to_frame(frame_to_timecode(2400, amber::kTimecodeNonDrop, rate), amber::kTimecodeNonDrop, rate),
             2400L);
  }

  void savedTogglesRoundTrip() {
    QTemporaryDir dir;
    const QString path = dir.filePath("config.xml");
    Config saved;
    QVERIFY(!saved.show_frame_rate);  // off by default
    saved.show_frame_rate = true;
    QVERIFY(!saved.show_pixel_sampler);
    saved.show_pixel_sampler = true;
    saved.lock_guides = true;  // was loaded but never saved
    QVERIFY(saved.use_proxies);  // on by default
    saved.use_proxies = false;
    QVERIFY(saved.show_welcome_screen);  // on by default
    saved.show_welcome_screen = false;
    HeadlessAppContext ctx;
    amber::app_ctx = &ctx;  // save() asks it for the project toolbar state
    saved.save(path);
    amber::app_ctx = nullptr;
    Config loaded;
    loaded.load(path);
    QVERIFY(loaded.show_frame_rate);
    QVERIFY(loaded.show_pixel_sampler);
    QVERIFY(loaded.lock_guides);
    QVERIFY(!loaded.use_proxies);
    QVERIFY(!loaded.show_welcome_screen);
  }
};

QTEST_GUILESS_MAIN(TestConfig)
#include "test_config.moc"
