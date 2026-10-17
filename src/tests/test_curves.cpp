#include <QtTest>

#include "core/curves.h"

using namespace amber::curves;

namespace {

// LUT byte of entry i, component c (0 R, 1 G, 2 B, 3 A = luma)
int lut_at(const QByteArray& lut, int i, int c) { return static_cast<uchar>(lut.at(i * 4 + c)); }

Curve curve(std::initializer_list<QPoint> points) { return Curve(points); }

}  // namespace

class TestCurves : public QObject {
  Q_OBJECT

 private slots:
  void identityLutIsIdentity() {
    const QByteArray lut = build_lut(identity());
    QCOMPARE(lut.size(), 256 * 4);
    for (int i = 0; i < 256; i++) {
      for (int c = 0; c < 4; c++) QCOMPARE(lut_at(lut, i, c), i);
    }
  }

  void masterMidPoint() {
    CurveSet s = identity();
    s.channels[kRgb] = curve({{0, 0}, {128, 192}, {255, 255}});
    const QByteArray lut = build_lut(s);
    QCOMPARE(lut_at(lut, 128, 0), 192);
    QCOMPARE(lut_at(lut, 0, 0), 0);
    QCOMPARE(lut_at(lut, 255, 0), 255);
    for (int i = 1; i < 256; i++) {
      for (int c = 0; c < 3; c++) QVERIFY(lut_at(lut, i, c) >= lut_at(lut, i - 1, c));
      QCOMPARE(lut_at(lut, i, 3), i);  // luma untouched by the master curve
    }
    // R, G and B all follow the master
    QCOMPARE(lut_at(lut, 64, 1), lut_at(lut, 64, 0));
    QCOMPARE(lut_at(lut, 64, 2), lut_at(lut, 64, 0));
  }

  void passesThroughEveryPoint() {
    const Curve c = curve({{0, 10}, {40, 90}, {100, 100}, {180, 30}, {255, 240}});
    for (const QPoint& p : c) QCOMPARE(qRound(evaluate(c, p.x())), p.y());
  }

  void stepStaysInRangeAndMonotone() {
    const Curve c = curve({{0, 0}, {100, 0}, {101, 255}, {255, 255}});
    double prev = -1;
    for (int i = 0; i <= 2550; i++) {
      const double v = evaluate(c, i / 10.0);
      QVERIFY2(v >= 0.0 && v <= 255.0, qPrintable(QString("x=%1 v=%2").arg(i / 10.0).arg(v)));
      QVERIFY2(v >= prev - 1e-9, qPrintable(QString("not monotone at x=%1").arg(i / 10.0)));
      prev = v;
    }
  }

  void noOvershootBetweenPoints() {
    // A flat run between two equal points stays flat (no bump), a descending run never rises
    const Curve c = curve({{0, 0}, {60, 200}, {120, 200}, {200, 50}, {255, 40}});
    for (int x = 60; x <= 120; x++) QVERIFY(qAbs(evaluate(c, x) - 200.0) < 1e-9);
    double prev = 300;
    for (int x = 120; x <= 255; x++) {
      const double v = evaluate(c, x);
      QVERIFY(v <= prev + 1e-9);
      prev = v;
    }
  }

  void endpointMovedInward() {
    CurveSet s = identity();
    s.channels[kRgb] = curve({{30, 0}, {255, 255}});
    QByteArray lut = build_lut(s);
    for (int i = 0; i <= 30; i++) QCOMPARE(lut_at(lut, i, 0), 0);
    QVERIFY(lut_at(lut, 31, 0) > 0);
    QCOMPARE(lut_at(lut, 255, 0), 255);

    s.channels[kRgb] = curve({{30, 40}, {255, 255}});
    lut = build_lut(s);
    for (int i = 0; i <= 30; i++) QCOMPARE(lut_at(lut, i, 0), 40);

    // Right endpoint pulled in and down: flat past it
    s.channels[kRgb] = curve({{0, 0}, {200, 220}});
    lut = build_lut(s);
    for (int i = 200; i < 256; i++) QCOMPARE(lut_at(lut, i, 0), 220);
  }

  void channelComposesWithMaster() {
    // R = red(master(v)): master 128 -> 192, red inverted -> R(128) = 255 - 192
    CurveSet s = identity();
    s.channels[kRgb] = curve({{0, 0}, {128, 192}, {255, 255}});
    s.channels[kRed] = curve({{0, 255}, {255, 0}});
    const QByteArray lut = build_lut(s);
    QCOMPARE(lut_at(lut, 128, 0), 63);
    QCOMPARE(lut_at(lut, 128, 1), 192);
    QCOMPARE(lut_at(lut, 128, 2), 192);
  }

  void lumaIndependentOfRgb() {
    CurveSet s = identity();
    s.channels[kLuma] = curve({{0, 0}, {128, 64}, {255, 255}});
    QByteArray lut = build_lut(s);
    QCOMPARE(lut_at(lut, 128, 3), 64);
    for (int i = 0; i < 256; i++) {
      for (int c = 0; c < 3; c++) QCOMPARE(lut_at(lut, i, c), i);
    }
    s = identity();
    s.channels[kRgb] = curve({{0, 0}, {128, 192}, {255, 255}});
    lut = build_lut(s);
    for (int i = 0; i < 256; i++) QCOMPARE(lut_at(lut, i, 3), i);
  }

  void identityString() {
    QCOMPARE(identity_string(),
             QStringLiteral("rgb:0,0 255,255;luma:0,0 255,255;r:0,0 255,255;g:0,0 255,255;b:0,0 255,255"));
    QCOMPARE(serialize(identity()), identity_string());
    QVERIFY(parse(identity_string()) == identity());
  }

  void canonicalRoundTrip() {
    CurveSet s = identity();
    s.channels[kRgb] = curve({{0, 12}, {64, 80}, {200, 190}, {250, 255}});
    s.channels[kBlue] = curve({{10, 0}, {255, 200}});
    const QString str = serialize(s);
    QVERIFY(parse(str) == s);
    QCOMPARE(serialize(parse(str)), str);
  }

  void nonCanonicalInputIsCanonicalised() {
    // Channel order, unsorted and duplicated points, out-of-range values, extra whitespace, a missing channel
    const QString messy =
        QStringLiteral(" b:255,300  10,-5 ;  rgb:255,255 128,192 0,0 128,100;luma:0,0 255,255;g:0,0 255,255 ");
    const CurveSet s = parse(messy);
    QCOMPARE(s.channels[kRgb], curve({{0, 0}, {128, 192}, {255, 255}}));  // first of the duplicated x wins
    QCOMPARE(s.channels[kBlue], curve({{10, 0}, {255, 255}}));
    QCOMPARE(s.channels[kRed], identity_curve());
    QCOMPARE(serialize(s), QStringLiteral("rgb:0,0 128,192 255,255;luma:0,0 255,255;r:0,0 255,255;g:0,0 255,255;"
                                          "b:10,0 255,255"));
    QCOMPARE(canonical(messy), serialize(s));
    QCOMPARE(canonical(serialize(s)), serialize(s));
  }

  void tooFewPointsGetEndpoints() {
    QCOMPARE(parse("rgb:128,192").channels[kRgb], curve({{0, 0}, {128, 192}, {255, 255}}));
    QCOMPARE(parse("rgb:0,40").channels[kRgb], curve({{0, 40}, {255, 255}}));
    QCOMPARE(parse("rgb:255,40").channels[kRgb], curve({{0, 0}, {255, 40}}));
  }

  void malformedIsIdentity() {
    const char* bad[] = {"garbage",
                         "rgb:0,0 255",
                         "rgb:a,b 255,255",
                         "rgb:0,0 255,255;x:0,0 255,255",
                         "rgb 0,0 255,255",
                         "rgb:0,0 255,255;rgb:0,0 255,255",
                         "rgb:",
                         "rgb:0,0,0 255,255"};
    for (const char* s : bad) {
      QVERIFY2(parse(QString::fromLatin1(s)) == identity(), s);
    }
    QVERIFY(parse(QString()) == identity());
  }

  void isIdentity() {
    QVERIFY(is_identity(identity_curve()));
    QVERIFY(!is_identity(curve({{0, 0}, {128, 128}, {255, 255}})));  // same mapping, but not the default point list
    QVERIFY(!is_identity(curve({{0, 0}, {255, 254}})));
  }

  void clampPointRules() {
    const Curve c = curve({{20, 0}, {100, 100}, {150, 160}, {255, 255}});
    // Inner points stay 1 apart from their neighbours
    QCOMPARE(clamp_point(c, 1, QPoint(10, 50)), QPoint(21, 50));
    QCOMPARE(clamp_point(c, 1, QPoint(200, 50)), QPoint(149, 50));
    QCOMPARE(clamp_point(c, 2, QPoint(160, 400)), QPoint(160, 255));
    // Endpoints: vertical is free, horizontal up to the neighbour and back out to the edge
    QCOMPARE(clamp_point(c, 0, QPoint(-30, 300)), QPoint(0, 255));
    QCOMPARE(clamp_point(c, 0, QPoint(140, 10)), QPoint(99, 10));
    QCOMPARE(clamp_point(c, 3, QPoint(120, -5)), QPoint(151, 0));
    QCOMPARE(clamp_point(c, 3, QPoint(300, 70)), QPoint(255, 70));
  }

  void insertOnCurve() {
    Curve c = curve({{0, 0}, {128, 192}, {255, 255}});
    const int y64 = qRound(evaluate(c, 64));
    const int i = insert_point(c, 64);
    QCOMPARE(i, 1);
    QCOMPARE(c.at(1), QPoint(64, y64));
    QCOMPARE(c.size(), 4);
    // An existing x returns that point instead of adding one
    QCOMPARE(insert_point(c, 128), 2);
    QCOMPARE(c.size(), 4);
    // Outside the endpoints the new point becomes the endpoint, on the flat extension
    Curve e = curve({{30, 40}, {255, 255}});
    QCOMPARE(insert_point(e, 10), 0);
    QCOMPARE(e.at(0), QPoint(10, 40));
  }

  void removeOnlyInnerPoints() {
    Curve c = curve({{0, 0}, {128, 192}, {255, 255}});
    QVERIFY(!remove_point(c, 0));
    QVERIFY(!remove_point(c, 2));
    QVERIFY(remove_point(c, 1));
    QCOMPARE(c, identity_curve());
    QVERIFY(!remove_point(c, 5));
  }
};

QTEST_GUILESS_MAIN(TestCurves)
#include "test_curves.moc"
