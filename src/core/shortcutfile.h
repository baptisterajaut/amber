#ifndef CORE_SHORTCUTFILE_H
#define CORE_SHORTCUTFILE_H

#include <QByteArray>
#include <QString>

namespace amber {

// Look up `action_name` in an exported keyboard shortcut file (one "<action id>\t<key sequence>" per line).
// A line matches only if it starts with the exact action id followed by a tab. Sets *found (when non-null) and
// returns the key sequence text, without a trailing '\r' from CRLF files.
QString find_shortcut_in_file(const QByteArray& file, const QString& action_name, bool* found);

// Appends the "<action id>\t<key sequence>" line to a shortcut file. An empty key sequence (shortcut cleared) is
// written too, as "<action id>\t".
void append_shortcut_line(QByteArray& file, const QString& action_name, const QString& shortcut);

}  // namespace amber

#endif  // CORE_SHORTCUTFILE_H
