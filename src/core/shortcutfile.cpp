#include "shortcutfile.h"

namespace amber {

QString find_shortcut_in_file(const QByteArray& file, const QString& action_name, bool* found) {
  if (found) *found = false;
  const QByteArray name = action_name.toUtf8();
  if (name.isEmpty()) return QString();

  // The first occurrence can be a prefix of a longer id ("export" in "exportframe") or sit mid-line:
  // keep scanning until one starts a line and is followed by the tab separator.
  qsizetype from = 0;
  while (true) {
    const qsizetype index = file.indexOf(name, from);
    if (index < 0) return QString();

    const qsizetype after = index + name.size();
    const bool at_line_start = (index == 0 || file.at(index - 1) == '\n');
    const bool followed_by_tab = (after < file.size() && file.at(after) == '\t');
    if (at_line_start && followed_by_tab) {
      qsizetype end = file.indexOf('\n', after + 1);
      if (end < 0) end = file.size();
      QByteArray value = file.mid(after + 1, end - after - 1);
      if (value.endsWith('\r')) value.chop(1);  // CRLF file
      if (found) *found = true;
      return QString::fromUtf8(value);  // save_shortcut_file writes UTF-8
    }
    from = index + 1;
  }
}

}  // namespace amber
