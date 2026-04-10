// Compile-time isolation proof: these headers must compile with
// only Qt Core + stdlib. If they pull in amber code, this fails.
#include "core/selection.h"
#include "core/guide.h"
#include "core/marker.h"
#include "core/keyframe.h"
#include "core/style.h"

#include <QtTest>

class TestCoreHeaders : public QObject {
  Q_OBJECT
 private slots:
  void testSelectionPod() {
    Selection s;
    s.in = 0;
    s.out = 100;
    s.track = 1;
    QCOMPARE(s.in, 0L);
    QCOMPARE(s.out, 100L);
    QCOMPARE(s.track, 1);
  }

  void testMarkerPod() {
    Marker m;
    m.frame = 42;
    m.name = "test";
    QCOMPARE(m.frame, 42L);
    QCOMPARE(m.name, QString("test"));
  }

  void testKeyframePod() {
    EffectKeyframe kf;
    QCOMPARE(kf.pre_handle_x, -40.0);
    QCOMPARE(kf.post_handle_x, 40.0);
    QCOMPARE(kf.pre_handle_y, 0.0);
    QCOMPARE(kf.post_handle_y, 0.0);
  }

  void testGuidePod() {
    Guide g;
    QCOMPARE(g.orientation, Guide::Horizontal);
    QCOMPARE(g.position, 0);
    QCOMPARE(g.mirror, false);
  }

  void testStyleEnum() {
    QCOMPARE(static_cast<int>(amber::styling::kAmberDefaultDark), 0);
    QCOMPARE(static_cast<int>(amber::styling::kAmberDefaultLight), 1);
  }
};

QTEST_GUILESS_MAIN(TestCoreHeaders)
#include "test_core_headers.moc"
