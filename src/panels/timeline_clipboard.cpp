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

#include "timeline.h"

#include <QCheckBox>
#include <QMessageBox>
#include <QPushButton>

#include "engine/undo/undostack.h"
#include "global/config.h"
#include "global/global.h"
#include "panels/panels.h"
#include "project/clipboard.h"
#include "rendering/renderfunctions.h"
#include "ui/mainwindow.h"

#include <QStatusBar>

// Returns true if the clip overlaps the selection on the same track.
static bool clip_overlaps_selection(ClipPtr c, const Selection& s) {
  return s.track == c->track() && !((c->timeline_in() <= s.in && c->timeline_out() <= s.in) ||
                                    (c->timeline_in() >= s.out && c->timeline_out() >= s.out));
}

void Timeline::copy(bool del) {
  if (amber::ActiveSequence == nullptr) return;

  bool cleared = false;
  bool copied = false;
  long min_in = 0;

  for (int i = 0; i < amber::ActiveSequence->clips.size(); i++) {
    ClipPtr c = amber::ActiveSequence->clips.at(i);
    if (c == nullptr) continue;

    for (const auto& s : amber::ActiveSequence->selections) {
      if (!clip_overlaps_selection(c, s)) continue;

      if (!cleared) {
        clear_clipboard();
        cleared = true;
        clipboard_type = CLIPBOARD_TYPE_CLIP;
      }

      ClipPtr copied_clip = c->copy(nullptr);
      copied_clip->linked = c->linked;  // corrected later in paste()

      if (copied_clip->timeline_in() < s.in) {
        copied_clip->set_clip_in(copied_clip->clip_in() + (s.in - copied_clip->timeline_in()));
        copied_clip->set_timeline_in(s.in);
      }
      if (copied_clip->timeline_out() > s.out) {
        copied_clip->set_timeline_out(s.out);
      }

      min_in = copied ? qMin(min_in, s.in) : s.in;
      copied = true;

      copied_clip->load_id = i;
      clipboard.append(copied_clip);
    }
  }

  for (const auto& i : clipboard) {
    ClipPtr c = std::static_pointer_cast<Clip>(i);
    c->set_timeline_in(c->timeline_in() - min_in);
    c->set_timeline_out(c->timeline_out() - min_in);
  }

  if (del && copied) {
    delete_selection(amber::ActiveSequence->selections, false);
  }
}

void Timeline::relink_clips_using_ids(QVector<int>& old_clips, QVector<ClipPtr>& new_clips) {
  if (amber::ActiveSequence == nullptr) return;

  // relink pasted clips
  for (int i = 0; i < old_clips.size(); i++) {
    // these indices should correspond
    ClipPtr oc = amber::ActiveSequence->clips.at(old_clips.at(i));
    for (int j = 0; j < oc->linked.size(); j++) {
      for (int k = 0; k < old_clips.size(); k++) {  // find clip with that ID
        if (oc->linked.at(j) == old_clips.at(k)) {
          if (new_clips.at(i) != nullptr) {
            new_clips.at(i)->linked.append(k);
          }
        }
      }
    }
  }
}

// Correct pasted clip links using the original clipboard load_id mapping.
static void relink_pasted_clips(const QVector<VoidPtr>& clipboard, QVector<ClipPtr>& pasted_clips) {
  for (int i = 0; i < clipboard.size(); i++) {
    ClipPtr oc = std::static_pointer_cast<Clip>(clipboard.at(i));
    for (int j = 0; j < oc->linked.size(); j++) {
      for (int k = 0; k < clipboard.size(); k++) {
        ClipPtr comp = std::static_pointer_cast<Clip>(clipboard.at(k));
        if (comp->load_id == oc->linked.at(j)) {
          pasted_clips.at(i)->linked.append(k);
        }
      }
    }
  }
}

static void paste_clips(Timeline* tl, ComboAction* ca, bool insert) {
  Sequence* seq = amber::ActiveSequence.get();
  QVector<Selection> delete_areas;
  QVector<ClipPtr> pasted_clips;
  long paste_start = LONG_MAX;
  long paste_end = LONG_MIN;

  for (const auto& i : clipboard) {
    ClipPtr c = std::static_pointer_cast<Clip>(i);
    ClipPtr cc = c->copy(seq);

    double src_rate = c->cached_frame_rate();
    double dst_rate = seq->frame_rate;
    cc->set_timeline_in(rescale_frame_number(cc->timeline_in(), src_rate, dst_rate));
    cc->set_timeline_out(rescale_frame_number(cc->timeline_out(), src_rate, dst_rate));
    cc->set_clip_in(rescale_frame_number(cc->clip_in(), src_rate, dst_rate));
    cc->set_timeline_in(cc->timeline_in() + seq->playhead);
    cc->set_timeline_out(cc->timeline_out() + seq->playhead);
    cc->set_track(c->track());

    paste_start = qMin(paste_start, cc->timeline_in());
    paste_end = qMax(paste_end, cc->timeline_out());
    pasted_clips.append(cc);

    if (!insert) {
      Selection s;
      s.in = cc->timeline_in();
      s.out = cc->timeline_out();
      s.track = c->track();
      delete_areas.append(s);
    }
  }

  if (insert) {
    tl->split_cache.clear();
    tl->split_all_clips_at_point(ca, seq->playhead);
    ripple_clips(ca, seq, paste_start, paste_end - paste_start);
  } else {
    tl->delete_areas_and_relink(ca, delete_areas, false);
  }

  relink_pasted_clips(clipboard, pasted_clips);
  ca->append(new AddClipCommand(seq, pasted_clips));
  amber::UndoStack.push(ca);
  update_ui(true);

  if (amber::CurrentConfig.paste_seeks) {
    panel_sequence_viewer->seek(paste_end);
  }
}

static void paste_effects(Timeline* tl, ComboAction* ca) {
  bool replace = false;
  bool skip = false;
  bool ask_conflict = true;

  QVector<Clip*> selected_clips = amber::ActiveSequence->SelectedClips();
  if (selected_clips.isEmpty()) {
    amber::MainWindow->statusBar()->showMessage(tl->tr("Select a clip to paste effects into"), 3000);
    return;
  }
  for (auto c : selected_clips) {
    for (const auto& j : clipboard) {
      EffectPtr e = std::static_pointer_cast<Effect>(j);
      if ((c->track() < 0) != (e->meta->subtype == EFFECT_TYPE_VIDEO)) continue;

      int found = -1;
      if (ask_conflict) {
        replace = false;
        skip = false;
      }
      for (int k = 0; k < c->effects.size(); k++) {
        if (c->effects.at(k)->meta == e->meta) {
          found = k;
          break;
        }
      }

      if (found >= 0 && ask_conflict) {
        QMessageBox box(tl);
        box.setWindowTitle(tl->tr("Effect already exists"));
        box.setText(tl->tr("Clip '%1' already contains a '%2' effect. "
                           "Would you like to replace it with the pasted one or add it as a separate effect?")
                        .arg(c->name(), e->meta->name));
        box.setIcon(QMessageBox::Icon::Question);
        box.addButton(tl->tr("Add"), QMessageBox::YesRole);
        QPushButton* replace_button = box.addButton(tl->tr("Replace"), QMessageBox::NoRole);
        QPushButton* skip_button = box.addButton(tl->tr("Skip"), QMessageBox::RejectRole);
        QCheckBox* future_box = new QCheckBox(tl->tr("Do this for all conflicts found"), &box);
        box.setCheckBox(future_box);
        box.exec();

        if (box.clickedButton() == replace_button)
          replace = true;
        else if (box.clickedButton() == skip_button)
          skip = true;
        ask_conflict = !future_box->isChecked();
      }

      if (found >= 0 && skip) {
        // do nothing
      } else if (found >= 0 && replace) {
        ca->append(new EffectDeleteCommand(c->effects.at(found).get()));
        ca->append(new AddEffectCommand(c, e->copy(c), nullptr, found));
      } else {
        ca->append(new AddEffectCommand(c, e->copy(c), nullptr));
      }
    }
  }
}

void Timeline::paste(bool insert) {
  if (amber::ActiveSequence == nullptr) return;
  if (clipboard.isEmpty()) {
    amber::MainWindow->statusBar()->showMessage(tr("Clipboard is empty"), 3000);
    return;
  }

  if (clipboard_type == CLIPBOARD_TYPE_CLIP) {
    ComboAction* ca = new ComboAction(tr("Paste"));
    paste_clips(this, ca, insert);
  } else if (clipboard_type == CLIPBOARD_TYPE_EFFECT) {
    ComboAction* ca = new ComboAction(tr("Paste Effect(s)"));
    paste_effects(this, ca);
    if (ca->hasActions()) {
      ca->appendPost(new ReloadEffectsCommand());
      amber::UndoStack.push(ca);
    } else {
      delete ca;
    }
    update_ui(true);
  }
}
