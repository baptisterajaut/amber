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

// Writes save_project_xml() to `path` through a QSaveFile. Returns false if the file cannot be written (open,
// write or rename failure); the previous file at `path` is then left untouched.
bool save_project_file(const QString& path, const QString& project_file);

}  // namespace amber

#endif  // PROJECTSAVER_H
