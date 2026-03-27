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

#include "footagerelinkdialog.h"

#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

#include "project/footage.h"
#include "project/previewgenerator.h"

FootageRelinkDialog::FootageRelinkDialog(QWidget* parent, QVector<QPair<Media*, Footage*>> invalid_footage)
    : QDialog(parent), footage_(invalid_footage) {
  setWindowTitle(tr("Relink Media"));
  setMinimumWidth(600);

  QVBoxLayout* layout = new QVBoxLayout(this);

  tree_ = new QTreeWidget(this);
  tree_->setHeaderLabels({tr("Filename"), tr("Last Known Path"), tr("Status")});
  tree_->header()->setStretchLastSection(true);

  for (int i = 0; i < footage_.size(); i++) {
    Footage* f = footage_[i].second;
    QTreeWidgetItem* item = new QTreeWidgetItem();
    item->setText(0, QFileInfo(f->url).fileName());
    item->setText(1, f->url);

    QPushButton* browse = new QPushButton(tr("Browse..."), this);
    browse->setProperty("row", i);
    connect(browse, &QPushButton::clicked, this, &FootageRelinkDialog::browse_clicked);

    tree_->addTopLevelItem(item);
    tree_->setItemWidget(item, 2, browse);
  }

  tree_->resizeColumnToContents(0);
  layout->addWidget(tree_);

  QDialogButtonBox* buttons = new QDialogButtonBox(this);
  buttons->addButton(tr("Skip"), QDialogButtonBox::RejectRole);
  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
  layout->addWidget(buttons);
}

void FootageRelinkDialog::browse_clicked() {
  int row = sender()->property("row").toInt();
  Footage* f = footage_[row].second;
  Media* m = footage_[row].first;

  QString path = QFileDialog::getOpenFileName(this, tr("Relink \"%1\"").arg(QFileInfo(f->url).fileName()),
                                              QFileInfo(f->url).absolutePath());
  if (path.isEmpty()) return;

  // Relink this footage
  f->url = path;
  f->invalid = false;
  f->ready = false;
  relinked_any_ = true;

  // Re-trigger media analysis and preview generation
  PreviewGenerator::AnalyzeMedia(m);

  update_row(row);
  try_auto_relink(QFileInfo(path).absolutePath(), row);

  if (all_valid()) accept();
}

void FootageRelinkDialog::try_auto_relink(const QString& directory, int skip_row) {
  QDir dir(directory);
  for (int i = 0; i < footage_.size(); i++) {
    if (i == skip_row || !footage_[i].second->invalid) continue;

    QString filename = QFileInfo(footage_[i].second->url).fileName();
    QString candidate = dir.filePath(filename);
    if (QFileInfo::exists(candidate)) {
      footage_[i].second->url = candidate;
      footage_[i].second->invalid = false;
      footage_[i].second->ready = false;

      // Re-trigger media analysis and preview generation
      PreviewGenerator::AnalyzeMedia(footage_[i].first);

      update_row(i);
    }
  }
}

void FootageRelinkDialog::update_row(int row) {
  QTreeWidgetItem* item = tree_->topLevelItem(row);
  if (!footage_[row].second->invalid) {
    item->setText(1, footage_[row].second->url);
    QLabel* ok = new QLabel(tr("Relinked"), this);
    ok->setStyleSheet("color: green; font-weight: bold;");
    tree_->setItemWidget(item, 2, ok);
  }
}

bool FootageRelinkDialog::all_valid() {
  for (const auto& pair : footage_) {
    if (pair.second->invalid) return false;
  }
  return true;
}
