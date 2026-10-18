#ifndef UI_DECIMALINPUT_H
#define UI_DECIMALINPUT_H

#include <QInputDialog>

#include "core/decimalinput.h"

namespace amber {

// Drop-in for QInputDialog::getDouble whose text doesn't depend on the locale: the value is prefilled with a dot,
// as slider labels show it, and '.' or ',' are both read as the decimal separator (core/decimalinput.h). A
// locale-following QDoubleSpinBox read "0.5" as 5 under French ('.' is the group separator there).
// *ok is false when cancelled or when the text is not a number; the caller then keeps its value.
// CBA: plain text field, no spin arrows and no live validation (garbage is only refused on OK); a QDoubleSpinBox
// subclass mapping ',' in validate() would bring both back if missed.
inline double get_decimal(QWidget* parent, const QString& title, const QString& label, double value, double min,
                          double max, int decimals, bool* ok) {
  bool accepted = false;
  const QString text = QInputDialog::getText(parent, title, label, QLineEdit::Normal,
                                             QString::number(qBound(min, value, max), 'f', decimals), &accepted);
  double result = value;
  *ok = accepted && parse_decimal_input(text, min, max, decimals, &result);
  return result;
}

}  // namespace amber

#endif  // UI_DECIMALINPUT_H
