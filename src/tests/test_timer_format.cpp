#include <QtTest>

#include "core/timerformat.h"

using amber::TimerFormat;

class TestTimerFormat : public QObject {
  Q_OBJECT

 private slots:
  void countUpEachFormat_data() {
    QTest::addColumn<double>("seconds");
    QTest::addColumn<int>("format");
    QTest::addColumn<QString>("expected");
    const int mmss = int(TimerFormat::MMSS), hms = int(TimerFormat::HHMMSS), sec = int(TimerFormat::Seconds),
              cc = int(TimerFormat::MMSSHundredths);
    QTest::newRow("0 mmss") << 0.0 << mmss << "00:00";
    QTest::newRow("0 hms") << 0.0 << hms << "00:00:00";
    QTest::newRow("0 sec") << 0.0 << sec << "0";
    QTest::newRow("0 cc") << 0.0 << cc << "00:00.00";
    QTest::newRow("59.9 mmss") << 59.9 << mmss << "00:59";
    QTest::newRow("59.9 hms") << 59.9 << hms << "00:00:59";
    QTest::newRow("59.9 sec") << 59.9 << sec << "59";
    QTest::newRow("59.9 cc") << 59.9 << cc << "00:59.90";
    QTest::newRow("60 mmss") << 60.0 << mmss << "01:00";
    QTest::newRow("60 hms") << 60.0 << hms << "00:01:00";
    QTest::newRow("60 sec") << 60.0 << sec << "60";
    QTest::newRow("60 cc") << 60.0 << cc << "01:00.00";
    QTest::newRow("3725.5 mmss") << 3725.5 << mmss << "62:05";
    QTest::newRow("3725.5 hms") << 3725.5 << hms << "01:02:05";
    QTest::newRow("3725.5 sec") << 3725.5 << sec << "3725";
    QTest::newRow("3725.5 cc") << 3725.5 << cc << "62:05.50";
  }
  void countUpEachFormat() {
    QFETCH(double, seconds);
    QFETCH(int, format);
    QFETCH(QString, expected);
    QCOMPARE(amber::format_timer(seconds, TimerFormat(format), false), expected);
  }

  void countDownRoundsUp() {
    QCOMPARE(amber::format_timer(0.01, TimerFormat::MMSS, true), QString("00:01"));
    QCOMPARE(amber::format_timer(0.0, TimerFormat::MMSS, true), QString("00:00"));
    QCOMPARE(amber::format_timer(59.1, TimerFormat::MMSS, true), QString("01:00"));
  }

  void floatErrorAtBoundaries() {
    QCOMPARE(amber::format_timer(3 - 1e-12, TimerFormat::MMSS, false), QString("00:03"));
    QCOMPARE(amber::format_timer(2 + 1e-12, TimerFormat::MMSS, true), QString("00:02"));
    // 59.99 is 5998.999... hundredths in binary
    QCOMPARE(amber::format_timer(59.99, TimerFormat::MMSSHundredths, false), QString("00:59.99"));
    // 1 frame at 30 fps, summed: 30 * (1/30) isn't exactly 1
    double s = 0;
    for (int i = 0; i < 30; i++) s += 1.0 / 30.0;
    QCOMPARE(amber::format_timer(s, TimerFormat::Seconds, false), QString("1"));
    QCOMPARE(amber::format_timer(s, TimerFormat::Seconds, true), QString("1"));
  }

  void negative() {
    // Rounding applies to the signed value, then the sign is drawn before the absolute value
    QCOMPARE(amber::format_timer(-5.5, TimerFormat::MMSS, false), QString("-00:06"));
    QCOMPARE(amber::format_timer(-5.5, TimerFormat::MMSS, true), QString("-00:05"));
    QCOMPARE(amber::format_timer(-0.5, TimerFormat::MMSS, true), QString("00:00"));  // no "-00:00"
    QCOMPARE(amber::format_timer(-61, TimerFormat::Seconds, false), QString("-61"));
    QCOMPARE(amber::format_timer(-3725.5, TimerFormat::HHMMSS, true), QString("-01:02:05"));
    QCOMPARE(amber::format_timer(-1.234, TimerFormat::MMSSHundredths, false), QString("-00:01.24"));
  }

  void minutesBeyond59() {
    QCOMPARE(amber::format_timer(4500, TimerFormat::MMSS, false), QString("75:00"));
    QCOMPARE(amber::format_timer(360000, TimerFormat::HHMMSS, false), QString("100:00:00"));
  }

  void hundredths() {
    QCOMPARE(amber::format_timer(1.234, TimerFormat::MMSSHundredths, false), QString("00:01.23"));
    QCOMPARE(amber::format_timer(1.234, TimerFormat::MMSSHundredths, true), QString("00:01.24"));
    QCOMPARE(amber::format_timer(0.001, TimerFormat::MMSSHundredths, true), QString("00:00.01"));
  }

  void timerSeconds() {
    // 30 fps clip, frames 0..40
    QCOMPARE(amber::timer_seconds(false, 0, 40, 30, 0), 0.0);
    QCOMPARE(amber::timer_seconds(false, 30, 40, 30, 2), 3.0);
    QCOMPARE(amber::timer_seconds(true, 0, 40, 30, 0), 40.0 / 30.0);
    QCOMPARE(amber::timer_seconds(true, 40, 40, 30, 0), 0.0);  // 0 on the last frame
    QCOMPARE(amber::timer_seconds(true, 40, 40, 30, 5), 5.0);
    QCOMPARE(amber::timer_seconds(true, 10, 40, 0, 1), 1.0);  // no frame rate: offset only
  }

  void progressFraction() {
    QCOMPARE(amber::progress_fraction(0, 40), 0.0);
    QCOMPARE(amber::progress_fraction(20, 40), 0.5);
    QCOMPARE(amber::progress_fraction(40, 40), 1.0);
    QCOMPARE(amber::progress_fraction(0, 0), 1.0);  // one-frame clip: full at once
    QCOMPARE(amber::progress_fraction(50, 40), 1.0);
    QCOMPARE(amber::progress_fraction(-3, 40), 0.0);
  }

  void elapsedFrames() {
    // The renderer hands effects (max(0, playhead - timeline_in(true)) + clip_in(true)) / fps
    const double fps = 30;
    // Untrimmed clip, timeline 0..41: playhead 10
    QCOMPARE(amber::clip_elapsed_frames(10 / fps, fps, 0, 40), 10L);
    // Head trimmed by 10 (clip_in 10): the same playhead gives the same elapsed time
    QCOMPARE(amber::clip_elapsed_frames((10 + 10) / fps, fps, 10, 40), 10L);
    // Opening transition of 10 frames on that clip: clip_in(true) = 0, timeline_in(true) = 10 frames earlier.
    // Halfway through the transition (5 frames in), still before the clip's own in point: clamped to 0
    QCOMPARE(amber::clip_elapsed_frames((5 + 0) / fps, fps, 10, 40), 0L);
    // At its own in point the transition cancels out: 0, then counts from there
    QCOMPARE(amber::clip_elapsed_frames((10 + 0) / fps, fps, 10, 40), 0L);
    QCOMPARE(amber::clip_elapsed_frames((20 + 0) / fps, fps, 10, 40), 10L);
    // Past the last frame (closing transition): clamped to the last frame
    QCOMPARE(amber::clip_elapsed_frames(100 / fps, fps, 0, 40), 40L);
    // Non-integer rate: 29.97 fps, frame 1000
    QCOMPARE(amber::clip_elapsed_frames(1000 / 29.97, 29.97, 0, 5000), 1000L);
  }
};

QTEST_GUILESS_MAIN(TestTimerFormat)
#include "test_timer_format.moc"
