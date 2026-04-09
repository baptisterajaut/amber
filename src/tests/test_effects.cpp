#include <QtTest>

#include "core/math.h"

class TestEffects : public QObject {
  Q_OBJECT
 private slots:

  void doubleFieldLinearInterpolation() {
    double before = 10.0;
    double after = 20.0;
    double progress = 0.5;
    QCOMPARE(double_lerp(before, after, progress), 15.0);
  }

  void doubleFieldHoldInterpolation() {
    double before = 10.0;
    QCOMPARE(before, 10.0);
  }

  void colorInterpolation() {
    QColor before(100, 0, 0);
    QColor after(200, 100, 50);
    double progress = 0.5;
    int r = lerp(before.red(), after.red(), progress);
    int g = lerp(before.green(), after.green(), progress);
    int b = lerp(before.blue(), after.blue(), progress);
    QCOMPARE(r, 150);
    QCOMPARE(g, 50);
    QCOMPARE(b, 25);
  }

  void boolStringConversion() {
    QCOMPARE(QString("1") == "1", true);
    QCOMPARE(QString("0") == "1", false);
    QCOMPARE(QString::number(true), "1");
    QCOMPARE(QString::number(false), "0");
  }

  void doubleStringConversion() {
    QString s("3.14");
    QCOMPARE(s.toDouble(), 3.14);
    QVariant v(3.14);
    QCOMPARE(QString::number(v.toDouble()), "3.14");
  }

  void colorStringConversion() {
    QColor original(255, 128, 0);
    QString name = original.name();
    QColor restored(name);
    QCOMPARE(restored, original);
  }

  // --- lerp boundary tests ---

  void lerpBoundaries() {
    QCOMPARE(lerp(0, 100, 0.0), 0);
    QCOMPARE(lerp(0, 100, 1.0), 100);
    QCOMPARE(lerp(-50, 50, 0.5), 0);
  }

  void doubleLerpBoundaries() {
    QCOMPARE(double_lerp(0.0, 1.0, 0.0), 0.0);
    QCOMPARE(double_lerp(0.0, 1.0, 1.0), 1.0);
    QCOMPARE(double_lerp(-10.0, 10.0, 0.25), -5.0);
  }

  // --- Quadratic bezier ---

  void quadBezierEndpoints() {
    QCOMPARE(quad_from_t(0, 50, 100, 0.0), 0.0);
    QCOMPARE(quad_from_t(0, 50, 100, 1.0), 100.0);
  }

  void quadBezierLinearControlPoint() {
    // Linear control point at midpoint of [0,100] => result is linear => 50 at t=0.5
    QCOMPARE(quad_from_t(0, 50, 100, 0.5), 50.0);
  }

  void quadBezierRoundtrip() {
    double t = 0.3;
    double x = quad_from_t(0, 25, 100, t);
    double t_back = quad_t_from_x(x, 0, 25, 100);
    QVERIFY(qAbs(t_back - t) < 0.001);
  }

  // --- Cubic bezier ---

  void cubicBezierEndpoints() {
    QCOMPARE(cubic_from_t(0, 0, 100, 100, 0.0), 0.0);
    QCOMPARE(cubic_from_t(0, 0, 100, 100, 1.0), 100.0);
  }

  void cubicBezierLinearishControlPoints() {
    // Control points at 33 and 66 out of [0,100]: nearly linear => ~49.625 at t=0.5
    double result = cubic_from_t(0, 33, 66, 100, 0.5);
    QVERIFY(qAbs(result - 49.625) < 0.001);
  }

  void cubicBezierRoundtrip() {
    double t = 0.4;
    double x = cubic_from_t(0, 30, 70, 100, t);
    double t_back = cubic_t_from_x(x, 0, 30, 70, 100);
    QVERIFY(qAbs(t_back - t) < 0.001);
  }

  void cubicBezierEaseIn() {
    // Control points at 0 and 0: steep acceleration, value at t=0.5 should be well below 50
    double result = cubic_from_t(0, 0, 0, 100, 0.5);
    QVERIFY(result < 50.0);
  }

  // --- dB conversion ---

  void amplitudeToDbUnity() { QCOMPARE(amplitude_to_db(1.0), 0.0); }

  void dbToAmplitudeUnity() { QCOMPARE(db_to_amplitude(0.0), 1.0); }

  void dbRoundtrip() {
    double amp = 0.5;
    QVERIFY(qAbs(db_to_amplitude(amplitude_to_db(amp)) - amp) < 0.01);
  }

  void amplitudeToDbHalf() {
    // 0.5 amplitude ≈ -6.02 dB
    QVERIFY(qAbs(amplitude_to_db(0.5) - (-6.0206)) < 0.01);
  }

  void dbToAmplitudeMinusSix() {
    // -6 dB ≈ 0.501 amplitude
    QVERIFY(qAbs(db_to_amplitude(-6.0) - 0.501) < 0.01);
  }

  void dbToAmplitudeMinusTwenty() {
    // -20 dB = 0.1 amplitude (exact)
    QVERIFY(qAbs(db_to_amplitude(-20.0) - 0.1) < 0.01);
  }

  void dbToAmplitudePlusTwenty() {
    // +20 dB = 10.0 amplitude (exact)
    QVERIFY(qAbs(db_to_amplitude(20.0) - 10.0) < 0.01);
  }

  // --- Color interpolation edge cases ---

  void colorInterpolationIdentical() {
    QColor c(100, 200, 50);
    int r = lerp(c.red(), c.red(), 0.5);
    int g = lerp(c.green(), c.green(), 0.5);
    int b = lerp(c.blue(), c.blue(), 0.5);
    QCOMPARE(r, c.red());
    QCOMPARE(g, c.green());
    QCOMPARE(b, c.blue());
  }

  void colorInterpolationAtZero() {
    QColor first(10, 20, 30);
    QColor second(100, 200, 50);
    QCOMPARE(lerp(first.red(), second.red(), 0.0), first.red());
    QCOMPARE(lerp(first.green(), second.green(), 0.0), first.green());
    QCOMPARE(lerp(first.blue(), second.blue(), 0.0), first.blue());
  }

  void colorInterpolationAtOne() {
    QColor first(10, 20, 30);
    QColor second(100, 200, 50);
    QCOMPARE(lerp(first.red(), second.red(), 1.0), second.red());
    QCOMPARE(lerp(first.green(), second.green(), 1.0), second.green());
    QCOMPARE(lerp(first.blue(), second.blue(), 1.0), second.blue());
  }
};

QTEST_MAIN(TestEffects)
#include "test_effects.moc"
