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

// Characters outside [A-Za-z0-9 _-] become '_'. The file of a user layout is "<sanitized>.xml".
QString sanitize_layout_name(const QString& name);

bool write_layout_file(const QString& path, const LayoutData& data);

// False if the file can't be opened or parsed, its root isn't <layout>, or it has no <panels> state
bool read_layout_file(const QString& path, LayoutData* data);

// The readable *.xml layouts of dir, sorted by name (case-insensitive). A layout without a name attribute is listed
// under its file base name.
QVector<LayoutEntry> list_layouts(const QDir& dir);

}  // namespace amber

#endif  // LAYOUTPRESETS_H
