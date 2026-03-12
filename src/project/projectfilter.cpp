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

#include "projectfilter.h"

#include "project/media.h"
#include "timeline/sequence.h"

#include <QDebug>

ProjectFilter::ProjectFilter(QObject *parent) :
  QSortFilterProxyModel(parent)
  
{}

bool ProjectFilter::get_show_sequences() {
  return show_sequences;
}

void ProjectFilter::set_show_sequences(bool b) {
#if QT_VERSION >= QT_VERSION_CHECK(6, 9, 0)
  beginFilterChange();
  show_sequences = b;
  endFilterChange();
#else
  show_sequences = b;
  invalidateFilter();
#endif
}

void ProjectFilter::update_search_filter(const QString &s) {
#if QT_VERSION >= QT_VERSION_CHECK(6, 9, 0)
  beginFilterChange();
  search_filter = s;
  endFilterChange();
#else
  search_filter = s;
  invalidateFilter();
#endif
}

bool ProjectFilter::filterAcceptsRow(int source_row, const QModelIndex &source_parent) const {
    // retrieve media object from index
    QModelIndex index = sourceModel()->index(source_row, 0, source_parent);
    Media* media = static_cast<Media*>(index.internalPointer());

    // hide sequences if show_sequences is false
  if (!show_sequences) {
    if (media != nullptr && media->get_type() == MEDIA_TYPE_SEQUENCE) {
      return false;
    }
  }

    // filter by search filter string
    if (!search_filter.isEmpty()) {
        // search markers if media is a sequene
        bool marker_contains_search = false;

        if (media->get_type() == MEDIA_TYPE_SEQUENCE
                || media->get_type() == MEDIA_TYPE_FOOTAGE) {
            QVector<Marker>& markers = media->get_markers();
            for (const auto & marker : markers) {
                if (marker.name.contains(search_filter, Qt::CaseInsensitive)) {
                    marker_contains_search = true;
                    break;
                }
            }
        }

        // hide any rows that don't contain the search string (unless it's a folder)
        if (!marker_contains_search
                && media->get_type() != MEDIA_TYPE_FOLDER
                && !media->get_name().contains(search_filter, Qt::CaseInsensitive)) {
            return false;
        }
    }

  return QSortFilterProxyModel::filterAcceptsRow(source_row, source_parent);
}
