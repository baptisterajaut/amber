#ifndef SCOPES_H
#define SCOPES_H

#include <QtGlobal>

#include <array>
#include <cstdint>
#include <vector>

namespace amber {
namespace scopes {

// Video scopes computed on the CPU from an RGBA8 frame (the viewer's readback: premultiplied, composited over
// black, rows top to bottom). Pure functions, no widgets: the Scopes panel draws the results. Every analysis is a
// single pass over the pixels; `stride` is the byte length of a row.

// A frame point-sampled down to a small size, RGBA8, tightly packed.
struct Sample {
  std::vector<uchar> data;
  int w = 0;
  int h = 0;
  bool empty() const { return w <= 0 || h <= 0; }
};

// Nearest-pixel downscale to fit max_w x max_h (aspect kept, never upscaled, at least 1 x 1). Output pixel x reads
// source pixel x * sw / w (integer), same for y. Runs under the render thread's buffer lock: keep it a plain copy.
Sample point_sample(const uchar* src, int sw, int sh, int stride, int max_w = 512, int max_h = 288);

// Rec.709 luma in 8-bit fixed point: weights sum to 256, so grey stays grey (128 -> 128) and red is 54.
inline int luma(int r, int g, int b) { return (54 * r + 183 * g + 19 * b + 128) >> 8; }

// Rec.709 colour difference of r, g, b in [0, 1]: each of cb, cr in [-0.5, 0.5].
void cb_cr(double r, double g, double b, double* cb, double* cr);

// Vectorscope bin of a Cb or Cr value: 0..255, 0 -> 128, +0.5 clamps to 255.
int cb_cr_bin(double c);

struct Histogram {
  std::array<uint32_t, 256> r{};
  std::array<uint32_t, 256> g{};
  std::array<uint32_t, 256> b{};
  std::array<uint32_t, 256> luma{};
};

// columns x 256 counts. Row index = value (0 = black at row 0): the vertical flip happens only when drawing.
struct WaveformGrid {
  int columns = 0;
  std::vector<uint32_t> counts;  // counts[value * columns + column]
  uint32_t at(int column, int value) const { return counts[size_t(value) * columns + column]; }
};

// 256 x 256 counts indexed [cr_bin][cb_bin].
struct Vectorscope {
  std::vector<uint32_t> counts = std::vector<uint32_t>(256 * 256, 0);
  uint32_t at(int cr_bin, int cb_bin) const { return counts[size_t(cr_bin) * 256 + cb_bin]; }
};

Histogram histogram(const uchar* rgba, int w, int h, int stride);

// One column per pixel column, the luma of each pixel counted at its value.
WaveformGrid luma_waveform(const uchar* rgba, int w, int h, int stride);

// R, G, B waveforms (in that order), one column per pixel column each, drawn side by side.
std::array<WaveformGrid, 3> rgb_parade(const uchar* rgba, int w, int h, int stride);

Vectorscope vectorscope(const uchar* rgba, int w, int h, int stride);

}  // namespace scopes
}  // namespace amber

#endif  // SCOPES_H
