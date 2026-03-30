#include <QtTest>

#include "effects/internal/srtparser.h"

class TestSrt : public QObject {
  Q_OBJECT
 private slots:
  void testBasicParse();
  void testHtmlTags();
  void testUnsupportedTagsStripped();
  void testMultiLineCue();
  void testMalformedCueSkipped();
  void testEmptyFile();
  void testWindowsLineEndings();
  void testMissingBlankLines();
  void testBom();
  void testOverlappingCues();
  void testTrailingWhitespace();
};

void TestSrt::testBasicParse() {
  QTemporaryFile f;
  QVERIFY(f.open());
  f.write("1\n00:00:05,000 --> 00:00:08,500\nHello world\n\n"
          "2\n00:00:10,000 --> 00:00:13,000\nSecond cue\n\n");
  f.flush();

  auto result = parse_srt(f.fileName());
  QCOMPARE(result.cues.size(), 2);
  QCOMPARE(result.skipped, 0);

  QCOMPARE(result.cues[0].start_ms, qint64(5000));
  QCOMPARE(result.cues[0].end_ms, qint64(8500));
  QCOMPARE(result.cues[0].text, QString("Hello world"));

  QCOMPARE(result.cues[1].start_ms, qint64(10000));
  QCOMPARE(result.cues[1].end_ms, qint64(13000));
  QCOMPARE(result.cues[1].text, QString("Second cue"));
}

void TestSrt::testHtmlTags() {
  QTemporaryFile f;
  QVERIFY(f.open());
  f.write(
      "1\n00:00:01,000 --> 00:00:02,000\n<i>italic</i> and <b>bold</b> and <u>underline</u>\n\n");
  f.flush();

  auto result = parse_srt(f.fileName());
  QCOMPARE(result.cues.size(), 1);
  QCOMPARE(result.cues[0].text, QString("<i>italic</i> and <b>bold</b> and <u>underline</u>"));
}

void TestSrt::testUnsupportedTagsStripped() {
  QTemporaryFile f;
  QVERIFY(f.open());
  f.write("1\n00:00:01,000 --> 00:00:02,000\n<font color=\"red\">colored</font> text\n\n");
  f.flush();

  auto result = parse_srt(f.fileName());
  QCOMPARE(result.cues.size(), 1);
  QCOMPARE(result.cues[0].text, QString("colored text"));
}

void TestSrt::testMultiLineCue() {
  QTemporaryFile f;
  QVERIFY(f.open());
  f.write("1\n00:00:01,000 --> 00:00:02,000\nLine one\nLine two\n\n");
  f.flush();

  auto result = parse_srt(f.fileName());
  QCOMPARE(result.cues.size(), 1);
  QCOMPARE(result.cues[0].text, QString("Line one<br>Line two"));
}

void TestSrt::testMalformedCueSkipped() {
  QTemporaryFile f;
  QVERIFY(f.open());
  f.write("1\n00:00:01,000 --> 00:00:02,000\nGood cue\n\n"
          "2\nBAD TIMECODE\nBad cue\n\n"
          "3\n00:00:05,000 --> 00:00:06,000\nAnother good\n\n");
  f.flush();

  auto result = parse_srt(f.fileName());
  QCOMPARE(result.cues.size(), 2);
  QCOMPARE(result.skipped, 1);
  QCOMPARE(result.cues[0].text, QString("Good cue"));
  QCOMPARE(result.cues[1].text, QString("Another good"));
}

void TestSrt::testEmptyFile() {
  QTemporaryFile f;
  QVERIFY(f.open());
  f.write("");
  f.flush();

  auto result = parse_srt(f.fileName());
  QCOMPARE(result.cues.size(), 0);
  QCOMPARE(result.skipped, 0);
}

void TestSrt::testWindowsLineEndings() {
  QTemporaryFile f;
  QVERIFY(f.open());
  f.write("1\r\n00:00:01,000 --> 00:00:02,000\r\nWindows text\r\n\r\n");
  f.flush();

  auto result = parse_srt(f.fileName());
  QCOMPARE(result.cues.size(), 1);
  QCOMPARE(result.cues[0].text, QString("Windows text"));
}

void TestSrt::testMissingBlankLines() {
  QTemporaryFile f;
  QVERIFY(f.open());
  f.write("1\n00:00:01,000 --> 00:00:02,000\nFirst\n"
          "2\n00:00:03,000 --> 00:00:04,000\nSecond\n");
  f.flush();

  auto result = parse_srt(f.fileName());
  QCOMPARE(result.cues.size(), 2);
  QCOMPARE(result.cues[0].text, QString("First"));
  QCOMPARE(result.cues[1].text, QString("Second"));
}

void TestSrt::testBom() {
  QTemporaryFile f;
  QVERIFY(f.open());
  f.write("\xEF\xBB\xBF"
          "1\n00:00:01,000 --> 00:00:02,000\nBOM text\n\n");
  f.flush();

  auto result = parse_srt(f.fileName());
  QCOMPARE(result.cues.size(), 1);
  QCOMPARE(result.cues[0].text, QString("BOM text"));
}

void TestSrt::testOverlappingCues() {
  QTemporaryFile f;
  QVERIFY(f.open());
  f.write("1\n00:00:01,000 --> 00:00:05,000\nFirst\n\n"
          "2\n00:00:03,000 --> 00:00:07,000\nSecond\n\n");
  f.flush();

  auto result = parse_srt(f.fileName());
  QCOMPARE(result.cues.size(), 2);
  // Cues should be sorted by start_ms
  QCOMPARE(result.cues[0].start_ms, qint64(1000));
  QCOMPARE(result.cues[1].start_ms, qint64(3000));
}

void TestSrt::testTrailingWhitespace() {
  QTemporaryFile f;
  QVERIFY(f.open());
  f.write("1  \n00:00:01,000 --> 00:00:02,000  \nTrimmed  \n\n");
  f.flush();

  auto result = parse_srt(f.fileName());
  QCOMPARE(result.cues.size(), 1);
  QCOMPARE(result.cues[0].text, QString("Trimmed"));
}

QTEST_GUILESS_MAIN(TestSrt)
#include "test_srt.moc"
