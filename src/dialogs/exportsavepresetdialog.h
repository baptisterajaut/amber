/***

    Olive - Non-Linear Video Editor
    Copyright (C) 2019  Olive Team

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.

***/

#ifndef EXPORTSAVEPRESETDIALOG_H
#define EXPORTSAVEPRESETDIALOG_H

#include <QDialog>
#include <QLineEdit>
#include <QListWidget>

class ExportSavePresetDialog : public QDialog {
  Q_OBJECT
 public:
  ExportSavePresetDialog(QWidget* parent, const QStringList& existing_presets);
  QString preset_name() const;

 private slots:
  void accept() override;
  void preset_clicked(QListWidgetItem* item);

 private:
  QLineEdit* name_edit_;
  QListWidget* preset_list_;
};

#endif  // EXPORTSAVEPRESETDIALOG_H
