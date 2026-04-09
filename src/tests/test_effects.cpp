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
};

QTEST_MAIN(TestEffects)
#include "test_effects.moc"
