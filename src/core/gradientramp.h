#ifndef GRADIENTRAMP_H
#define GRADIENTRAMP_H

#include <QBrush>
#include <QColor>
#include <QString>
#include <QVector>

namespace amber {
namespace gradientramp {

// The Gradient effect's colour ramp: pure data, no widgets. Start Color and End Color are the stops at 0 and 1 (their
// own keyframeable fields); this module holds the intermediate stops, as one string:
//   "0.25 #ff8800ff;0.6 #0044ccff"
// positions in (0, 1) with 4 decimals (trailing zeros dropped), colours #rrggbbaa, sorted by position (equal positions
// keep their order), ';'-separated. "" = no intermediate stop, the plain two-colour gradient.
// CBA: stops interpolate in sRGB; no midpoints or ramp presets

struct RampStop {
  double pos;
  QColor color;
  bool operator==(const RampStop& o) const { return pos == o.pos && color == o.color; }
  bool operator!=(const RampStop& o) const { return !(*this == o); }
};
using Ramp = QVector<RampStop>;

constexpr int kMaxStops = 16;
constexpr double kMinPos = 0.0001;
constexpr double kMaxPos = 0.9999;

// Rounded to 4 decimals and clamped into [kMinPos, kMaxPos]
double clamp_pos(double pos);

// Tolerant: skips malformed entries (bad number, NaN, bad colour, extra tokens), accepts #rrggbb as opaque, clamps
// positions, keeps the first kMaxStops valid entries, sorts them (stable)
Ramp parse(const QString& value);
// Canonical string of `ramp`, clamped, capped and sorted like parse()
QString serialize(Ramp ramp);
inline QString canonical(const QString& value) { return serialize(parse(value)); }

// `start` at 0, the intermediate stops, `end` at 1, for QGradient::setStops(). That collapses equal positions to the
// last one, so a run of equal positions is spread by a hair (1e-6 each, far below the 4-decimal grid): a hard edge.
QGradientStops stops(const QColor& start, const QColor& end, const Ramp& ramp);
QGradientStops stops(const QColor& start, const QColor& end, const QString& value);

// The colour of a gradient with these (sorted) stops at `pos`, interpolated premultiplied like QPainter's default
// QGradient::ColorInterpolation, so a stop added with it doesn't change the picture. 8 bits per channel.
QColor color_at(const QGradientStops& stops, double pos);

// Positions mirrored (1 - pos) and the order reversed, so a mirrored hard edge stays one
Ramp reversed(const Ramp& ramp);

}  // namespace gradientramp
}  // namespace amber

#endif  // GRADIENTRAMP_H
