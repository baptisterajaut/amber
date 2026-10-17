#include <QtTest>

#include "core/pixelsampler.h"

using amber::FramePoint;
using amber::ViewerFrameGeometry;

class TestPixelSampler : public QObject {
  Q_OBJECT

  // 1920x1080 sequence fitted in a 960x540 widget, as ViewerContainer::adjust() sets it: zoom = widget / sequence
  static ViewerFrameGeometry fitted() {
    ViewerFrameGeometry g;
    g.widget = QSizeF(960, 540);
    g.zoom = 0.5;
    g.sequence = QSize(1920, 1080);
    return g;
  }

 private slots:
  void centreAtFullAndHalfResolution() {
    FramePoint p;
    QVERIFY(amber::map_viewer_point(fitted(), QPointF(480, 270), QSize(1920, 1080), &p));
    QCOMPARE(p.seq_x, 960);
    QCOMPARE(p.seq_y, 540);
    QCOMPARE(p.buf_x, 960);
    QCOMPARE(p.buf_y, 540);
    // Preview at 1/2: still sequence pixels, half the buffer pixels
    QVERIFY(amber::map_viewer_point(fitted(), QPointF(480, 270), QSize(960, 540), &p));
    QCOMPARE(p.seq_x, 960);
    QCOMPARE(p.seq_y, 540);
    QCOMPARE(p.buf_x, 480);
    QCOMPARE(p.buf_y, 270);
  }
  void edgesClampToLastPixel() {
    FramePoint p;
    QVERIFY(amber::map_viewer_point(fitted(), QPointF(0, 0), QSize(960, 540), &p));
    QCOMPARE(p.seq_x, 0);
    QCOMPARE(p.seq_y, 0);
    QVERIFY(amber::map_viewer_point(fitted(), QPointF(959.75, 539.75), QSize(960, 540), &p));
    QCOMPARE(p.seq_x, 1919);
    QCOMPARE(p.seq_y, 1079);
    QVERIFY(amber::map_viewer_point(fitted(), QPointF(960, 540), QSize(960, 540), &p));  // far edge itself
    QCOMPARE(p.seq_x, 1919);
    QCOMPARE(p.seq_y, 1079);
    QCOMPARE(p.buf_x, 959);
    QCOMPARE(p.buf_y, 539);
  }
  void zoomedFrameFollowsScroll() {
    // 200%: ViewerContainer keeps the widget fitted and render() draws the frame 3840x2160, panned by the scrollbars
    ViewerFrameGeometry g = fitted();
    g.zoom = 2.0;
    g.x_scroll = 0.5;
    g.y_scroll = 0.0;
    FramePoint p;
    QVERIFY(amber::map_viewer_point(g, QPointF(480, 0), QSize(1920, 1080), &p));
    QCOMPARE(p.seq_x, 960);  // centred horizontally
    QCOMPARE(p.seq_y, 0);    // scrolled to the top
    QVERIFY(amber::map_viewer_point(g, QPointF(2, 540), QSize(1920, 1080), &p));
    QCOMPARE(p.seq_x, 721);  // (1440 + 2) / 2
    QCOMPARE(p.seq_y, 270);  // a quarter of the frame is in view
    g.x_scroll = 1.0;
    g.y_scroll = 1.0;
    QVERIFY(amber::map_viewer_point(g, QPointF(959.5, 539.5), QSize(1920, 1080), &p));
    QCOMPARE(p.seq_x, 1919);
    QCOMPARE(p.seq_y, 1079);
  }
  void offFrameIsOutside() {
    // A frame drawn smaller than the widget leaves an empty band, here right and bottom (render() anchors at scroll 0)
    ViewerFrameGeometry g = fitted();
    g.widget = QSizeF(1000, 600);
    FramePoint p;
    QVERIFY(amber::map_viewer_point(g, QPointF(950, 500), QSize(1920, 1080), &p));
    QVERIFY(!amber::map_viewer_point(g, QPointF(990, 300), QSize(1920, 1080), &p));
    QVERIFY(!amber::map_viewer_point(fitted(), QPointF(-1, 10), QSize(1920, 1080), &p));
    QVERIFY(!amber::map_viewer_point(fitted(), QPointF(10, 541), QSize(1920, 1080), &p));
  }
  void emptySizesAreOutside() {
    FramePoint p;
    QVERIFY(!amber::map_viewer_point(fitted(), QPointF(10, 10), QSize(), &p));  // no frame yet
    ViewerFrameGeometry g = fitted();
    g.sequence = QSize();
    QVERIFY(!amber::map_viewer_point(g, QPointF(10, 10), QSize(1920, 1080), &p));
  }
};

QTEST_GUILESS_MAIN(TestPixelSampler)
#include "test_pixel_sampler.moc"
