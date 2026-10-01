#include "project/deletionplan.h"

namespace amber {

bool has_selected_ancestor(Media* item, const QList<Media*>& items) {
  for (Media* p = item->parentItem(); p != nullptr; p = p->parentItem()) {
    if (items.contains(p)) return true;
  }
  return false;
}

void collect_folder_contents(Media* folder, QList<Media*>& out) {
  for (int i = 0; i < folder->childCount(); i++) {
    Media* child = folder->child(i);
    if (child->get_type() == MEDIA_TYPE_FOLDER) {
      collect_folder_contents(child, out);
    } else {
      out.append(child);
    }
  }
}

QList<Media*> collect_deleted_media(const QList<Media*>& items, const QVector<Media*>& kept) {
  QList<Media*> deleted;
  for (auto item : items) {
    if (kept.contains(item)) continue;
    if (!deleted.contains(item)) deleted.append(item);
    if (item->get_type() != MEDIA_TYPE_FOLDER) continue;
    QList<Media*> contents;
    collect_folder_contents(item, contents);
    for (auto m : contents) {
      if (!deleted.contains(m)) deleted.append(m);
    }
  }
  return deleted;
}

void skip_media_item(Media* item, QList<Media*>& items, QVector<Media*>& parents) {
  // Only climb through ancestors that are themselves going away (selected, or inside a selected folder). Stopping at
  // the first one that isn't keeps the walk away from unselected folders and the hidden root: re-adding the root's
  // children would delete every top-level item.
  Media* parent = item;
  while (parent != nullptr && (items.contains(parent) || has_selected_ancestor(parent, items))) {
    parents.append(parent);
    for (int m = 0; m < parent->childCount(); m++) {
      Media* child = parent->child(m);
      bool found = false;
      for (auto existing : items) {
        if (existing == child) {
          found = true;
          break;
        }
      }
      if (!found) {
        items.append(child);
      }
    }
    parent = parent->parentItem();
  }
}

QVector<int> clipboard_removal_positions(const QVector<Media*>& clipboard_media, const QList<Media*>& media) {
  QVector<int> positions;
  int delete_count = 0;
  for (int i = 0; i < clipboard_media.size(); i++) {
    Media* m = clipboard_media.at(i);
    if (m != nullptr && media.contains(m)) {
      // Clip i has already shifted left by one for every clip removed before it
      positions.append(i - delete_count);
      delete_count++;
    }
  }
  return positions;
}

}  // namespace amber
