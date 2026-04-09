#ifndef DOUBLEFIELD_H
#define DOUBLEFIELD_H

#include "../effectfield.h"
#include "core/displaytype.h"

/**
 * @brief The DoubleField class
 *
 * An EffectField derivative the produces number values (integer or floating-point) and uses a LabelSlider as its
 * visual representation.
 */
class DoubleField : public EffectField {
  Q_OBJECT
 public:
  /**
   * @brief Reimplementation of EffectField::EffectField().
   */
  DoubleField(EffectRow* parent, const QString& id);

  /**
   * @brief Get double value at timecode
   *
   * Convenience function. Equivalent to GetValueAt().toDouble()
   *
   * @param timecode
   *
   * Timecode to retrieve value at
   *
   * @return
   *
   * Double value at the set timecode
   */
  double GetDoubleAt(double timecode);

  /**
   * @brief Sets the minimum allowed number for the user to set to `minimum`.
   */
  void SetMinimum(double minimum);

  /**
   * @brief Sets the maximum allowed number for the user to set to `maximum`.
   */
  void SetMaximum(double maximum);

  /**
   * @brief Sets the default number for this field to `d`.
   */
  void SetDefault(double d);

  /**
   * @brief Sets the UI display type to a member of amber::DisplayType.
   */
  void SetDisplayType(amber::DisplayType type);

  /**
   * @brief For a timecode-based display type, sets the frame rate to be used for the displayed timecode
   *
   * \see SetDisplayType() and LabelSlider::SetFrameRate().
   */
  void SetFrameRate(const double& rate);

  int GetDisplayType() const { return static_cast<int>(display_type_); }
  double GetMinimum() const { return min_; }
  double GetMaximum() const { return max_; }
  double GetDefault() const { return default_; }
  double GetFrameRate() const { return frame_rate_; }

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
   * @brief Signal emitted when the field's maximum value has changed
   *
   * This signal gets connected to any LabelSlider created from CreateWidget() so the maximum value is
   * always synchronized between them.
   *
   * Note: A connection is not made both ways as you should never manipulate a UI object created from
   * an EffectField directly. Always access data through the EffectField itself.
   *
   * \see SetMaximum()
   *
   * @param maximum
   *
   * The new maximum value.
   */
  void MaximumChanged(double maximum);

  /**
   * @brief Signal emitted when the field's minimum value has changed
   *
   * This signal gets connected to any LabelSlider created from CreateWidget() so the minimum value is
   * always synchronized between them.
   *
   * Note: A connection is not made both ways as you should never manipulate a UI object created from
   * an EffectField directly. Always access data through the EffectField itself.
   *
   * \see SetMinimum()
   *
   * @param minimum
   *
   * The new minimum value.
   */
  void MinimumChanged(double minimum);

 private:
  /**
   * @brief Internal minimum value
   *
   * \see SetMinimum().
   */
  double min_;

  /**
   * @brief Internal maximum value
   *
   * \see SetMaximum().
   */
  double max_;

  /**
   * @brief Internal default value
   *
   * \see SetDefault().
   */
  double default_{0};

  /**
   * @brief Internal display type value
   *
   * \see SetDisplayType().
   */
  amber::DisplayType display_type_{amber::DisplayType::Normal};

  /**
   * @brief Internal frame rate value
   *
   * \see SetFrameRate().
   */
  double frame_rate_{30};

  /**
   * @brief Internal value used to allow SetDefault() to set the value as well if none has been set
   *
   * Initialized to FALSE, then set to TRUE indefinitely whenever the value gets set on this field.
   */
  bool value_set_{false};

 private slots:
  /**
   * @brief Connected to EffectField::Changed() to ensure value_set_ gets set to TRUE whenever a value is set on this
   * field.
   */
  void ValueHasBeenSet();
};

#endif  // DOUBLEFIELD_H
