#ifndef COLORFIELD_H
#define COLORFIELD_H

#include "../effectfield.h"

/**
 * @brief The ColorField class
 *
 * An EffectField derivative that produces color values and uses a ColorButton as its UI representative.
 */
class ColorField : public EffectField
{
  Q_OBJECT
public:
  /**
   * @brief Reimplementation of EffectField::EffectField().
   */
  ColorField(EffectRow* parent, const QString& id);

  /**
   * @brief Get the color value at a given timecode
   *
   * A convenience function, equivalent to GetValueAt(timecode).value<QColor>().
   *
   * @param timecode
   *
   * The timecode to retrieve the color at
   *
   * @return
   *
   * The color value at this timecode
   */
  QColor GetColorAt(double timecode);

  /**
   * @brief Reimplementation of EffectField::ConvertStringToValue()
   */
  QVariant ConvertStringToValue(const QString& s) override;

  /**
   * @brief Reimplementation of EffectField::ConvertValueToString()
   */
  QString ConvertValueToString(const QVariant& v) override;
};

#endif // COLORFIELD_H
