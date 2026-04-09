#ifndef STRINGFIELD_H
#define STRINGFIELD_H

#include "../effectfield.h"

/**
 * @brief The StringField class
 *
 * An EffectField derivative that produces arbitrary strings entered by the user and uses a TextEditEx as its
 * visual representation.
 */
class StringField : public EffectField
{
  Q_OBJECT
public:
  /**
   * @brief Reimplementation of EffectField::EffectField().
   *
   * Provides a setting for whether this StringField - and its attached TextEditEx objects - should operate in rich
   * text or plain text mode, defaulting to rich text mode.
   */
  StringField(EffectRow* parent, const QString& id, bool rich_text = true);

  /**
   * @brief Get the string at the given timecode
   *
   * A convenience function, equivalent to GetValueAt(timecode).toString()
   *
   * @param timecode
   *
   * The timecode to retrieve the string at
   *
   * @return
   *
   * The string at this timecode
   */
  QString GetStringAt(double timecode);

  bool IsRichText() const { return rich_text_; }

private:
  /**
   * @brief Internal value for whether this field is in rich text or plain text mode
   */
  bool rich_text_;
};

#endif // STRINGFIELD_H
