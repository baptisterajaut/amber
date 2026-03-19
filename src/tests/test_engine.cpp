/***

    Amber Video Editor
    Copyright (C) 2019  Olive Team

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.

***/

#include <QtTest>
#include <QtMath>

// Engine types (Clip, Sequence) cannot be compiled in isolation because they
// depend on Cacher (FFmpeg), QRhi, Effects, Config globals, and the full render
// pipeline.  Until the engine is refactored into an OBJECT library or its pure
// calculation functions are extracted into core/, we test engine-equivalent logic
// via standalone reimplementations of the key algorithms.

// ---------- rescale_frame_number ----------
// Canonical impl: rendering/renderfunctions.cpp
//   qRound((double(framenumber) / source_frame_rate) * target_frame_rate)
static long rescale_frame_number(long framenumber, double source_frame_rate, double target_frame_rate) {
  return qRound((double(framenumber) / source_frame_rate) * target_frame_rate);
}

// ---------- clip length ----------
// Canonical impl: Clip::length() = timeline_out_ - timeline_in_
static long clip_length(long timeline_in, long timeline_out) { return timeline_out - timeline_in; }

class TestEngine : public QObject {
  Q_OBJECT
 private slots:
  // -- rescale_frame_number --------------------------------------------------

  void rescaleIdentity() {
    // Same frame rate: no change
    QCOMPARE(rescale_frame_number(100, 30.0, 30.0), 100L);
    QCOMPARE(rescale_frame_number(0, 24.0, 24.0), 0L);
  }

  void rescale30to24() {
    // 30 fps -> 24 fps: frame 30 (= 1 second) -> frame 24
    QCOMPARE(rescale_frame_number(30, 30.0, 24.0), 24L);
    QCOMPARE(rescale_frame_number(60, 30.0, 24.0), 48L);
  }

  void rescale24to30() {
    // 24 fps -> 30 fps: frame 24 (= 1 second) -> frame 30
    QCOMPARE(rescale_frame_number(24, 24.0, 30.0), 30L);
  }

  void rescaleZeroFrame() {
    // Frame 0 always maps to 0 regardless of rate
    QCOMPARE(rescale_frame_number(0, 25.0, 60.0), 0L);
    QCOMPARE(rescale_frame_number(0, 60.0, 25.0), 0L);
  }

  void rescaleRounding() {
    // 1 frame at 30fps = 0.0333s -> at 24fps = 0.8 frames -> rounds to 1
    QCOMPARE(rescale_frame_number(1, 30.0, 24.0), 1L);
    // 1 frame at 24fps = 0.0417s -> at 30fps = 1.25 frames -> rounds to 1
    QCOMPARE(rescale_frame_number(1, 24.0, 30.0), 1L);
  }

  void rescaleLargeFrameNumbers() {
    // 1 hour at 30fps = 108000 frames -> at 24fps = 86400 frames
    QCOMPARE(rescale_frame_number(108000, 30.0, 24.0), 86400L);
  }

  void rescaleNonStandardRates() {
    // 23.976 (NTSC) -> 29.97 (NTSC drop-frame)
    long result = rescale_frame_number(23976, 23.976, 29.97);
    // 23976 frames / 23.976 = 1000 seconds * 29.97 = 29970 frames
    QCOMPARE(result, 29970L);
  }

  // -- clip_length -----------------------------------------------------------

  void clipLengthBasic() {
    QCOMPARE(clip_length(0, 100), 100L);
    QCOMPARE(clip_length(50, 150), 100L);
  }

  void clipLengthZero() { QCOMPARE(clip_length(100, 100), 0L); }

  void clipLengthLargeValues() {
    long in = 1000000;
    long out = 2000000;
    QCOMPARE(clip_length(in, out), 1000000L);
  }
};

QTEST_GUILESS_MAIN(TestEngine)
#include "test_engine.moc"
