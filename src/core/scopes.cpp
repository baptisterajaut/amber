#include "core/scopes.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace amber {
namespace scopes {

Sample point_sample(const uchar* src, int sw, int sh, int stride, int max_w, int max_h) {
  Sample s;
  if (src == nullptr || sw <= 0 || sh <= 0 || max_w <= 0 || max_h <= 0) return s;

  // scale = min(max_w / sw, max_h / sh, 1), in integers: the limiting side gets its max, the other floors
  int w = sw, h = sh;
  if (sw > max_w || sh > max_h) {
    if (qint64(max_w) * sh <= qint64(max_h) * sw) {
      w = max_w;
      h = int(qint64(sh) * max_w / sw);
    } else {
      h = max_h;
      w = int(qint64(sw) * max_h / sh);
    }
  }
  s.w = std::max(1, w);
  s.h = std::max(1, h);
  s.data.resize(size_t(s.w) * s.h * 4);

  uchar* out = s.data.data();
  for (int y = 0; y < s.h; y++) {
    const uchar* row = src + size_t(qint64(y) * sh / s.h) * stride;
    for (int x = 0; x < s.w; x++) {
      memcpy(out, row + size_t(qint64(x) * sw / s.w) * 4, 4);
      out += 4;
    }
  }
  return s;
}

void cb_cr(double r, double g, double b, double* cb, double* cr) {
  const double y = 0.2126 * r + 0.7152 * g + 0.0722 * b;
  *cb = (b - y) / 1.8556;
  *cr = (r - y) / 1.5748;
}

int cb_cr_bin(double c) { return std::clamp(int(std::floor((c + 0.5) * 256 + 1e-4)), 0, 255); }

Histogram histogram(const uchar* rgba, int w, int h, int stride) {
  Histogram hist;
  for (int y = 0; y < h; y++) {
    const uchar* p = rgba + size_t(y) * stride;
    for (int x = 0; x < w; x++, p += 4) {
      hist.r[p[0]]++;
      hist.g[p[1]]++;
      hist.b[p[2]]++;
      hist.luma[luma(p[0], p[1], p[2])]++;
    }
  }
  return hist;
}

WaveformGrid luma_waveform(const uchar* rgba, int w, int h, int stride) {
  WaveformGrid grid;
  grid.columns = std::max(0, w);
  grid.counts.assign(size_t(grid.columns) * 256, 0);
  for (int y = 0; y < h; y++) {
    const uchar* p = rgba + size_t(y) * stride;
    for (int x = 0; x < w; x++, p += 4) grid.counts[size_t(luma(p[0], p[1], p[2])) * w + x]++;
  }
  return grid;
}

std::array<WaveformGrid, 3> rgb_parade(const uchar* rgba, int w, int h, int stride) {
  std::array<WaveformGrid, 3> grids;
  for (WaveformGrid& g : grids) {
    g.columns = std::max(0, w);
    g.counts.assign(size_t(g.columns) * 256, 0);
  }
  for (int y = 0; y < h; y++) {
    const uchar* p = rgba + size_t(y) * stride;
    for (int x = 0; x < w; x++, p += 4) {
      for (int c = 0; c < 3; c++) grids[c].counts[size_t(p[c]) * w + x]++;
    }
  }
  return grids;
}

Vectorscope vectorscope(const uchar* rgba, int w, int h, int stride) {
  Vectorscope vs;
  for (int y = 0; y < h; y++) {
    const uchar* p = rgba + size_t(y) * stride;
    for (int x = 0; x < w; x++, p += 4) {
      double cb, cr;
      cb_cr(p[0] / 255.0, p[1] / 255.0, p[2] / 255.0, &cb, &cr);
      vs.counts[size_t(cb_cr_bin(cr)) * 256 + cb_cr_bin(cb)]++;
    }
  }
  return vs;
}

}  // namespace scopes
}  // namespace amber
