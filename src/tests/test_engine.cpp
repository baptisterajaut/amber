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

#include <QtMath>
#include <QtTest>

#include <climits>

#include "engine/clip.h"
#include "project/footage.h"
#include "rendering/renderfunctions.h"

// These tests call the real engine functions (amber-engine is linked as an OBJECT library).

// Clip::length() on a real, never-opened Clip (no sequence, no media).
static long clip_length(long timeline_in, long timeline_out) {
  Clip c(nullptr);
  c.set_timeline_in(timeline_in);
  c.set_timeline_out(timeline_out);
  return c.length();
}

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

  // -- Footage::get_length_in_frames ----------------------------------------

  void footageLengthUnknown() {
    Footage f;
    f.length = -1;
    QCOMPARE(f.get_length_in_frames(30.0), 0L);
    f.ready_lock.unlock();  // the Footage constructor locks it
  }

  void footageLengthTypical() {
    Footage f;
    f.length = 2 * AV_TIME_BASE;  // 2 seconds
    QCOMPARE(f.get_length_in_frames(30.0), 60L);
    f.speed = 0.5;  // half speed plays twice as long
    QCOMPARE(f.get_length_in_frames(30.0), 120L);
    f.ready_lock.unlock();
  }

  void footageLengthZeroSpeed() {
    Footage f;
    f.length = AV_TIME_BASE;
    f.speed = 0.0;
    QCOMPARE(f.get_length_in_frames(30.0), LONG_MAX);
    f.ready_lock.unlock();
  }
};

QTEST_GUILESS_MAIN(TestEngine)
#include "test_engine.moc"
