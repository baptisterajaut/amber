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

#include "global/global.h"
#include "panels/panels.h"
#include "engine/undo/undostack.h"

void Timeline::split_clip_at_positions(ComboAction* ca, int clip_index, QVector<long> positions) {

  QVector<int> pre_splits;

  // Add the clip and each of its links to the pre_splits array
  Clip* clip = amber::ActiveSequence->clips.at(clip_index).get();
  pre_splits.append(clip_index);
  for (int i : clip->linked)   {
    pre_splits.append(i);
  }

  std::sort(positions.begin(), positions.end());

  // Remove any duplicate positions
  for (int i=1;i<positions.size();i++) {
    if (positions.at(i-1) == positions.at(i)) {
      positions.removeAt(i);
      i--;
    }
  }

  for (int i=1;i<positions.size();i++) {
    Q_ASSERT(positions.at(i-1) < positions.at(i));
  }

  QVector< QVector<ClipPtr> > post_splits(positions.size());

  for (int i=positions.size()-1;i>=0;i--) {

    post_splits[i].resize(pre_splits.size());

    for (int j=0;j<pre_splits.size();j++) {
      post_splits[i][j] = split_clip(ca, true, pre_splits.at(j), positions.at(i));

      if (post_splits[i][j] != nullptr && i + 1 < positions.size()) {
        post_splits[i][j]->set_timeline_out(positions.at(i+1));
      }
    }
  }

  for (auto & post_split : post_splits) {
    relink_clips_using_ids(pre_splits, post_split);
    ca->append(new AddClipCommand(amber::ActiveSequence.get(), post_split));
  }

}

ClipPtr Timeline::split_clip(ComboAction* ca, bool transitions, int p, long frame) {
  return split_clip(ca, transitions, p, frame, frame);
}

ClipPtr Timeline::split_clip(ComboAction* ca, bool transitions, int p, long frame, long post_in) {
  Clip* pre = amber::ActiveSequence->clips.at(p).get();
  if (pre != nullptr) {

    if (pre->timeline_in() < frame && pre->timeline_out() > frame) {
      // duplicate clip without duplicating its transitions, we'll restore them later

      ClipPtr post = pre->copy(amber::ActiveSequence.get());

      long new_clip_length = frame - pre->timeline_in();

      post->set_timeline_in(post_in);
      post->set_clip_in(pre->clip_in() + (post->timeline_in() - pre->timeline_in()));

      pre->move(ca, pre->timeline_in(), frame, pre->clip_in(), pre->track(), false);

      if (transitions) {

        // check if this clip has a closing transition
        if (pre->closing_transition != nullptr) {

          // if so, move closing transition to the post clip
          post->closing_transition = pre->closing_transition;

          // and set the original clip's closing transition to nothing
          ca->append(new SetPointer(reinterpret_cast<void**>(&pre->closing_transition), nullptr));

          // and set the transition's reference to the post clip
          if (post->closing_transition->parent_clip == pre) {
            ca->append(new SetPointer(reinterpret_cast<void**>(&post->closing_transition->parent_clip), post.get()));
          }
          if (post->closing_transition->secondary_clip == pre) {
            ca->append(new SetPointer(reinterpret_cast<void**>(&post->closing_transition->secondary_clip), post.get()));
          }

          // and make sure it's at the correct size to the closing clip
          if (post->closing_transition != nullptr && post->closing_transition->get_true_length() > post->length()) {
            ca->append(new ModifyTransitionCommand(post->closing_transition, post->length()));
            post->closing_transition->set_length(post->length());
          }

        }

        // we're keeping the opening clip, so ensure that's a correct size too
        if (pre->opening_transition != nullptr && pre->opening_transition->get_true_length() > new_clip_length) {
          ca->append(new ModifyTransitionCommand(pre->opening_transition, new_clip_length));
        }
      }

      return post;

    } else if (frame == pre->timeline_in()
               && pre->opening_transition != nullptr
               && pre->opening_transition->secondary_clip != nullptr) {
      // special case for shared transitions to split it into two

      // set transition to single-clip mode
      ca->append(new SetPointer(reinterpret_cast<void**>(&pre->opening_transition->secondary_clip), nullptr));

      // clone transition for other clip
      ca->append(new AddTransitionCommand(nullptr,
                                          pre->opening_transition->secondary_clip,
                                          pre->opening_transition,
                                          nullptr,
                                          0)
                 );

    }

  }
  return nullptr;
}

bool Timeline::split_clip_and_relink(ComboAction *ca, int clip, long frame, bool relink) {
  // see if we split this clip before
  if (split_cache.contains(clip)) {
    return false;
  }

  split_cache.append(clip);

  Clip* c = amber::ActiveSequence->clips.at(clip).get();
  if (c != nullptr) {
    QVector<int> pre_clips;
    QVector<ClipPtr> post_clips;

    ClipPtr post = split_clip(ca, true, clip, frame);

    if (post == nullptr) {
      return false;
    } else {
      post_clips.append(post);

      // if alt is not down, split clips links too
      if (relink) {
        pre_clips.append(clip);

        bool original_clip_is_selected = c->IsSelected();

        // find linked clips of old clip
        for (int l : c->linked) {
          if (!split_cache.contains(l)) {
            Clip* link = amber::ActiveSequence->clips.at(l).get();
            if ((original_clip_is_selected && link->IsSelected()) || !original_clip_is_selected) {
              split_cache.append(l);
              ClipPtr s = split_clip(ca, true, l, frame);
              if (s != nullptr) {
                pre_clips.append(l);
                post_clips.append(s);
              }
            }
          }
        }

        relink_clips_using_ids(pre_clips, post_clips);
      }
      ca->append(new AddClipCommand(amber::ActiveSequence.get(), post_clips));
      return true;
    }
  }
  return false;
}

bool selection_contains_transition(const Selection& s, Clip* c, int type) {
  if (type == kTransitionOpening) {
    return c->opening_transition != nullptr
        && s.out == c->timeline_in() + c->opening_transition->get_true_length()
        && ((c->opening_transition->secondary_clip == nullptr && s.in == c->timeline_in())
            || (c->opening_transition->secondary_clip != nullptr && s.in == c->timeline_in() - c->opening_transition->get_true_length()));
  } else {
    return c->closing_transition != nullptr
        && s.in == c->timeline_out() - c->closing_transition->get_true_length()
        && ((c->closing_transition->secondary_clip == nullptr && s.out == c->timeline_out())
            || (c->closing_transition->secondary_clip != nullptr && s.out == c->timeline_out() + c->closing_transition->get_true_length()));
  }
}

void Timeline::clean_up_selections(QVector<Selection>& areas) {
  for (int i=0;i<areas.size();i++) {
    Selection& s = areas[i];
    for (int j=0;j<areas.size();j++) {
      if (i != j) {
        Selection& ss = areas[j];
        if (s.track == ss.track) {
          bool remove = false;
          if (s.in < ss.in && s.out > ss.out) {
            // do nothing
          } else if (s.in >= ss.in && s.out <= ss.out) {
            remove = true;
          } else if (s.in <= ss.out && s.out > ss.out) {
            ss.out = s.out;
            remove = true;
          } else if (s.out >= ss.in && s.in < ss.in) {
            ss.in = s.in;
            remove = true;
          }
          if (remove) {
            areas.removeAt(i);
            i--;
            break;
          }
        }
      }
    }
  }
}

bool Timeline::split_selection(ComboAction* ca) {
  bool split = false;

  // temporary relinking vectors
  QVector<int> pre_splits;
  QVector<ClipPtr> post_splits;
  QVector<ClipPtr> secondary_post_splits;

  // find clips within selection and split
  for (int j=0;j<amber::ActiveSequence->clips.size();j++) {
    ClipPtr clip = amber::ActiveSequence->clips.at(j);
    if (clip != nullptr) {
      for (const auto & s : amber::ActiveSequence->selections) {
        if (s.track == clip->track()) {
          ClipPtr post_b = split_clip(ca, true, j, s.out);
          ClipPtr post_a = split_clip(ca, true, j, s.in);

          pre_splits.append(j);
          post_splits.append(post_a);
          secondary_post_splits.append(post_b);

          if (post_a != nullptr) {
            post_a->set_timeline_out(qMin(post_a->timeline_out(), s.out));
          }

          split = true;
        }
      }
    }
  }

  if (split) {
    // relink after splitting
    relink_clips_using_ids(pre_splits, post_splits);
    relink_clips_using_ids(pre_splits, secondary_post_splits);

    ca->append(new AddClipCommand(amber::ActiveSequence.get(), post_splits));
    ca->append(new AddClipCommand(amber::ActiveSequence.get(), secondary_post_splits));

    return true;
  }
  return false;
}

bool Timeline::split_all_clips_at_point(ComboAction* ca, long point) {
  bool split = false;
  for (int j=0;j<amber::ActiveSequence->clips.size();j++) {
    ClipPtr c = amber::ActiveSequence->clips.at(j);
    if (c != nullptr) {
      // always relinks
      if (split_clip_and_relink(ca, j, point, true)) {
        split = true;
      }
    }
  }
  return split;
}

void Timeline::split_at_playhead() {
  ComboAction* ca = new ComboAction(tr("Split Clip(s)"));
  bool split_selected = false;
  split_cache.clear();

  if (amber::ActiveSequence->selections.size() > 0) {
    // see if whole clips are selected
    QVector<int> pre_clips;
    QVector<ClipPtr> post_clips;
    for (int j=0;j<amber::ActiveSequence->clips.size();j++) {
      Clip* clip = amber::ActiveSequence->clips.at(j).get();
      if (clip != nullptr && clip->IsSelected()) {
        ClipPtr s = split_clip(ca, true, j, amber::ActiveSequence->playhead);
        if (s != nullptr) {
          pre_clips.append(j);
          post_clips.append(s);
          split_selected = true;
        }
      }
    }

    if (split_selected) {
      // relink clips if we split
      relink_clips_using_ids(pre_clips, post_clips);
      ca->append(new AddClipCommand(amber::ActiveSequence.get(), post_clips));
    } else {
      // split a selection if not
      split_selected = split_selection(ca);
    }
  }

  // if nothing was selected or no selections fell within playhead, simply split at playhead
  if (!split_selected) {
    split_selected = split_all_clips_at_point(ca, amber::ActiveSequence->playhead);
  }

  if (split_selected) {
    amber::UndoStack.push(ca);
    update_ui(true);
  } else {
    delete ca;
  }
}
