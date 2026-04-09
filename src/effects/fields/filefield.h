#ifndef FILEFIELD_H
#define FILEFIELD_H

#include "../effectfield.h"

/**
 * @brief The FileField class
 *
 * An EffectField derivative that produces filenames in string and uses an EmbeddedFileChooser
 * as its visual representation.
 */
class FileField : public EffectField
{
  Q_OBJECT
public:
  /**
   * @brief Reimplementation of EffectField::EffectField().
   */
  FileField(EffectRow* parent, const QString& id);

  /**
   * @brief Get the filename at the given timecode
   *
   * A convenience function, equivalent to GetValueAt(timecode).toString()
   *
   * @param timecode
   *
   * The timecode to retrieve the filename at
   *
   * @return
   *
   * The filename at this timecode
   */
  QString GetFileAt(double timecode);

};

#endif // FILEFIELD_H
