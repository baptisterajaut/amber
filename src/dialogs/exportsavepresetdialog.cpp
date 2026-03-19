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

#include "exportsavepresetdialog.h"

#include <QDialogButtonBox>
#include <QLabel>
#include <QMessageBox>
#include <QVBoxLayout>

ExportSavePresetDialog::ExportSavePresetDialog(QWidget* parent, const QStringList& existing_presets) : QDialog(parent) {
  setWindowTitle(tr("Save Export Preset"));

  QVBoxLayout* layout = new QVBoxLayout(this);

  layout->addWidget(new QLabel(tr("Preset Name:"), this));
  name_edit_ = new QLineEdit(this);
  layout->addWidget(name_edit_);

  if (!existing_presets.isEmpty()) {
    layout->addWidget(new QLabel(tr("Existing Presets:"), this));
    preset_list_ = new QListWidget(this);
    preset_list_->addItems(existing_presets);
    connect(preset_list_, &QListWidget::itemClicked, this, &ExportSavePresetDialog::preset_clicked);
    layout->addWidget(preset_list_);
  } else {
    preset_list_ = nullptr;
  }

  QDialogButtonBox* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
  connect(buttons, &QDialogButtonBox::accepted, this, &ExportSavePresetDialog::accept);
  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
  layout->addWidget(buttons);
}

QString ExportSavePresetDialog::preset_name() const { return name_edit_->text().trimmed(); }

void ExportSavePresetDialog::preset_clicked(QListWidgetItem* item) { name_edit_->setText(item->text()); }

void ExportSavePresetDialog::accept() {
  QString name = name_edit_->text().trimmed();
  if (name.isEmpty()) {
    QMessageBox::warning(this, tr("Error"), tr("Please enter a preset name."));
    return;
  }

  if (preset_list_ != nullptr) {
    for (int i = 0; i < preset_list_->count(); i++) {
      if (preset_list_->item(i)->text() == name) {
        if (QMessageBox::question(this, tr("Overwrite Preset"),
                                  tr("A preset named \"%1\" already exists. Overwrite?").arg(name),
                                  QMessageBox::Yes | QMessageBox::No) != QMessageBox::Yes) {
          return;
        }
        break;
      }
    }
  }

  QDialog::accept();
}
