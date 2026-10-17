#include <QtTest>

#include "core/scopes.h"

using namespace amber::scopes;

namespace {

std::vector<uchar> solid(int w, int h, uchar r, uchar g, uchar b) {
  std::vector<uchar> px(size_t(w) * h * 4);
  for (size_t i = 0; i < px.size(); i += 4) {
    px[i] = r;
    px[i + 1] = g;
    px[i + 2] = b;
    px[i + 3] = 255;
  }
  return px;
}

// Each pixel stores its own coordinates: R, G = x low/high byte, B, A = y low/high byte.
std::vector<uchar> coords(int w, int h, int stride) {
  std::vector<uchar> px(size_t(stride) * h, 0xEE);  // padding bytes are garbage
  for (int y = 0; y < h; y++) {
    for (int x = 0; x < w; x++) {
      uchar* p = px.data() + size_t(y) * stride + x * 4;
      p[0] = x & 0xFF;
      p[1] = x >> 8;
      p[2] = y & 0xFF;
      p[3] = y >> 8;
    }
  }
  return px;
}

int sample_x(const Sample& s, int x, int y) {
  const uchar* p = s.data.data() + (size_t(y) * s.w + x) * 4;
  return p[0] | (p[1] << 8);
}
int sample_y(const Sample& s, int x, int y) {
  const uchar* p = s.data.data() + (size_t(y) * s.w + x) * 4;
  return p[2] | (p[3] << 8);
}

}  // namespace

class TestScopes : public QObject {
  Q_OBJECT

 private slots:
  void lumaWeights() {
    QCOMPARE(luma(128, 128, 128), 128);
    QCOMPARE(luma(255, 255, 255), 255);
    QCOMPARE(luma(0, 0, 0), 0);
    QCOMPARE(luma(255, 0, 0), 54);
  }

  void solidGrey() {
    const int w = 64, h = 32;
    auto px = solid(w, h, 128, 128, 128);
    Histogram hist = histogram(px.data(), w, h, w * 4);
    QCOMPARE(hist.luma[128], uint32_t(w * h));
    QCOMPARE(hist.r[128], uint32_t(w * h));

    WaveformGrid wf = luma_waveform(px.data(), w, h, w * 4);
    QCOMPARE(wf.columns, w);
    for (int x = 0; x < w; x++) QCOMPARE(wf.at(x, 128), uint32_t(h));

    Vectorscope vs = vectorscope(px.data(), w, h, w * 4);
    QCOMPARE(vs.at(128, 128), uint32_t(w * h));
  }

  void pureRed() {
    const int w = 16, h = 8;
    auto px = solid(w, h, 255, 0, 0);
    Histogram hist = histogram(px.data(), w, h, w * 4);
    QCOMPARE(hist.r[255], uint32_t(w * h));
    QCOMPARE(hist.g[0], uint32_t(w * h));
    QCOMPARE(hist.b[0], uint32_t(w * h));
    QCOMPARE(hist.luma[54], uint32_t(w * h));

    auto parade = rgb_parade(px.data(), w, h, w * 4);
    for (int x = 0; x < w; x++) {
      QCOMPARE(parade[0].at(x, 255), uint32_t(h));
      QCOMPARE(parade[1].at(x, 0), uint32_t(h));
      QCOMPARE(parade[2].at(x, 0), uint32_t(h));
    }

    // Cr = +0.5 clamps to 255; Cb = -0.2126 / 1.8556 = -0.1146 -> floor(0.3854 * 256) = 98
    Vectorscope vs = vectorscope(px.data(), w, h, w * 4);
    QCOMPARE(vs.at(255, 98), uint32_t(w * h));
  }

  void horizontalGradient() {
    const int w = 256, h = 4;
    std::vector<uchar> px(size_t(w) * h * 4);
    for (int y = 0; y < h; y++) {
      for (int x = 0; x < w; x++) {
        uchar v = uchar(qRound(x * 255.0 / (w - 1)));
        uchar* p = px.data() + (size_t(y) * w + x) * 4;
        p[0] = p[1] = p[2] = v;
        p[3] = 255;
      }
    }
    WaveformGrid wf = luma_waveform(px.data(), w, h, w * 4);
    for (int x = 0; x < w; x++) {
      uint32_t near = 0;
      for (int v = qMax(0, x - 1); v <= qMin(255, x + 1); v++) near += wf.at(x, v);
      QCOMPARE(near, uint32_t(h));
    }
  }

  void pointSample1080p() {
    const int sw = 1920, sh = 1080;
    auto px = coords(sw, sh, sw * 4);
    Sample s = point_sample(px.data(), sw, sh, sw * 4);
    QCOMPARE(s.w, 512);
    QCOMPARE(s.h, 288);
    QCOMPARE(int(s.data.size()), 512 * 288 * 4);
    QCOMPARE(sample_x(s, 0, 0), 0);
    QCOMPARE(sample_y(s, 0, 0), 0);
    QCOMPARE(sample_x(s, 511, 287), 511 * 1920 / 512);  // 1916
    QCOMPARE(sample_y(s, 511, 287), 287 * 1080 / 288);  // 1076
    QCOMPARE(sample_x(s, 256, 144), 960);
    QCOMPARE(sample_y(s, 256, 144), 540);
  }

  void pointSampleScopeAndStride() {
    const int sw = 1920, sh = 804, stride = sw * 4 + 64;  // padded rows
    auto px = coords(sw, sh, stride);
    Sample s = point_sample(px.data(), sw, sh, stride);
    QCOMPARE(s.w, 512);
    QCOMPARE(s.h, 214);
    QCOMPARE(sample_x(s, 511, 213), 511 * 1920 / 512);
    QCOMPARE(sample_y(s, 511, 213), 213 * 804 / 214);
    QCOMPARE(sample_x(s, 256, 107), 960);
    QCOMPARE(sample_y(s, 256, 107), 107 * 804 / 214);
    QCOMPARE(sample_x(s, 0, 213), 0);
    QCOMPARE(sample_y(s, 511, 0), 0);
  }

  void pointSampleSmallStays() {
    const int sw = 100, sh = 50;
    auto px = coords(sw, sh, sw * 4);
    Sample s = point_sample(px.data(), sw, sh, sw * 4);
    QCOMPARE(s.w, 100);
    QCOMPARE(s.h, 50);
    QCOMPARE(sample_x(s, 99, 49), 99);
    QCOMPARE(sample_y(s, 99, 49), 49);
  }

  void pointSampleDegenerate() {
    // Extreme aspect never rounds a side to 0.
    const int sw = 4000, sh = 1;
    auto px = coords(sw, sh, sw * 4);
    Sample s = point_sample(px.data(), sw, sh, sw * 4);
    QCOMPARE(s.w, 512);
    QCOMPARE(s.h, 1);
    QVERIFY(point_sample(nullptr, 0, 0, 0).empty());
  }
};

QTEST_GUILESS_MAIN(TestScopes)
#include "test_scopes.moc"
