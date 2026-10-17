#ifndef PROJECTSAVER_H
#define PROJECTSAVER_H

#include <QString>

class QXmlStreamWriter;

namespace amber {

// Writes the whole project (amber::project_model) as .ove XML to `stream`. Footage URLs are written relative to
// `project_file`'s directory and `project_file` is written as <url>. Assigns save ids on folders, footage and
// sequences as a side effect (clip media references and nests rely on them). Also reads amber::ActiveSequence
// (written as open="1") and amber::CurrentConfig.preview_resolution_divider, and turns on auto-formatting on `stream`.
void save_project_xml(QXmlStreamWriter& stream, const QString& project_file);

// Opens `path` for writing and calls save_project_xml(). Returns false if the file cannot be opened.
// CBA: write errors after a successful open (disk full) are not detected, as before the move; check
// stream.hasError() if a caller ever needs that.
bool save_project_file(const QString& path, const QString& project_file);

}  // namespace amber

#endif  // PROJECTSAVER_H
