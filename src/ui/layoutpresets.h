#ifndef LAYOUTPRESETS_H
#define LAYOUTPRESETS_H

#include <QByteArray>
#include <QDir>
#include <QPair>
#include <QString>
#include <QVector>

// Window layout files: the "layout" file the main window writes on exit and the user layouts saved from
// Window > Layouts into a "layouts" directory, all in the same XML. Free of QtWidgets so it can be unit-tested:
// MainWindow captures and applies the LayoutData.
// CBA: no rename or layout tab bar; add if asked
namespace amber {

struct LayoutData {
  QString name;                                // "name" attribute of the root, empty in the exit-time "layout" file
  QByteArray state;                            // QMainWindow::saveState(0)
  QVector<QPair<QString, QByteArray>> panels;  // panel objectName() -> Panel::SaveLayoutState(), in panel order
};

struct LayoutEntry {
  QString name;
  QString path;
};

// Characters outside [A-Za-z0-9 _-] become '_': the readable part of a user layout's file name (see layout_file_for)
QString sanitize_layout_name(const QString& name);

bool write_layout_file(const QString& path, const LayoutData& data);

// False if the file can't be opened or parsed, its root isn't <layout>, or it has no <panels> state
bool read_layout_file(const QString& path, LayoutData* data);

// The readable *.xml layouts of dir, sorted by name (case-insensitive). A layout without a name attribute is listed
// under its file base name.
QVector<LayoutEntry> list_layouts(const QDir& dir);

// The file the user layout called `name` is saved to in dir: the layout already stored under that exact name, whatever
// its file is called (older versions used "<sanitized>.xml"), else "<sanitized>-<hash>.xml". The hash of the real name
// keeps names that sanitize alike apart ("Edit!"/"Edit?", accents, CJK, case on Windows/macOS file systems).
QString layout_file_for(const QDir& dir, const QString& name);

}  // namespace amber

#endif  // LAYOUTPRESETS_H
