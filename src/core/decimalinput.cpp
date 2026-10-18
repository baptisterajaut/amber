#include "core/decimalinput.h"

#include <QtGlobal>

#include <cmath>

namespace amber {

bool parse_decimal_input(const QString& text, double min, double max, int decimals, double* value) {
  // QString::toDouble always parses in the C locale (dot only, no group separator), so mapping ',' to '.' is
  // enough to take both separators. "1,000.5" becomes "1.000.5" and is rejected rather than misread.
  QString t = text;
  t.replace(QLatin1Char(','), QLatin1Char('.'));
  bool ok = false;
  const double v = t.trimmed().toDouble(&ok);
  if (!ok || !std::isfinite(v)) return false;
  *value = QString::number(qBound(min, v, max), 'f', decimals).toDouble();
  return true;
}

}  // namespace amber
