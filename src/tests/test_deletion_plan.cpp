#include <QtTest>

#include "project/deletionplan.h"
#include "project/media.h"

namespace {

// Media trees are built without ProjectModel: a hidden root (root = true, no parent) like ProjectModel's, and
// children attached with appendChild(), which also sets the parent pointer. get_type() only reads the type set by
// set_folder() / set_footage() / set_sequence(), so footage and sequences carry no payload (nullptr object).
struct Tree {
  MediaPtr root;

  Tree() : root(std::make_shared<Media>()) { root->root = true; }

  Media* add(Media* parent, int type) {
    MediaPtr m = std::make_shared<Media>();
    if (type == MEDIA_TYPE_FOLDER) {
      m->set_folder();
    } else if (type == MEDIA_TYPE_SEQUENCE) {
      m->set_sequence(nullptr);
    } else {
      m->set_footage(nullptr);
    }
    (parent != nullptr ? parent : root.get())->appendChild(m);
    return m.get();
  }
  Media* folder(Media* parent = nullptr) { return add(parent, MEDIA_TYPE_FOLDER); }
  Media* footage(Media* parent = nullptr) { return add(parent, MEDIA_TYPE_FOOTAGE); }
  Media* sequence(Media* parent = nullptr) { return add(parent, MEDIA_TYPE_SEQUENCE); }
};

// The final delete list exactly as Project::delete_selected_media() computes it after the footage checks:
// items with every skipped entry ("parents") removed.
QList<Media*> final_delete_list(QList<Media*> items, const QVector<Media*>& parents) {
  for (auto parent : parents) {
    for (int l = 0; l < items.size(); l++) {
      if (items.at(l) == parent) {
        items.removeAt(l);
        l--;
      }
    }
  }
  return items;
}

}  // namespace

class TestDeletionPlan : public QObject {
  Q_OBJECT
 private slots:
  void hasSelectedAncestor();
  void collectFolderContents();
  void collectDeletedMedia();
  void skipTopLevel();
  void skipInsideUnselectedFolder();
  void skipInsideSelectedFolder();
  void skipInsideNestedSelectedFolder();
  void skipTwiceInSameFolder();
  void clipboardRemovalPositions();
  void clipboardRemovalUndoInvariant();
};

void TestDeletionPlan::hasSelectedAncestor() {
  Tree t;
  Media* f = t.folder();
  Media* g = t.folder(f);
  Media* a = t.footage(g);
  Media* top = t.footage();

  QVERIFY(amber::has_selected_ancestor(a, {g}));          // direct parent
  QVERIFY(amber::has_selected_ancestor(a, {f}));          // grandparent
  QVERIFY(!amber::has_selected_ancestor(a, {a}));         // only itself: strict ancestors only
  QVERIFY(!amber::has_selected_ancestor(top, {top, f}));  // top-level: only the hidden root is above it
  // The walk passes through the hidden root (never selectable, so never in the list) and finds nothing
  QVERIFY(!amber::has_selected_ancestor(f, {a, g, top}));
  QVERIFY(!amber::has_selected_ancestor(t.root.get(), {f, a}));
}

void TestDeletionPlan::collectFolderContents() {
  Tree t;
  Media* f = t.folder();
  Media* a = t.footage(f);
  Media* g = t.folder(f);
  Media* b = t.footage(g);
  Media* h = t.folder(g);
  Media* s = t.sequence(h);
  Media* s2 = t.sequence(f);
  t.folder(f);  // empty subfolder
  Media* outside = t.footage();

  QList<Media*> out;
  amber::collect_folder_contents(f, out);
  QCOMPARE(out, (QList<Media*>{a, b, s, s2}));  // depth-first, folders excluded
  QVERIFY(!out.contains(outside));

  // Appends to whatever is already in `out`
  QList<Media*> pre{outside};
  amber::collect_folder_contents(h, pre);
  QCOMPARE(pre, (QList<Media*>{outside, s}));
}

void TestDeletionPlan::collectDeletedMedia() {
  // Folder and its child both selected: the child appears once
  {
    Tree t;
    Media* f = t.folder();
    Media* a = t.footage(f);
    Media* b = t.footage(f);
    QCOMPARE(amber::collect_deleted_media({f, a}, {}), (QList<Media*>{f, a, b}));
  }
  // Nested selected folders: shared contents deduped
  {
    Tree t;
    Media* f = t.folder();
    Media* g = t.folder(f);
    Media* b = t.footage(g);
    QCOMPARE(amber::collect_deleted_media({f, g}, {}), (QList<Media*>{f, b, g}));
  }
  // After a Skip: the kept folder (and the skipped item) are excluded, the re-added sibling is present
  {
    Tree t;
    Media* f = t.folder();
    Media* a = t.footage(f);
    Media* c = t.footage(f);
    QList<Media*> items{f, a};
    QVector<Media*> parents;
    amber::skip_media_item(a, items, parents);
    QList<Media*> deleted = amber::collect_deleted_media(items, parents);
    QCOMPARE(deleted, (QList<Media*>{c}));
    QVERIFY(!deleted.contains(f));
    QVERIFY(!deleted.contains(a));
  }
}

void TestDeletionPlan::skipTopLevel() {
  // Regression: the old walk reached the hidden root and re-added every top-level item, so C got deleted too
  Tree t;
  Media* a = t.footage();
  Media* b = t.footage();
  Media* c = t.footage();
  QList<Media*> items{a, b};
  QVector<Media*> parents;
  amber::skip_media_item(a, items, parents);

  QCOMPARE(parents, (QVector<Media*>{a}));
  QList<Media*> result = final_delete_list(items, parents);
  QCOMPARE(result, (QList<Media*>{b}));
  QVERIFY(!result.contains(c));
  QVERIFY(!items.contains(c));
}

void TestDeletionPlan::skipInsideUnselectedFolder() {
  Tree t;
  Media* f = t.folder();
  Media* a = t.footage(f);
  Media* e = t.footage(f);
  Media* b = t.footage();
  QList<Media*> items{a, b};
  QVector<Media*> parents;
  amber::skip_media_item(a, items, parents);

  QCOMPARE(parents, (QVector<Media*>{a}));  // stops at F: not being deleted
  QCOMPARE(final_delete_list(items, parents), (QList<Media*>{b}));
  QVERIFY(!items.contains(e));
  QVERIFY(!items.contains(f));
}

void TestDeletionPlan::skipInsideSelectedFolder() {
  Tree t;
  Media* f = t.folder();
  Media* a = t.footage(f);
  Media* c = t.footage(f);
  QList<Media*> items{f, a};
  QVector<Media*> parents;
  amber::skip_media_item(a, items, parents);

  QCOMPARE(parents, (QVector<Media*>{a, f}));
  QCOMPARE(final_delete_list(items, parents), (QList<Media*>{c}));
}

void TestDeletionPlan::skipInsideNestedSelectedFolder() {
  Tree t;
  Media* f = t.folder();
  Media* g = t.folder(f);
  Media* a = t.footage(g);
  Media* d = t.footage(g);
  Media* x = t.footage(f);
  Media* other = t.footage();  // unselected top-level item, must stay untouched
  QList<Media*> items{f, a};
  QVector<Media*> parents;
  amber::skip_media_item(a, items, parents);

  // G is not selected but lies inside selected F, so the walk climbs through it and stops at the root
  QCOMPARE(parents, (QVector<Media*>{a, g, f}));
  QList<Media*> result = final_delete_list(items, parents);
  QCOMPARE(result, (QList<Media*>{d, x}));
  QVERIFY(!items.contains(other));
}

void TestDeletionPlan::skipTwiceInSameFolder() {
  Tree t;
  Media* f = t.folder();
  Media* g = t.folder(f);
  Media* a = t.footage(g);
  Media* d = t.footage(g);
  Media* x = t.footage(f);
  QList<Media*> items{f, a};
  QVector<Media*> parents;
  amber::skip_media_item(a, items, parents);
  amber::skip_media_item(d, items, parents);

  QList<Media*> result = final_delete_list(items, parents);
  QCOMPARE(result, (QList<Media*>{x}));
  QCOMPARE(amber::collect_deleted_media(items, parents), (QList<Media*>{x}));
}

void TestDeletionPlan::clipboardRemovalPositions() {
  Tree t;
  Media* m1 = t.footage();
  Media* m2 = t.sequence();
  Media* m3 = t.footage();
  const QVector<Media*> clip{m1, m2, m1};  // X(m1), Y(m2), Z(m1)

  QCOMPARE(amber::clipboard_removal_positions(clip, {m1}), (QVector<int>{0, 1}));
  QCOMPARE(amber::clipboard_removal_positions(clip, {m1, m2}), (QVector<int>{0, 0, 0}));
  QCOMPARE(amber::clipboard_removal_positions({nullptr, m1, nullptr, m1}, {m1}), (QVector<int>{1, 2}));
  QVERIFY(amber::clipboard_removal_positions({nullptr, nullptr}, {m1, m2}).isEmpty());
  QVERIFY(amber::clipboard_removal_positions(clip, {m3}).isEmpty());
  QVERIFY(amber::clipboard_removal_positions(clip, {}).isEmpty());
  QVERIFY(amber::clipboard_removal_positions({}, {m1}).isEmpty());
}

void TestDeletionPlan::clipboardRemovalUndoInvariant() {
  Tree t;
  Media* m1 = t.footage();
  Media* m2 = t.sequence();
  Media* m3 = t.footage();
  const QList<QList<Media*>> removals{{m1}, {m1, m2}, {m1, m3}, {m2}, {m1, m2, m3}, {}};
  const QVector<Media*> clipboard_media{m1, m2, m1, nullptr, m3, m1, m2};

  for (const auto& media : removals) {
    const QVector<int> positions = amber::clipboard_removal_positions(clipboard_media, media);

    // Redo: each RemoveClipsFromClipboard does removeAt(pos) on the clipboard as left by the previous ones
    QVector<Media*> clipboard = clipboard_media;
    QVector<Media*> removed;
    for (int pos : positions) {
      QVERIFY(pos >= 0 && pos < clipboard.size());
      removed.append(clipboard.takeAt(pos));
    }
    QVector<Media*> expected;
    for (auto m : clipboard_media) {
      if (m == nullptr || !media.contains(m)) expected.append(m);
    }
    QCOMPARE(clipboard, expected);
    for (auto m : removed) QVERIFY(media.contains(m));

    // Undo: the ComboAction undoes in reverse order, each insert(pos, clip) restores the original order
    for (int k = positions.size() - 1; k >= 0; k--) {
      clipboard.insert(positions.at(k), removed.at(k));
    }
    QCOMPARE(clipboard, clipboard_media);
  }
}

QTEST_GUILESS_MAIN(TestDeletionPlan)
#include "test_deletion_plan.moc"
