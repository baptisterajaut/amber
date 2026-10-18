#include <QtTest>

#include "core/decimalinput.h"

using amber::parse_decimal_input;

class TestDecimalInput : public QObject {
  Q_OBJECT

 private slots:
  void cleanup() { QLocale::setDefault(QLocale::c()); }

  void parse_data() {
    QTest::addColumn<QString>("locale");
    QTest::addColumn<QString>("text");
    QTest::addColumn<bool>("ok");
    QTest::addColumn<double>("expected");
    for (const QString& loc : {QStringLiteral("C"), QStringLiteral("fr_FR")}) {
      const QByteArray p = loc.toLatin1() + " ";
      QTest::newRow(p + "dot") << loc << "0.5" << true << 0.5;
      QTest::newRow(p + "comma") << loc << "0,5" << true << 0.5;
      QTest::newRow(p + "integer") << loc << "42" << true << 42.0;
      QTest::newRow(p + "negative dot") << loc << "-1.25" << true << -1.25;
      QTest::newRow(p + "negative comma") << loc << "-1,25" << true << -1.25;
      QTest::newRow(p + "spaces") << loc << "  2,5 " << true << 2.5;
      QTest::newRow(p + "above max") << loc << "250" << true << 100.0;
      QTest::newRow(p + "below min") << loc << "-300,5" << true << -100.0;
      QTest::newRow(p + "rounded to decimals") << loc << "0,123456" << true << 0.123;
      QTest::newRow(p + "empty") << loc << "" << false << 7.0;
      QTest::newRow(p + "blank") << loc << "   " << false << 7.0;
      QTest::newRow(p + "letters") << loc << "abc" << false << 7.0;
      QTest::newRow(p + "trailing junk") << loc << "0.5x" << false << 7.0;
      QTest::newRow(p + "two separators") << loc << "1,000.5" << false << 7.0;
      QTest::newRow(p + "nan") << loc << "nan" << false << 7.0;
      QTest::newRow(p + "inf") << loc << "inf" << false << 7.0;
    }
  }

  void parse() {
    QFETCH(QString, locale);
    QFETCH(QString, text);
    QFETCH(bool, ok);
    QFETCH(double, expected);
    QLocale::setDefault(QLocale(locale));
    double v = 7.0;  // untouched on failure
    QCOMPARE(parse_decimal_input(text, -100, 100, 3, &v), ok);
    QCOMPARE(v, expected);
  }

  // The prompt prefills QString::number(value, 'f', decimals): it must read back to the same value in any locale.
  void prefillRoundTrips() {
    QLocale::setDefault(QLocale(QLocale::French, QLocale::France));
    const QString shown = QString::number(12.5, 'f', 1);
    QCOMPARE(shown, QStringLiteral("12.5"));
    double v = 0;
    QVERIFY(parse_decimal_input(shown, 0, 100, 1, &v));
    QCOMPARE(v, 12.5);
  }
};

QTEST_GUILESS_MAIN(TestDecimalInput)
#include "test_decimal_input.moc"
