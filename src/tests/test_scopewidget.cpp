#include <QtTest>

#include "ui/scopewidget.h"

// Offscreen smoke test of the Scopes panel's drawing: in every mode a frame draws something on top of the bare
// graticule.
class TestScopeWidget : public QObject {
  Q_OBJECT

  static amber::scopes::Sample bars() {
    amber::scopes::Sample s;
    s.w = 70;
    s.h = 20;
    s.data.resize(size_t(s.w) * s.h * 4);
    const uchar colours[7][3] = {{191, 191, 191}, {191, 191, 0}, {0, 191, 191}, {0, 191, 0},
                                 {191, 0, 191},   {191, 0, 0},   {0, 0, 191}};
    for (int y = 0; y < s.h; y++) {
      for (int x = 0; x < s.w; x++) {
        uchar* p = s.data.data() + (size_t(y) * s.w + x) * 4;
        memcpy(p, colours[x / 10], 3);
        p[3] = 255;
      }
    }
    return s;
  }

  static int lit_pixels(const QImage& img) {
    int n = 0;
    for (int y = 0; y < img.height(); y++) {
      for (int x = 0; x < img.width(); x++) n += qGray(img.pixel(x, y)) > 40;
    }
    return n;
  }

 private slots:
  void everyModeDrawsTheFrame_data() {
    QTest::addColumn<int>("mode");
    QTest::newRow("luma waveform") << int(ScopeMode::LumaWaveform);
    QTest::newRow("rgb parade") << int(ScopeMode::RgbParade);
    QTest::newRow("histogram") << int(ScopeMode::Histogram);
    QTest::newRow("vectorscope") << int(ScopeMode::Vectorscope);
  }
  void everyModeDrawsTheFrame() {
    QFETCH(int, mode);
    ScopeWidget w;
    w.resize(320, 200);
    w.setMode(static_cast<ScopeMode>(mode));
    w.setFrame(amber::scopes::Sample());
    const QImage graticule = w.grab().toImage();
    QVERIFY(lit_pixels(graticule) > 0);  // lines and labels
    w.setFrame(bars());
    const QImage frame = w.grab().toImage();
    QVERIFY2(lit_pixels(frame) > lit_pixels(graticule), "the frame's data isn't drawn");
  }
};

QTEST_MAIN(TestScopeWidget)
#include "test_scopewidget.moc"
