#ifndef CURVES_H
#define CURVES_H

#include <QByteArray>
#include <QPoint>
#include <QString>
#include <QVector>

#include <array>

namespace amber {
namespace curves {

// Tone curves of the Curves effect: pure math, no widgets and no GPU. A curve maps input 0-255 to output 0-255
// through a few integer control points; the shader samples the 256-entry table built here.

enum Channel { kRgb = 0, kLuma, kRed, kGreen, kBlue, kChannelCount };

// Control points sorted by x, distinct x, coordinates 0-255, at least two. The first and last points are the
// endpoints: the curve is flat at their y outside them.
using Curve = QVector<QPoint>;

struct CurveSet {
  std::array<Curve, kChannelCount> channels;
  bool operator==(const CurveSet& o) const { return channels == o.channels; }
  bool operator!=(const CurveSet& o) const { return !(*this == o); }
};

// (0,0) (255,255)
Curve identity_curve();
CurveSet identity();
// The default point list exactly (a curve with an extra point on the diagonal is not identity)
bool is_identity(const Curve& c);

// Clamps to 0-255, sorts by x (stable), keeps the first point of a duplicated x, and adds the (0,0) / (255,255)
// endpoints to a list of fewer than two points
Curve canonicalize(Curve c);

// "rgb:0,0 255,255;luma:...;r:...;g:...;b:..." — fixed channel order, single spaces
QString serialize(const CurveSet& s);
// Any channel order, missing channels are identity, each channel canonicalised. Malformed input (unknown or
// repeated channel, bad point syntax, an empty point list) gives identity.
CurveSet parse(const QString& s);
inline QString canonical(const QString& s) { return serialize(parse(s)); }
QString identity_string();

// Monotone cubic (Fritsch–Carlson) through the points: never overshoots or inverts between two points.
// x and the result in 0-255.
double evaluate(const Curve& c, double x);

// 256 x RGBA8: R = red(master(v)), G = green(master(v)), B = blue(master(v)), A = luma(v)
QByteArray build_lut(const CurveSet& s);

// Editing rules, shared by the curve editor and its tests:
// where point `index` lands when dragged to `wanted`: y in 0-255, x in 0-255 and at least 1 away from its neighbours
QPoint clamp_point(const Curve& c, int index, QPoint wanted);
// Adds a point at x (clamped to 0-255) on the curve's current y and returns its index; a point already at x is
// returned instead
int insert_point(Curve& c, int x);
// Removes an inner point; endpoints (and out-of-range indices) are refused
bool remove_point(Curve& c, int index);

}  // namespace curves
}  // namespace amber

#endif  // CURVES_H
