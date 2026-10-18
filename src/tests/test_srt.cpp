#include <QtTest>

#include "effects/internal/srtparser.h"

class TestSrt : public QObject {
  Q_OBJECT
 private slots:
  void testBasicParse();
  void testHtmlTags();
  void testUnsupportedTagsStripped();
  void testLiteralAngleBracketsKept();
  void testAssOverridesStripped();
  void testCueTextToHtml();
  void testMultiLineCue();
  void testMalformedCueSkipped();
  void testEmptyFile();
  void testWindowsLineEndings();
  void testMissingBlankLines();
  void testBom();
  void testUtf8Accents();
  void testWindows1252Fallback();
  void testUtf16Bom();
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

// Only markup is stripped: a '<' or '>' that is text stays, and the renderer gets it escaped
void TestSrt::testLiteralAngleBracketsKept() {
  QTemporaryFile f;
  QVERIFY(f.open());
  f.write(
      "1\n00:00:01,000 --> 00:00:02,000\nif a < b and c > d\n<3 you > all\n\n"
      "2\n00:00:03,000 --> 00:00:04,000\n<s>struck</s> <FONT COLOR=\"red\">red</FONT> <I>it</I>\n\n");
  f.flush();

  auto result = parse_srt(f.fileName());
  QCOMPARE(result.cues.size(), 2);
  QCOMPARE(result.cues[0].text, QString("if a < b and c > d<br><3 you > all"));
  QCOMPARE(result.cues[1].text, QString("<s>struck</s> red <I>it</I>"));
}

void TestSrt::testAssOverridesStripped() {
  QTemporaryFile f;
  QVERIFY(f.open());
  f.write("1\n00:00:01,000 --> 00:00:02,000\n{\\an8}Top\n{\\i1}slanted{\\i0} {not an override}\n\n");
  f.flush();

  auto result = parse_srt(f.fileName());
  QCOMPARE(result.cues.size(), 1);
  QCOMPARE(result.cues[0].text, QString("Top<br>slanted {not an override}"));
}

void TestSrt::testCueTextToHtml() {
  QCOMPARE(cue_text_to_html("if a < b and c > d"), QString("if a &lt; b and c &gt; d"));
  QCOMPARE(cue_text_to_html("<i>x</i><br>y <3<BR/><B>z</B> <s>s</s>"),
           QString("<i>x</i><br>y &lt;3<BR/><B>z</B> <s>s</s>"));
  QCOMPARE(cue_text_to_html("<span>s</span>"), QString("&lt;span&gt;s&lt;/span&gt;"));
  QCOMPARE(cue_text_to_html(""), QString());
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

void TestSrt::testUtf8Accents() {
  QTemporaryFile f;
  QVERIFY(f.open());
  f.write("1\n00:00:01,000 --> 00:00:02,000\n\xC3\xA9t\xC3\xA9 \xE2\x82\xAC\n\n");  // "été €" in UTF-8
  f.flush();

  auto result = parse_srt(f.fileName());
  QCOMPARE(result.cues.size(), 1);
  QCOMPARE(result.cues[0].text, QString::fromUtf8("\xC3\xA9t\xC3\xA9 \xE2\x82\xAC"));
}

// Not valid UTF-8: decoded as Windows-1252 instead of turning every accented letter into U+FFFD
void TestSrt::testWindows1252Fallback() {
  QTemporaryFile f;
  QVERIFY(f.open());
  f.write("1\n00:00:01,000 --> 00:00:02,000\n\xE9t\xE9 \xE0 No\xEBl\n\n");  // "été à Noël" in Windows-1252
  f.flush();

  auto result = parse_srt(f.fileName());
  QCOMPARE(result.cues.size(), 1);
  QCOMPARE(result.cues[0].text, QString::fromUtf8("\xC3\xA9t\xC3\xA9 \xC3\xA0 No\xC3\xABl"));

  // 0x80 is the euro sign in Windows-1252 (U+0080, a control char, in Latin-1)
  if (QStringDecoder("Windows-1252").isValid()) {
    QCOMPARE(decode_srt_bytes("\x80 \xE9"), QString::fromUtf8("\xE2\x82\xAC \xC3\xA9"));
  } else {
    QCOMPARE(decode_srt_bytes("\x80 \xE9"), QString::fromUtf8("\xC2\x80 \xC3\xA9"));
  }

  // The only non-UTF-8 byte is the file's last one (no final newline): a truncated UTF-8 sequence, not valid UTF-8
  QCOMPARE(decode_srt_bytes("1\n00:00:01,000 --> 00:00:02,000\nCaf\xE9"),
           QString::fromUtf8("1\n00:00:01,000 --> 00:00:02,000\nCaf\xC3\xA9"));
}

void TestSrt::testUtf16Bom() {
  QString srt = QString::fromUtf8("1\n00:00:01,000 --> 00:00:02,000\n\xC3\xA9t\xC3\xA9\n\n");
  QByteArray bytes = QStringEncoder(QStringConverter::Utf16LE, QStringConverter::Flag::WriteBom).encode(srt);
  QTemporaryFile f;
  QVERIFY(f.open());
  f.write(bytes);
  f.flush();

  auto result = parse_srt(f.fileName());
  QCOMPARE(result.cues.size(), 1);
  QCOMPARE(result.cues[0].text, QString::fromUtf8("\xC3\xA9t\xC3\xA9"));
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
