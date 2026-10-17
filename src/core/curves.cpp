#include "curves.h"

#include <QStringList>
#include <QtMath>

#include <algorithm>

namespace amber {
namespace curves {

namespace {

const char* const kChannelNames[kChannelCount] = {"rgb", "luma", "r", "g", "b"};

int clamp255(int v) { return qBound(0, v, 255); }

// Fritsch–Carlson tangents: secant average inside, zero at a local extremum or on a flat run, then scaled down
// where they would overshoot
QVector<double> tangents(const Curve& c) {
  const int n = c.size();
  QVector<double> d(n - 1);
  for (int k = 0; k < n - 1; k++) d[k] = double(c[k + 1].y() - c[k].y()) / double(c[k + 1].x() - c[k].x());

  QVector<double> m(n);
  m[0] = d[0];
  m[n - 1] = d[n - 2];
  for (int k = 1; k < n - 1; k++) m[k] = (d[k - 1] * d[k] <= 0.0) ? 0.0 : (d[k - 1] + d[k]) * 0.5;

  for (int k = 0; k < n - 1; k++) {
    if (d[k] == 0.0) {
      m[k] = 0.0;
      m[k + 1] = 0.0;
      continue;
    }
    const double a = m[k] / d[k];
    const double b = m[k + 1] / d[k];
    const double h = a * a + b * b;
    if (h > 9.0) {
      const double t = 3.0 / qSqrt(h);
      m[k] = t * a * d[k];
      m[k + 1] = t * b * d[k];
    }
  }
  return m;
}

double evaluate_with(const Curve& c, const QVector<double>& m, double x) {
  if (x <= c.first().x()) return c.first().y();
  if (x >= c.last().x()) return c.last().y();
  int k = 0;
  while (k < c.size() - 2 && x > c[k + 1].x()) k++;
  const double x0 = c[k].x();
  const double hx = c[k + 1].x() - x0;
  const double t = (x - x0) / hx;
  const double t2 = t * t;
  const double t3 = t2 * t;
  const double v = (2 * t3 - 3 * t2 + 1) * c[k].y() + (t3 - 2 * t2 + t) * hx * m[k] +
                   (-2 * t3 + 3 * t2) * c[k + 1].y() + (t3 - t2) * hx * m[k + 1];
  return qBound(0.0, v, 255.0);
}

// The curve's output for every input 0-255 (input may be fractional through evaluate_with)
struct Sampled {
  const Curve& c;
  QVector<double> m;
  explicit Sampled(const Curve& curve) : c(curve), m(tangents(curve)) {}
  double operator()(double x) const { return evaluate_with(c, m, x); }
};

bool parse_points(const QString& text, Curve& out) {
  const QStringList pairs = text.split(' ', Qt::SkipEmptyParts);
  if (pairs.isEmpty()) return false;
  for (const QString& pair : pairs) {
    const QStringList xy = pair.split(',');
    if (xy.size() != 2) return false;
    bool okx = false, oky = false;
    const int x = xy[0].toInt(&okx);
    const int y = xy[1].toInt(&oky);
    if (!okx || !oky) return false;
    out.append(QPoint(x, y));
  }
  return true;
}

}  // namespace

Curve identity_curve() { return Curve{QPoint(0, 0), QPoint(255, 255)}; }

CurveSet identity() {
  CurveSet s;
  for (Curve& c : s.channels) c = identity_curve();
  return s;
}

bool is_identity(const Curve& c) { return c == identity_curve(); }

Curve canonicalize(Curve c) {
  for (QPoint& p : c) p = QPoint(clamp255(p.x()), clamp255(p.y()));
  std::stable_sort(c.begin(), c.end(), [](const QPoint& a, const QPoint& b) { return a.x() < b.x(); });
  Curve out;
  for (const QPoint& p : c) {
    if (out.isEmpty() || out.last().x() != p.x()) out.append(p);
  }
  if (out.isEmpty()) return identity_curve();
  if (out.size() == 1) {
    if (out.first().x() > 0) out.prepend(QPoint(0, 0));
    if (out.last().x() < 255) out.append(QPoint(255, 255));
  }
  return out;
}

QString serialize(const CurveSet& s) {
  QStringList channels;
  for (int ch = 0; ch < kChannelCount; ch++) {
    QStringList points;
    for (const QPoint& p : s.channels[ch]) points.append(QString::number(p.x()) + ',' + QString::number(p.y()));
    channels.append(QLatin1String(kChannelNames[ch]) + ':' + points.join(' '));
  }
  return channels.join(';');
}

CurveSet parse(const QString& s) {
  CurveSet out = identity();
  bool seen[kChannelCount] = {};
  const QStringList parts = s.split(';', Qt::SkipEmptyParts);
  for (const QString& raw : parts) {
    const QString part = raw.trimmed();
    if (part.isEmpty()) continue;
    const int colon = part.indexOf(':');
    if (colon < 0) return identity();
    const QString name = part.left(colon).trimmed();
    int ch = -1;
    for (int i = 0; i < kChannelCount; i++) {
      if (name == QLatin1String(kChannelNames[i])) ch = i;
    }
    if (ch < 0 || seen[ch]) return identity();
    seen[ch] = true;
    Curve points;
    if (!parse_points(part.mid(colon + 1), points)) return identity();
    out.channels[ch] = canonicalize(points);
  }
  return out;
}

QString identity_string() { return serialize(identity()); }

double evaluate(const Curve& c, double x) {
  if (c.size() < 2) return qBound(0.0, x, 255.0);
  return Sampled(c)(x);
}

QByteArray build_lut(const CurveSet& s) {
  const Sampled master(s.channels[kRgb]);
  const Sampled luma(s.channels[kLuma]);
  const Sampled rgb[3] = {Sampled(s.channels[kRed]), Sampled(s.channels[kGreen]), Sampled(s.channels[kBlue])};

  QByteArray lut(256 * 4, '\0');
  uchar* out = reinterpret_cast<uchar*>(lut.data());
  for (int i = 0; i < 256; i++) {
    const double v = master(i);
    for (int c = 0; c < 3; c++) out[i * 4 + c] = uchar(qRound(rgb[c](v)));
    out[i * 4 + 3] = uchar(qRound(luma(i)));
  }
  return lut;
}

QPoint clamp_point(const Curve& c, int index, QPoint wanted) {
  const int lo = index > 0 ? c[index - 1].x() + 1 : 0;
  const int hi = index < c.size() - 1 ? c[index + 1].x() - 1 : 255;
  return QPoint(qBound(lo, wanted.x(), hi), clamp255(wanted.y()));
}

int insert_point(Curve& c, int x) {
  x = clamp255(x);
  int i = 0;
  while (i < c.size() && c[i].x() < x) i++;
  if (i < c.size() && c[i].x() == x) return i;
  const int y = clamp255(qRound(evaluate(c, x)));
  c.insert(i, QPoint(x, y));
  return i;
}

bool remove_point(Curve& c, int index) {
  if (index <= 0 || index >= c.size() - 1) return false;
  c.remove(index);
  return true;
}

}  // namespace curves
}  // namespace amber
