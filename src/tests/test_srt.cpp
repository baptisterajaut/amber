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
  void testWriteFormat();
  void testWriteMultiLine();
  void testWriteOffset();
  void testRoundTrip();
  void testSortStable();
  void testShift();
  void testShiftClampsAtZero();
  void testValidation();
  void testTextLines();
  void testBlankLinesDropped();
  void testFrameMapping();
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
  auto result = parse_srt_text(
      "1\n00:00:01,000 --> 00:00:02,000\nif a < b and c > d\n<3 you > all\n\n"
      "2\n00:00:03,000 --> 00:00:04,000\n<s>struck</s> <FONT COLOR=\"red\">red</FONT> <I>it</I>\n\n");
  QCOMPARE(result.cues.size(), 2);
  QCOMPARE(result.cues[0].text, QString("if a < b and c > d<br><3 you > all"));
  QCOMPARE(result.cues[1].text, QString("<s>struck</s> red <I>it</I>"));
}

void TestSrt::testAssOverridesStripped() {
  auto result =
      parse_srt_text("1\n00:00:01,000 --> 00:00:02,000\n{\\an8}Top\n{\\i1}slanted{\\i0} {not an override}\n\n");
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
  result = parse_srt_text(decode_srt_bytes("1\n00:00:01,000 --> 00:00:02,000\nCaf\xE9"));
  QCOMPARE(result.cues.size(), 1);
  QCOMPARE(result.cues[0].text, QString::fromUtf8("Caf\xC3\xA9"));
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

void TestSrt::testWriteFormat() {
  QVector<SubtitleCue> cues = {{5000, 8500, "Hello"}, {36000123, 36001000, "Ten hours"}};
  QCOMPARE(write_srt(cues),
           QString("1\n00:00:05,000 --> 00:00:08,500\nHello\n\n"
                   "2\n10:00:00,123 --> 10:00:01,000\nTen hours\n\n"));
}

void TestSrt::testWriteMultiLine() {
  QVector<SubtitleCue> cues = {{0, 1000, "Line one<br>Line two<BR/>Line <i>three</i>"}};
  QCOMPARE(write_srt(cues), QString("1\n00:00:00,000 --> 00:00:01,000\nLine one\nLine two\nLine <i>three</i>\n\n"));
}

void TestSrt::testWriteOffset() {
  QVector<SubtitleCue> cues = {{0, 500, "Gone"}, {500, 2000, "Clamped"}, {3000, 4000, "Moved"}};
  // -1000: the first cue ends before 0 and is dropped, the second starts at 0, numbering stays contiguous
  QCOMPARE(write_srt(cues, -1000), QString("1\n00:00:00,000 --> 00:00:01,000\nClamped\n\n"
                                           "2\n00:00:02,000 --> 00:00:03,000\nMoved\n\n"));
  QCOMPARE(write_srt(cues, 61000).section('\n', 1, 1), QString("00:01:01,000 --> 00:01:01,500"));
}

void TestSrt::testRoundTrip() {
  QVector<SubtitleCue> cues = {
      {1000, 2500, "One"}, {3000, 4000, "Two<br>lines"}, {3000, 5000, "<b>Same start</b>"}, {7200000, 7201001, "Late"}};
  SrtParseResult r = parse_srt_text(write_srt(cues));
  QCOMPARE(r.skipped, 0);
  QCOMPARE(r.cues.size(), cues.size());
  for (int i = 0; i < cues.size(); i++) {
    QCOMPARE(r.cues[i].start_ms, cues[i].start_ms);
    QCOMPARE(r.cues[i].end_ms, cues[i].end_ms);
    QCOMPARE(r.cues[i].text, cues[i].text);
  }
}

void TestSrt::testSortStable() {
  QVector<SubtitleCue> cues = {{3000, 4000, "c"}, {1000, 2000, "a1"}, {1000, 1500, "a2"}, {2000, 3000, "b"},
                               {1000, 3000, "a3"}};
  sort_cues(cues);
  QStringList order;
  for (const SubtitleCue& c : cues) order << c.text;
  QCOMPARE(order, QStringList({"a1", "a2", "a3", "b", "c"}));
}

void TestSrt::testShift() {
  QVector<SubtitleCue> cues = {{1000, 2000, "a"}, {5000, 6000, "b"}, {9000, 9500, "c"}};
  QCOMPARE(shift_cues(cues, {1, 2}, 250), qint64(250));
  QCOMPARE(cues[0].start_ms, qint64(1000));
  QCOMPARE(cues[1].start_ms, qint64(5250));
  QCOMPARE(cues[1].end_ms, qint64(6250));
  QCOMPARE(cues[2].end_ms, qint64(9750));
}

void TestSrt::testShiftClampsAtZero() {
  QVector<SubtitleCue> cues = {{1000, 2000, "a"}, {5000, 6000, "b"}};
  // -3000 would put "a" at -2000: every cue moves by -1000 instead, spacing kept
  QCOMPARE(shift_cues(cues, {0, 1}, -3000), qint64(-1000));
  QCOMPARE(cues[0].start_ms, qint64(0));
  QCOMPARE(cues[0].end_ms, qint64(1000));
  QCOMPARE(cues[1].start_ms, qint64(4000));
  // Only the shifted cues limit the delta
  QCOMPARE(shift_cues(cues, {1}, -3000), qint64(-3000));
  QCOMPARE(cues[1].start_ms, qint64(1000));
  QCOMPARE(shift_cues(cues, {0, 7}, -5), qint64(0));
}

void TestSrt::testValidation() {
  QVERIFY(cue_times_valid(0, 1));
  QVERIFY(!cue_times_valid(1000, 1000));
  QVERIFY(!cue_times_valid(2000, 1000));
  QVERIFY(!cue_times_valid(-1, 1000));
}

void TestSrt::testTextLines() {
  QCOMPARE(cue_text_to_lines("a<br>b<br/>c<BR />d"), QString("a\nb\nc\nd"));
  QCOMPARE(cue_lines_to_text("a\nb\r\nc"), QString("a<br>b<br>c"));
  QCOMPARE(cue_lines_to_text(cue_text_to_lines("x<br>y")), QString("x<br>y"));
}

void TestSrt::testBlankLinesDropped() {
  // A blank line inside the text would end the SRT block and turn the rest into garbage on re-import
  QVector<SubtitleCue> cues = {{0, 1000, "a<br><br>b<br> <br>"}, {2000, 3000, "next"}};
  QCOMPARE(write_srt(cues), QString("1\n00:00:00,000 --> 00:00:01,000\na\nb\n\n"
                                    "2\n00:00:02,000 --> 00:00:03,000\nnext\n\n"));
  SrtParseResult r = parse_srt_text(write_srt(cues));
  QCOMPARE(r.skipped, 0);
  QCOMPARE(r.cues.size(), 2);
  QCOMPARE(r.cues[0].text, QString("a<br>b"));
  QCOMPARE(r.cues[1].text, QString("next"));

  QCOMPARE(cue_lines_to_text("a\n\nb\n"), QString("a<br>b"));
  QCOMPARE(cue_lines_to_text("a\n  \n indented"), QString("a<br> indented"));
}

void TestSrt::testFrameMapping() {
  // Integer rate: frame boundaries are exact
  QCOMPARE(cue_ms_at_frame(30, 30.0), qint64(1000));
  QCOMPARE(first_frame_at_or_after_ms(1000, 30.0), 30L);
  QCOMPARE(first_frame_at_or_after_ms(1001, 30.0), 31L);
  QCOMPARE(first_frame_at_or_after_ms(0, 30.0), 0L);

  // 29.97: an SRT time rarely falls on a frame; the first frame shown is the first whose time reaches it
  const double fr = 30000.0 / 1001.0;
  for (qint64 ms : {qint64(0), qint64(5000), qint64(5010), qint64(123456), qint64(3600000)}) {
    long f = first_frame_at_or_after_ms(ms, fr);
    QVERIFY(cue_ms_at_frame(f, fr) >= ms);
    QVERIFY(cue_ms_at_frame(f - 1, fr) < ms);
  }
  // A time taken from a frame maps back to that frame
  for (long f : {0L, 1L, 149L, 150L, 107892L}) {
    QCOMPARE(first_frame_at_or_after_ms(cue_ms_at_frame(f, fr), fr), f);
  }
}

QTEST_GUILESS_MAIN(TestSrt)
#include "test_srt.moc"
