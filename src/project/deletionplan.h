#ifndef DELETIONPLAN_H
#define DELETIONPLAN_H

#include <QList>
#include <QVector>

#include "project/media.h"

// Pure planning logic behind Project::delete_selected_media(): what a media delete removes, how a "Skip" spares an
// item, and which clipboard positions to drop. Operates only on the Media tree and lists — no UI, no undo commands.
namespace amber {

// True if a folder above `item` (at any depth) is also in `items`.
bool has_selected_ancestor(Media* item, const QList<Media*>& items);

// Append every footage and sequence inside `folder` to `out`, recursing into subfolders (folders themselves are not
// appended).
void collect_folder_contents(Media* folder, QList<Media*>& out);

// Everything the delete currently removes: `items` plus the contents of selected folders, minus what a Skip kept
// (`kept`: the skipped footage and its ancestor folders, which leave `items` only after the footage checks).
// Deduplicated, in discovery order.
QList<Media*> collect_deleted_media(const QList<Media*>& items, const QVector<Media*>& kept);

// Handle the "skip" action: add item and the ancestor folders being deleted with it to the `parents` exclusion list,
// and re-add the other children of each to `items` so they still get deleted. The walk stops at the first ancestor
// that isn't being deleted (never the hidden root), so nothing outside the delete is touched.
void skip_media_item(Media* item, QList<Media*>& items, QVector<Media*>& parents);

// Positions to pass, in order, to one RemoveClipsFromClipboard each, so that every clipboard clip whose media is in
// `media` is removed. clipboard_media[i] is the media of clipboard clip i (nullptr allowed, never matched).
// Each command stores a raw index into the clipboard as it is when that command runs, i.e. after the previous
// removals, hence the running offset (i - number already removed). All removals must therefore be computed in one
// pass against the same offset; undoing in reverse order re-inserts each clip at its original index.
QVector<int> clipboard_removal_positions(const QVector<Media*>& clipboard_media, const QList<Media*>& media);

}  // namespace amber

#endif  // DELETIONPLAN_H
