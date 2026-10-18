#include <QtTest>

#include "core/gradientramp.h"

using namespace amber::gradientramp;

class TestGradientRamp : public QObject {
  Q_OBJECT

 private slots:
  void emptyIsNoStop() {
    QVERIFY(parse(QString()).isEmpty());
    QVERIFY(parse(QStringLiteral("   ")).isEmpty());
    QCOMPARE(serialize({}), QString());
    QCOMPARE(canonical(QString()), QString());
  }

  void roundTrip() {
    const QString s = QStringLiteral("0.25 #ff8800ff;0.6 #0044ccff");
    const Ramp r = parse(s);
    QCOMPARE(r.size(), 2);
    QCOMPARE(r[0].pos, 0.25);
    QCOMPARE(r[0].color, QColor(255, 136, 0, 255));
    QCOMPARE(r[1].pos, 0.6);
    QCOMPARE(r[1].color, QColor(0, 68, 204, 255));
    QCOMPARE(serialize(r), s);
    QCOMPARE(canonical(s), s);
  }

  void alphaIsKept() {
    const Ramp r = parse(QStringLiteral("0.5 #11223344"));
    QCOMPARE(r.size(), 1);
    QCOMPARE(r[0].color, QColor(0x11, 0x22, 0x33, 0x44));
    QCOMPARE(serialize(r), QStringLiteral("0.5 #11223344"));
  }

  void canonicalSortsAndTidies() {
    // Out of order, extra spaces, an empty entry, upper-case hex, #rrggbb (opaque), a trailing ';'
    QCOMPARE(canonical(QStringLiteral("  0.70   #ABCDEF ;;0.1 #000000ff;")),
             QStringLiteral("0.1 #000000ff;0.7 #abcdefff"));
    // Four decimals, trailing zeros dropped
    QCOMPARE(canonical(QStringLiteral("0.123456 #ffffffff")), QStringLiteral("0.1235 #ffffffff"));
    QCOMPARE(canonical(QStringLiteral("0.5000 #ffffffff")), QStringLiteral("0.5 #ffffffff"));
  }

  void malformedEntriesAreSkipped() {
    const QString s = QStringLiteral(
        "abc;0.3;#ff0000ff;0.4 #ff00;0.5 #gg0000ff;0.6 ff0000ff;nan #ff0000ff;inf #ff0000ff;0.7 #ff0000ff extra;"
        "0.8 #00ff00ff");
    QCOMPARE(canonical(s), QStringLiteral("0.8 #00ff00ff"));
    QCOMPARE(canonical(QStringLiteral("garbage")), QString());
  }

  void positionsAreClamped() {
    QCOMPARE(canonical(QStringLiteral("0 #ff0000ff;1 #00ff00ff;-3 #0000ffff;7 #ffffffff")),
             QStringLiteral("0.0001 #ff0000ff;0.0001 #0000ffff;0.9999 #00ff00ff;0.9999 #ffffffff"));
    QCOMPARE(clamp_pos(0.0), kMinPos);
    QCOMPARE(clamp_pos(1.0), kMaxPos);
    QCOMPARE(clamp_pos(0.33333), 0.3333);
    QCOMPARE(clamp_pos(0.00004), kMinPos);  // rounds to 0, clamped back in
  }

  void capsAtSixteen() {
    QStringList entries;
    for (int i = 0; i < 20; i++) entries << QStringLiteral("0.%1 #ff0000ff").arg(20 - i, 2, 10, QLatin1Char('0'));
    const Ramp r = parse(entries.join(';'));
    QCOMPARE(r.size(), kMaxStops);
    // The first sixteen valid entries (0.20 down to 0.05), sorted
    QCOMPARE(r.first().pos, 0.05);
    QCOMPARE(r.last().pos, 0.2);
    // Malformed entries don't count towards the cap
    QCOMPARE(parse(QStringLiteral("x;") + entries.mid(0, 16).join(';')).size(), kMaxStops);
  }

  void equalPositionsKeepTheirOrder() {
    const Ramp r = parse(QStringLiteral("0.5 #0000ffff;0.2 #ffffffff;0.5 #ff0000ff"));
    QCOMPARE(r.size(), 3);
    QCOMPARE(r[1].color, QColor(Qt::blue));
    QCOMPARE(r[2].color, QColor(Qt::red));
    QCOMPARE(serialize(r), QStringLiteral("0.2 #ffffffff;0.5 #0000ffff;0.5 #ff0000ff"));
  }

  void stopsHaveTheEnds() {
    const QGradientStops none = stops(Qt::black, Qt::white, QString());
    QCOMPARE(none.size(), 2);
    QCOMPARE(none[0], QGradientStop(0.0, QColor(Qt::black)));
    QCOMPARE(none[1], QGradientStop(1.0, QColor(Qt::white)));

    const QGradientStops some = stops(Qt::black, Qt::white, QStringLiteral("0.6 #0000ffff;0.3 #ff0000ff"));
    QCOMPARE(some.size(), 4);
    QCOMPARE(some[0], QGradientStop(0.0, QColor(Qt::black)));
    QCOMPARE(some[1], QGradientStop(0.3, QColor(Qt::red)));
    QCOMPARE(some[2], QGradientStop(0.6, QColor(Qt::blue)));
    QCOMPARE(some[3], QGradientStop(1.0, QColor(Qt::white)));
  }

  void stopsAtTheSamePositionMakeAHardEdge() {
    // QGradient::setStops() collapses equal positions (only the last colour survives), so stops() spreads them by a
    // hair, in order: a hard edge from red to blue
    const QGradientStops s = stops(Qt::black, Qt::white, QStringLiteral("0.5 #ff0000ff;0.5 #0000ffff"));
    QCOMPARE(s.size(), 4);
    QCOMPARE(s[1].second, QColor(Qt::red));
    QCOMPARE(s[2].second, QColor(Qt::blue));
    QCOMPARE(s[1].first, 0.5);
    QVERIFY(s[2].first > s[1].first);
    QVERIFY(s[2].first - s[1].first < 1e-5);
    QLinearGradient g(0, 0, 1, 0);
    g.setStops(s);
    QCOMPARE(g.stops().size(), 4);

    // Sixteen at the last position stay strictly increasing and below the end stop
    QStringList entries;
    for (int i = 0; i < kMaxStops; i++) entries << QStringLiteral("0.9999 #ff0000ff");
    const QGradientStops t = stops(Qt::black, Qt::white, entries.join(';'));
    QCOMPARE(t.size(), kMaxStops + 2);
    for (int i = 1; i < t.size(); i++) QVERIFY(t[i].first > t[i - 1].first);
    QCOMPARE(t.last().first, 1.0);
  }

  void colorAtFollowsTheRamp() {
    const QGradientStops s = stops(Qt::black, Qt::white, QStringLiteral("0.5 #ff0000ff"));
    QCOMPARE(color_at(s, 0.0), QColor(Qt::black));
    QCOMPARE(color_at(s, 0.5), QColor(Qt::red));
    QCOMPARE(color_at(s, 1.0), QColor(Qt::white));
    const QColor q = color_at(s, 0.25);  // black/red midpoint
    QVERIFY(qAbs(q.red() - 128) <= 1);
    QCOMPARE(q.green(), 0);
    QCOMPARE(q.blue(), 0);
    const QColor r = color_at(s, 0.75);  // red/white midpoint
    QVERIFY(qAbs(r.green() - 128) <= 1);
    QCOMPARE(r.red(), 255);
  }

  void colorAtInterpolatesPremultiplied() {
    // Like QPainter's default ColorInterpolation: opaque red to transparent black is red fading out, not darkening
    const QGradientStops s = {{0.0, QColor(255, 0, 0, 255)}, {1.0, QColor(0, 0, 0, 0)}};
    const QColor mid = color_at(s, 0.5);
    QVERIFY(qAbs(mid.alpha() - 128) <= 1);
    QVERIFY(mid.red() >= 254);
  }

  void reversedMirrors() {
    const Ramp r = reversed(parse(QStringLiteral("0.2 #ff0000ff;0.5 #00ff00ff;0.5 #0000ffff")));
    QCOMPARE(serialize(r), QStringLiteral("0.5 #0000ffff;0.5 #00ff00ff;0.8 #ff0000ff"));
    QCOMPARE(serialize(reversed(r)), QStringLiteral("0.2 #ff0000ff;0.5 #00ff00ff;0.5 #0000ffff"));
  }
};

QTEST_GUILESS_MAIN(TestGradientRamp)
#include "test_gradientramp.moc"
