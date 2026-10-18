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
  void frameRectFollowsZoomAndScroll() {
    QCOMPARE(amber::viewer_frame_rect(fitted()), QRectF(0, 0, 960, 540));
    ViewerFrameGeometry g = fitted();
    g.zoom = 2.0;
    QCOMPARE(amber::viewer_frame_rect(g), QRectF(0, 0, 3840, 2160));  // scroll 0: top-left corner in view
    g.x_scroll = 1.0;
    g.y_scroll = 0.25;
    QCOMPARE(amber::viewer_frame_rect(g), QRectF(-2880, -405, 3840, 2160));
    g.sequence = QSize();
    QVERIFY(amber::viewer_frame_rect(g).isEmpty());
  }
  void overlayPointsStayOnThePicture() {
    // Guides, gizmos and the title safe area are drawn at sequence_to_viewer(); their hit tests invert it
    ViewerFrameGeometry g = fitted();
    g.zoom = 2.0;
    g.x_scroll = 1.0;
    g.y_scroll = 1.0;  // scrolled to the bottom-right corner
    QCOMPARE(amber::sequence_to_viewer(g, QPointF(1920, 1080)), QPointF(960, 540));
    QCOMPARE(amber::sequence_to_viewer(g, QPointF(1440, 810)), QPointF(0, 0));
    QCOMPARE(amber::viewer_to_sequence(g, QPointF(480, 270)), QPointF(1680, 945));
    g.y_scroll = 0.0;  // top: the frame's top row on the widget's, not its bottom one
    QCOMPARE(amber::sequence_to_viewer(g, QPointF(1440, 0)), QPointF(0, 0));
    QCOMPARE(amber::viewer_to_sequence(g, QPointF(960, 540)), QPointF(1920, 270));
    // Fitted: plain scale, and off-frame points stay unclamped (a guide dragged off the frame)
    QCOMPARE(amber::sequence_to_viewer(fitted(), QPointF(1920, 1080)), QPointF(960, 540));
    QCOMPARE(amber::viewer_to_sequence(fitted(), QPointF(-10, 600)), QPointF(-20, 1200));
    g.widget = QSizeF();
    QCOMPARE(amber::viewer_to_sequence(g, QPointF(10, 10)), QPointF());
  }
  void handleToleranceIsInWidgetPixels() {
    // A gizmo dot (5 px half box) stays grabbable at any zoom: the old tolerance, 2 * qRound(2.5 / zoom) sequence
    // pixels, was 0 above 500 %
    for (double zoom : {0.25, 0.5, 1.0, 4.0, 8.0, 20.0}) {
      ViewerFrameGeometry g = fitted();
      g.zoom = zoom;
      g.x_scroll = 0.5;
      g.y_scroll = 0.5;
      const QPointF dot(960, 540);
      const QPointF at = amber::sequence_to_viewer(g, dot);
      QVERIFY2(amber::viewer_point_near(g, dot, at, 5), qPrintable(QString::number(zoom)));
      QVERIFY2(amber::viewer_point_near(g, dot, at + QPointF(4, -4), 5), qPrintable(QString::number(zoom)));
      QVERIFY2(!amber::viewer_point_near(g, dot, at + QPointF(7, 0), 5), qPrintable(QString::number(zoom)));
      QVERIFY2(!amber::viewer_point_near(g, dot, at + QPointF(0, -7), 5), qPrintable(QString::number(zoom)));
    }
    ViewerFrameGeometry g = fitted();
    g.sequence = QSize();
    QVERIFY(!amber::viewer_point_near(g, QPointF(), QPointF(), 5));  // no frame: nothing to grab
  }
  void guideBandIsInWidgetPixels() {
    // The guide grab band was max(3, 5 / zoom) sequence pixels: 12 screen pixels each side at 400 %, 30 at 1000 %
    for (double zoom : {0.5, 1.0, 4.0, 10.0}) {
      ViewerFrameGeometry g = fitted();
      g.zoom = zoom;
      g.x_scroll = 0.5;
      g.y_scroll = 0.5;
      const QPointF row = amber::sequence_to_viewer(g, QPointF(960, 540));
      QCOMPARE(amber::viewer_line_distance(g, true, 540, row + QPointF(300, 4)), 4.0);
      QCOMPARE(amber::viewer_line_distance(g, false, 960, row + QPointF(-6, 200)), 6.0);
    }
    ViewerFrameGeometry g = fitted();
    g.widget = QSizeF();
    QVERIFY(qIsInf(amber::viewer_line_distance(g, true, 0, QPointF())));
  }
  void formatPadsToFixedWidth() {
    QString expected = QStringLiteral("X _812  Y _403   R 210  G 180  B _96  A 255");
    expected.replace('_', QChar(0x2007));  // figure space
    QCOMPARE(amber::format_pixel_sample(812, 403, 210, 180, 96, 255), expected);
    QCOMPARE(amber::format_pixel_sample(0, 0, 0, 0, 0, 0).size(),
             amber::format_pixel_sample(1919, 1079, 255, 255, 255, 255).size());
  }
};

QTEST_GUILESS_MAIN(TestPixelSampler)
#include "test_pixel_sampler.moc"
