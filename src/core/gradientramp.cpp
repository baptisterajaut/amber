#include "gradientramp.h"

#include <QStringList>
#include <QtMath>

#include <algorithm>
#include <cmath>

namespace amber {
namespace gradientramp {

namespace {

constexpr double kNudge = 1e-6;

int hex_digit(QChar c) {
  if (c >= QLatin1Char('0') && c <= QLatin1Char('9')) return c.unicode() - '0';
  if (c >= QLatin1Char('a') && c <= QLatin1Char('f')) return c.unicode() - 'a' + 10;
  if (c >= QLatin1Char('A') && c <= QLatin1Char('F')) return c.unicode() - 'A' + 10;
  return -1;
}

// "#rrggbbaa" or "#rrggbb"; invalid QColor otherwise (QColor's own parser reads 8 digits as #aarrggbb)
QColor parse_color(QStringView s) {
  if ((s.size() != 7 && s.size() != 9) || s[0] != QLatin1Char('#')) return {};
  int bytes[4] = {0, 0, 0, 255};
  for (int i = 0; i < (s.size() - 1) / 2; i++) {
    const int hi = hex_digit(s[1 + i * 2]);
    const int lo = hex_digit(s[2 + i * 2]);
    if (hi < 0 || lo < 0) return {};
    bytes[i] = hi * 16 + lo;
  }
  return QColor(bytes[0], bytes[1], bytes[2], bytes[3]);
}

QString format_color(const QColor& c) {
  const QColor rgb = c.toRgb();
  return QStringLiteral("#%1%2%3%4")
      .arg(rgb.red(), 2, 16, QLatin1Char('0'))
      .arg(rgb.green(), 2, 16, QLatin1Char('0'))
      .arg(rgb.blue(), 2, 16, QLatin1Char('0'))
      .arg(rgb.alpha(), 2, 16, QLatin1Char('0'));
}

QString format_pos(double pos) {
  QString s = QString::number(pos, 'f', 4);
  while (s.endsWith(QLatin1Char('0'))) s.chop(1);
  if (s.endsWith(QLatin1Char('.'))) s.chop(1);
  return s;
}

void sort_ramp(Ramp& r) {
  std::stable_sort(r.begin(), r.end(), [](const RampStop& a, const RampStop& b) { return a.pos < b.pos; });
}

}  // namespace

double clamp_pos(double pos) { return qBound(kMinPos, std::round(pos * 10000.0) / 10000.0, kMaxPos); }

Ramp parse(const QString& value) {
  Ramp r;
  const QStringList entries = value.split(QLatin1Char(';'), Qt::SkipEmptyParts);
  for (const QString& entry : entries) {
    if (r.size() >= kMaxStops) break;
    const QStringList parts = entry.simplified().split(QLatin1Char(' '), Qt::SkipEmptyParts);
    if (parts.size() != 2) continue;
    bool ok = false;
    const double pos = parts[0].toDouble(&ok);
    if (!ok || !std::isfinite(pos)) continue;
    const QColor c = parse_color(parts[1]);
    if (!c.isValid()) continue;
    r.append({clamp_pos(pos), c});
  }
  sort_ramp(r);
  return r;
}

QString serialize(Ramp ramp) {
  if (ramp.size() > kMaxStops) ramp.resize(kMaxStops);
  for (RampStop& s : ramp) s.pos = clamp_pos(s.pos);
  sort_ramp(ramp);
  QStringList out;
  for (const RampStop& s : ramp) out << format_pos(s.pos) + QLatin1Char(' ') + format_color(s.color);
  return out.join(QLatin1Char(';'));
}

QGradientStops stops(const QColor& start, const QColor& end, const Ramp& ramp) {
  Ramp sorted = ramp;
  sort_ramp(sorted);
  QGradientStops out;
  out.reserve(sorted.size() + 2);
  out.append({0.0, start});
  double last = 0.0;
  for (const RampStop& s : sorted) {
    double p = qBound(kMinPos, s.pos, kMaxPos);
    if (p <= last) p = last + kNudge;
    out.append({p, s.color});
    last = p;
  }
  out.append({1.0, end});
  return out;
}

QGradientStops stops(const QColor& start, const QColor& end, const QString& value) {
  return stops(start, end, parse(value));
}

QColor color_at(const QGradientStops& stops, double pos) {
  if (stops.isEmpty()) return {};
  if (pos <= stops.first().first) return stops.first().second;
  if (pos >= stops.last().first) return stops.last().second;
  int i = 1;
  while (i < stops.size() - 1 && stops[i].first < pos) i++;
  const QGradientStop& a = stops[i - 1];
  const QGradientStop& b = stops[i];
  const double span = b.first - a.first;
  const double t = span > 0 ? (pos - a.first) / span : 1.0;
  const QColor ca = a.second.toRgb();
  const QColor cb = b.second.toRgb();
  const double aa = ca.alphaF(), ab = cb.alphaF();
  const double alpha = aa + (ab - aa) * t;
  if (alpha <= 0.0) return QColor(0, 0, 0, 0);
  auto channel = [&](double x, double y) {
    const double premul = x * aa + (y * ab - x * aa) * t;
    return qBound(0.0, premul / alpha, 1.0);
  };
  // 8 bits per channel, what a stop string holds
  return QColor(qRound(channel(ca.redF(), cb.redF()) * 255.0), qRound(channel(ca.greenF(), cb.greenF()) * 255.0),
                qRound(channel(ca.blueF(), cb.blueF()) * 255.0), qRound(alpha * 255.0));
}

Ramp reversed(const Ramp& ramp) {
  Ramp sorted = ramp;
  sort_ramp(sorted);
  Ramp out;
  out.reserve(sorted.size());
  for (int i = sorted.size() - 1; i >= 0; i--) out.append({clamp_pos(1.0 - sorted[i].pos), sorted[i].color});
  return out;
}

}  // namespace gradientramp
}  // namespace amber
