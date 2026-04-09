#ifndef EFFECTFIELDWIDGET_H
#define EFFECTFIELDWIDGET_H

#include <QWidget>

class EffectField;

/**
 * @brief Base class for EffectField widget wrappers.
 *
 * Each data-bearing EffectField subclass has a corresponding EffectFieldWidget subclass
 * that handles widget creation, signal/slot wiring to the UI, and drag undo tracking.
 * The data class (EffectField) knows nothing about widgets; the widget class holds a
 * pointer back to the data class and reads/writes through its public API.
 */
class EffectFieldWidget : public QObject {
  Q_OBJECT
public:
  explicit EffectFieldWidget(EffectField* field, QObject* parent = nullptr);
  virtual ~EffectFieldWidget() = default;

  /**
   * @brief Create or attach to a widget for user interaction.
   *
   * If existing is nullptr, creates a new widget. Otherwise attaches
   * signals/slots to the existing widget (used when multiple effects
   * share a single UI row).
   */
  virtual QWidget* CreateWidget(QWidget* existing = nullptr) = 0;

  /**
   * @brief Update the widget to display the value at the given timecode.
   */
  virtual void UpdateWidgetValue(QWidget* widget, double timecode) = 0;

  /**
   * @brief Get the data field this widget wraps.
   */
  EffectField* GetField() const;

  /**
   * @brief Factory: create the correct widget wrapper for a given field.
   *
   * Dispatches on field->type() to construct the appropriate subclass.
   * Handles all field types including UI-only fields (ButtonField, LabelField).
   * Returns nullptr only for unknown field types.
   */
  static EffectFieldWidget* Create(EffectField* field, QObject* parent = nullptr);

protected:
  EffectField* field_;
};

#endif  // EFFECTFIELDWIDGET_H
