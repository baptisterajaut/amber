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

#include <QScopeGuard>
#include <QtMath>
#include <QtTest>

#include <climits>

#include "core/audio.h"
#include "engine/clip.h"
#include "engine/sequence.h"
#include "global/config.h"
#include "project/footage.h"
#include "project/media.h"
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
  // Preview decodes a finished proxy, export always the original; a missing or unset proxy falls back to the original
  void decodeUrlProxyOnlyForPreview() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString proxy = dir.filePath("proxy.mov");
    Footage f;
    f.url = dir.filePath("original.mov");

    QCOMPARE(f.decode_url(false, true), f.url);  // no proxy

    f.proxy = true;  // being generated: flagged, no path yet
    QCOMPARE(f.decode_url(false, true), f.url);

    f.proxy_path = proxy;  // path recorded, file missing
    QCOMPARE(f.decode_url(false, true), f.url);

    QFile file(proxy);
    QVERIFY(file.open(QFile::WriteOnly));
    file.close();
    QCOMPARE(f.decode_url(false, true), proxy);
    QCOMPARE(f.decode_url(true, true), f.url);

    // Use Proxies off: the original, in preview too
    QCOMPARE(f.decode_url(false, false), f.url);
    QCOMPARE(f.decode_url(true, false), f.url);
  }

  // Toggling Use Proxies reconfigures an open clip whose footage has a proxy, so it switches file on its next frame;
  // a clip without a proxy decodes the original either way and stays open
  void useProxiesToggleReconfiguresOpenClip() {
    const bool saved = amber::CurrentConfig.use_proxies;
    const auto restore = qScopeGuard([saved] { amber::CurrentConfig.use_proxies = saved; });
    amber::CurrentConfig.use_proxies = true;

    // Footage with no streams: the cacher thread starts, finds no stream to open and idles until Close()
    auto footage = std::make_shared<Footage>();
    footage->url = "/nonexistent/original.mov";
    Media media;
    media.set_footage(footage);
    Sequence seq;
    Clip c(&seq);
    c.set_track(-1);
    c.set_media(&media, 0);

    c.Open();
    QVERIFY(c.IsOpen());
    QVERIFY(c.OpenedWithProxies());
    amber::CurrentConfig.use_proxies = false;
    QVERIFY(!c.NeedsCacherReconfigure());  // no proxy: nothing to switch

    footage->proxy = true;
    QVERIFY(c.NeedsCacherReconfigure());
    // Close() is a no-op until the cacher thread has finished opening and released state_change_lock
    QVERIFY(QTest::qWaitFor([&c] { return c.state_change_lock.tryLock(); }));  // not QTRY: it re-evaluates
    c.state_change_lock.unlock();
    c.Close(true);
    c.Open();
    QVERIFY(!c.OpenedWithProxies());
    QVERIFY(!c.NeedsCacherReconfigure());
    amber::CurrentConfig.use_proxies = true;
    QVERIFY(c.NeedsCacherReconfigure());
    QVERIFY(QTest::qWaitFor([&c] { return c.state_change_lock.tryLock(); }));  // not QTRY: it re-evaluates
    c.state_change_lock.unlock();
    c.Close(true);
    QVERIFY(!c.IsOpen());
  }

  // A clip left open across the start or end of an export (its close skipped while it was still opening) is
  // reconfigured by compose_sequence(), so it switches between the proxy and the original
  void exportModeChangeReconfiguresOpenClip() {
    Sequence seq;
    Clip c(&seq);
    c.set_track(-1);  // video, no media: opens without a cacher
    audio_rendering = false;
    const auto reset_rendering = qScopeGuard([] { audio_rendering = false; });  // even when a check fails
    c.Open();
    QVERIFY(c.IsOpen());
    QVERIFY(!c.OpenedForExport());
    QVERIFY(!c.NeedsCacherReconfigure());
    audio_rendering = true;
    QVERIFY(c.NeedsCacherReconfigure());
    c.Close(true);
    c.Open();
    QVERIFY(c.OpenedForExport());
    QVERIFY(!c.NeedsCacherReconfigure());
    audio_rendering = false;
    QVERIFY(c.NeedsCacherReconfigure());
    c.Close(true);
  }

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

  // -- Clip::reset_audio -----------------------------------------------------

  void resetAudioSelfNestedSequence() {
    // A sequence holding a clip of itself (corrupt or hand-edited .ove): reset_audio() must return instead of
    // recursing until the stack overflows
    auto seq = std::make_shared<Sequence>();
    Media m;
    m.set_sequence(seq);
    auto clip = std::make_shared<Clip>(seq.get());
    clip->set_track(-1);  // video track: no cacher to reset, only the nesting walk
    clip->set_media(&m, 0);
    seq->clips.append(clip);
    clip->reset_audio();
    seq->clips.clear();  // before m goes out of scope: the clip points at it
  }

  void footageLengthZeroSpeed() {
    Footage f;
    f.length = AV_TIME_BASE;
    f.speed = 0.0;
    QCOMPARE(f.get_length_in_frames(30.0), LONG_MAX);
    f.ready_lock.unlock();
  }

  // -- Sequence::copy ----------------------------------------------------------

  void sequenceCopyKeepsWorkarea() {
    Sequence s;
    s.width = 1280;  // no default initializers on these, and copy() reads them
    s.height = 720;
    s.frame_rate = 25.0;
    s.audio_frequency = 48000;
    s.audio_layout = 3;
    s.using_workarea = true;
    s.workarea_in = 12;
    s.workarea_out = 48;
    SequencePtr copy = s.copy();
    QCOMPARE(copy->using_workarea, true);
    QCOMPARE(copy->workarea_in, 12L);
    QCOMPARE(copy->workarea_out, 48L);
  }
};

QTEST_GUILESS_MAIN(TestEngine)
#include "test_engine.moc"
