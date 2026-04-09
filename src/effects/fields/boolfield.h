#ifndef BOOLFIELD_H
#define BOOLFIELD_H

#include "../effectfield.h"

/**
 * @brief The BoolField class
 *
 * An EffectField derivative the produces boolean values (true or false) and uses a checkbox as its visual representation.
 */
class BoolField : public EffectField
{
  Q_OBJECT
public:
  /**
   * @brief Reimplementation of EffectField::EffectField().
   */
  BoolField(EffectRow* parent, const QString& id);

  /**
   * @brief Get the boolean value at a given timecode
   *
   * A convenience function, equivalent to GetValueAt(timecode).toBool()
   *
   * @param timecode
   *
   * The timecode to retrieve the value at
   *
   * @return
   *
   * The boolean value at this timecode
   */
  bool GetBoolAt(double timecode);

  /**
   * @brief Reimplementation of EffectField::ConvertStringToValue()
   */
  QVariant ConvertStringToValue(const QString& s) override;

  /**
   * @brief Reimplementation of EffectField::ConvertValueToString()
   */
  QString ConvertValueToString(const QVariant& v) override;
signals:
  /**
   * @brief Emitted whenever the UI widget's boolean value has changed
   */
  void Toggled(bool);
};

#endif // BOOLFIELD_H
