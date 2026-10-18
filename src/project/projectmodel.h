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

#ifndef PROJECTMODEL_H
#define PROJECTMODEL_H

#include <QAbstractItemModel>

#include "project/media.h"

class ComboAction;

class ProjectModel : public QAbstractItemModel
{
  Q_OBJECT
public:
  ProjectModel(QObject* parent = nullptr);
  ~ProjectModel() override;

  void make_root();
  void destroy_root();
  void clear();
  [[nodiscard]] Media* get_root() const;
  [[nodiscard]] QVariant data(const QModelIndex &index, int role) const override;
  [[nodiscard]] Qt::ItemFlags flags(const QModelIndex &index) const override;
  [[nodiscard]] QVariant headerData(int section, Qt::Orientation orientation,
                                    int role = Qt::DisplayRole) const override;
  [[nodiscard]] QModelIndex index(int row, int column,
                                  const QModelIndex &parent = QModelIndex()) const override;
  QModelIndex create_index(int arow, int acolumn, void *adata);
  [[nodiscard]] QModelIndex parent(const QModelIndex &index) const override;
  [[nodiscard]] bool canFetchMore(const QModelIndex &parent) const override;
  [[nodiscard]] bool hasChildren(const QModelIndex &parent = QModelIndex()) const override;
  bool setData(const QModelIndex &index, const QVariant &value, int role = Qt::EditRole) override;
  [[nodiscard]] int rowCount(const QModelIndex &parent = QModelIndex()) const override;
  [[nodiscard]] int columnCount(const QModelIndex &parent = QModelIndex()) const override;
  [[nodiscard]] Media* getItem(const QModelIndex &index) const;

  void appendChild(Media *parent, MediaPtr child);
  void moveChild(MediaPtr child, Media* to);
  void removeChild(Media* parent, Media* m);
  Media* child(int i, Media* parent = nullptr);
  int childCount(Media* parent = nullptr);
  void set_icon(Media* m, const QIcon &ico);

private:
  MediaPtr root_item_;
};

namespace amber {
extern ProjectModel project_model;

/**
 * @brief Create a folder item. It is not added to project_model.
 */
MediaPtr create_folder_item(const QString& name);

/**
 * @brief Create an item wrapping sequence s, to live under parent (nullptr = root)
 *
 * With an undo action, adding the item (and opening the sequence if open is true) is appended to ca. Without one, the
 * item is appended to project_model immediately and, if open is true, the sequence becomes the active sequence.
 */
MediaPtr create_sequence_item(ComboAction* ca, SequencePtr s, bool open, Media* parent);

/**
 * @brief The proxy file of every footage in project_model, in any folder, but the footage in skip
 */
QStringList project_proxy_paths(const QVector<Media*>& skip);
}

#endif // PROJECTMODEL_H
