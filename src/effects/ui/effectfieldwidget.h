#ifndef EFFECTFIELDWIDGET_H
#define EFFECTFIELDWIDGET_H

#include <QPointer>
#include <QVector>
#include <QWidget>
#include <functional>

class EffectField;
class QUndoCommand;

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
  /**
   * @brief Register this wrapper on the widget it is wiring up.
   *
   * A widget shared by several effects (multi-selected clips) is driven by the wrapper that created it: the others
   * join that owner instead of reacting to the widget themselves, so one edit makes one undo entry for all of them.
   *
   * @return **TRUE** if this wrapper drives the widget and must connect its value-change signal, **FALSE** if it
   * joined the owner (which applies its edits through PushEdit()).
   */
  bool AttachToWidget(QWidget* widget);

  /**
   * @brief Apply one widget edit to this wrapper's field and every attached one, as one undo entry.
   *
   * `apply` edits the field of the wrapper it is given (always of this wrapper's class) and returns the undo command
   * it would have pushed alone, or nullptr when that field needs none. Nothing is pushed if no field needs one.
   */
  void PushEdit(const std::function<QUndoCommand*(EffectFieldWidget*)>& apply);

  EffectField* field_;

private:
  QVector<QPointer<EffectFieldWidget>> followers_;  // wrappers attached to the widget this one drives
};

#endif  // EFFECTFIELDWIDGET_H
