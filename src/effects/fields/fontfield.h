#ifndef FONTFIELD_H
#define FONTFIELD_H

#include "combofield.h"

/**
 * @brief The FontField class
 *
 * An EffectField derivative the produces font family names in string and uses a QComboBox
 * as its visual representation.
 *
 * TODO Upgrade to QFontComboBox.
 */
class FontField : public EffectField {
  Q_OBJECT
public:
  /**
   * @brief Reimplementation of EffectField::EffectField().
   */
  FontField(EffectRow* parent, const QString& id);

  /**
   * @brief Get the font family name at the given timecode
   *
   * A convenience function, equivalent to GetValueAt(timecode).toString()
   *
   * @param timecode
   *
   * The timecode to retrieve the font family name at
   *
   * @return
   *
   * The font family name at this timecode
   */
  QString GetFontAt(double timecode);

  const QStringList& GetFontList() const { return font_list; }

private:
  /**
   * @brief Internal list of fonts to add to a QComboBox when creating one in CreateWidget().
   *
   * NOTE: Deprecated. Once QComboBox is replaced by QFontComboBox this will be completely unnecessary.
   */
  QStringList font_list;
};

#endif // FONTFIELD_H
