#ifndef DECIMALINPUT_H
#define DECIMALINPUT_H

#include <QString>

namespace amber {

// Reads a number typed in a value prompt. Both '.' and ',' are taken as the decimal separator whatever the locale
// ("0.5" and "0,5" are 0.5), so a value copied from a slider label (always shown with a dot) reads back the same.
// The result is clamped to [min, max] and rounded to `decimals` places, like the QDoubleSpinBox it replaces.
// Returns false and leaves *value untouched on empty, non-numeric or non-finite text.
bool parse_decimal_input(const QString& text, double min, double max, int decimals, double* value);

}  // namespace amber

#endif  // DECIMALINPUT_H
