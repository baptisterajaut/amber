#include <QtTest>

#include <cmath>

#include "core/math.h"

class TestMath : public QObject {
  Q_OBJECT
 private slots:
  void testLerp() {
    QCOMPARE(lerp(0, 100, 0.0), 0);
    QCOMPARE(lerp(0, 100, 1.0), 100);
    QCOMPARE(lerp(0, 100, 0.5), 50);
    QCOMPARE(lerp(0, 10, 0.25), 3);  // rounds 2.5 -> 3
  }

  void testDoubleLerp() {
    QCOMPARE(double_lerp(0.0, 1.0, 0.0), 0.0);
    QCOMPARE(double_lerp(0.0, 1.0, 1.0), 1.0);
    QCOMPARE(double_lerp(0.0, 1.0, 0.5), 0.5);
  }

  void testQuadFromT() {
    QCOMPARE(quad_from_t(0.0, 0.5, 1.0, 0.0), 0.0);
    QCOMPARE(quad_from_t(0.0, 0.5, 1.0, 1.0), 1.0);
  }

  void testQuadTFromX() {
    double a = 0.0, b = 0.3, c = 1.0;
    double x = 0.5;
    double t = quad_t_from_x(x, a, b, c);
    double result = quad_from_t(a, b, c, t);
    QVERIFY(qAbs(result - x) < 0.001);
  }

  void testCubicFromT() {
    // t=0 should return first control point
    QCOMPARE(cubic_from_t(0.0, 0.3, 0.7, 1.0, 0.0), 0.0);
    // t=1 should return last control point
    QCOMPARE(cubic_from_t(0.0, 0.3, 0.7, 1.0, 1.0), 1.0);
  }

  void testCubicTFromX() {
    // roundtrip: cubic_from_t(a,b,c,d, cubic_t_from_x(x, a,b,c,d)) ~ x
    double a = 0.0, b = 0.3, c = 0.7, d = 1.0;
    double x = 0.5;
    double t = cubic_t_from_x(x, a, b, c, d);
    double result = cubic_from_t(a, b, c, d, t);
    QVERIFY(qAbs(result - x) < 0.001);
  }

  void testAmplitudeToDb() {
    // 1.0 amplitude = 0 dB
    QVERIFY(qAbs(amplitude_to_db(1.0) - 0.0) < 0.001);
    // roundtrip
    double db = -6.0;
    QVERIFY(qAbs(amplitude_to_db(db_to_amplitude(db)) - db) < 0.001);
  }

  void testDbToAmplitude() {
    // 0 dB = 1.0 amplitude
    QVERIFY(qAbs(db_to_amplitude(0.0) - 1.0) < 0.001);
  }

  void testLerpExtrapolation() {
    // lerp does not clamp — extrapolates linearly outside [0,1]
    QCOMPARE(lerp(0, 100, -0.5), -50);
    QCOMPARE(lerp(0, 100, 1.5), 150);
  }

  void testLerpSameValues() {
    QCOMPARE(lerp(42, 42, 0.0), 42);
    QCOMPARE(lerp(42, 42, 0.5), 42);
    QCOMPARE(lerp(42, 42, 1.0), 42);
  }

  void testDoubleLerpExtrapolation() {
    QVERIFY(qAbs(double_lerp(0.0, 1.0, -0.5) - (-0.5)) < 0.001);
    QVERIFY(qAbs(double_lerp(0.0, 1.0, 2.0) - 2.0) < 0.001);
  }

  void testAmplitudeToDbZero() {
    // log(0) → -infinity
    double result = amplitude_to_db(0.0);
    QVERIFY(std::isinf(result) && result < 0);
  }

  void testDbToAmplitudeVeryLow() {
    // -100 dB → near zero but positive
    double result = db_to_amplitude(-100.0);
    QVERIFY(result > 0.0);
    QVERIFY(result < 0.001);
  }

  void testDbAmplitudeRoundtrip() {
    for (double db : {-20.0, -12.0, -6.0, -3.0, 0.0, 3.0, 6.0}) {
      double amp = db_to_amplitude(db);
      double back = amplitude_to_db(amp);
      QVERIFY2(qAbs(back - db) < 0.001,
               qPrintable(QString("roundtrip failed for %1 dB").arg(db)));
    }
  }

  void testCubicFromTMidpoint() {
    double result = cubic_from_t(0.0, 0.333, 0.666, 1.0, 0.5);
    QVERIFY(qAbs(result - 0.5) < 0.01);
  }

  void testQuadFromTMidpoint() {
    double result = quad_from_t(0.0, 0.5, 1.0, 0.5);
    QVERIFY(qAbs(result - 0.5) < 0.001);
  }
};

QTEST_GUILESS_MAIN(TestMath)
#include "test_math.moc"
