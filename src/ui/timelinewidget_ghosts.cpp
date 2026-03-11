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

#include "timelinewidget.h"

#include <QToolTip>

#include "global/config.h"
#include "panels/panels.h"
#include "rendering/renderfunctions.h"

void validate_transitions(Clip* c, int transition_type, long& frame_diff) {
  long validator;

  if (transition_type == kTransitionOpening) {
    // prevent from going below 0 on the timeline
    validator = c->timeline_in() + frame_diff;
    if (validator < 0) frame_diff -= validator;

    // prevent from going below 0 for the media
    validator = c->clip_in() + frame_diff;
    if (validator < 0) frame_diff -= validator;

    // prevent transition from exceeding media length
    validator -= c->media_length();
    if (validator > 0) frame_diff -= validator;
  } else {
    // prevent from going below 0 on the timeline
    validator = c->timeline_out() + frame_diff;
    if (validator < 0) frame_diff -= validator;

    // prevent from going below 0 for the media
    validator = c->clip_in() + c->length() + frame_diff;
    if (validator < 0) frame_diff -= validator;

    // prevent transition from exceeding media length
    validator -= c->media_length();
    if (validator > 0) frame_diff -= validator;
  }
}

void make_room_for_transition(ComboAction* ca,
                              Clip* c,
                              int type,
                              long transition_start,
                              long transition_end,
                              bool delete_old_transitions,
                              long timeline_in = -1,
                              long timeline_out = -1) {
  // it's possible to specify other in/out points for the clip, but default behavior is to use the ones existing
  if (timeline_in < 0) {
    timeline_in = c->timeline_in();
  }
  if (timeline_out < 0) {
    timeline_out = c->timeline_out();
  }

  // make room for transition
  if (type == kTransitionOpening) {
    if (delete_old_transitions && c->opening_transition != nullptr) {
      ca->append(new DeleteTransitionCommand(c->opening_transition));
    }
    if (c->closing_transition != nullptr) {
      if (transition_end >= c->timeline_out()) {
        ca->append(new DeleteTransitionCommand(c->closing_transition));
      } else if (transition_end > c->timeline_out() - c->closing_transition->get_true_length()) {
        ca->append(new ModifyTransitionCommand(c->closing_transition, c->timeline_out() - transition_end));
      }
    }
  } else {
    if (delete_old_transitions && c->closing_transition != nullptr) {
      ca->append(new DeleteTransitionCommand(c->closing_transition));
    }
    if (c->opening_transition != nullptr) {
      if (transition_start <= c->timeline_in()) {
        ca->append(new DeleteTransitionCommand(c->opening_transition));
      } else if (transition_start < c->timeline_in() + c->opening_transition->get_true_length()) {
        ca->append(new ModifyTransitionCommand(c->opening_transition, transition_start - c->timeline_in()));
      }
    }
  }
}

void VerifyTransitionsAfterCreating(ComboAction* ca, Clip* open, Clip* close, long transition_start, long transition_end) {
  // in case the user made the transition larger than the clips, we're going to delete everything under
  // the transition ghost and extend the clips to the transition's coordinates as necessary

  if (open == nullptr && close == nullptr) {
    qWarning() << "VerifyTransitionsAfterCreating() called with two null clips";
    return;
  }

  // determine whether this is a "shared" transition between to clips or not
  bool shared_transition = (open != nullptr && close != nullptr);

  int track = 0;

  // first we set the clips to "undeletable" so they aren't affected by delete_areas_and_relink()
  if (open != nullptr) {
    open->undeletable = true;
    track = open->track();
  }
  if (close != nullptr) {
    close->undeletable = true;
    track = close->track();
  }

  // set the area to delete to the transition's coordinates and clear it
  QVector<Selection> areas;
  Selection s;
  s.in = transition_start;
  s.out = transition_end;
  s.track = track;
  areas.append(s);
  panel_timeline->delete_areas_and_relink(ca, areas, false);

  // set the clips back to undeletable now that we're done
  if (open != nullptr) {
    open->undeletable = false;
  }
  if (close != nullptr) {
    close->undeletable = false;
  }

  // loop through both kinds of transition
  for (int t=kTransitionOpening;t<=kTransitionClosing;t++) {

    Clip* clip_ref = (t == kTransitionOpening) ? open : close;

    // if we have an opening transition:
    if (clip_ref != nullptr) {

      // make_room_for_transition will adjust the opposite transition to make space for this one,
      // for example if the user makes an opening transition that overlaps the closing transition, it'll resize
      // or even delete the closing transition if necessary (and vice versa)

      make_room_for_transition(ca, clip_ref, t, transition_start, transition_end, true);

      // check if the transition coordinates require the clip to be resized
      if (transition_start < clip_ref->timeline_in() || transition_end > clip_ref->timeline_out()) {

        long new_in, new_out;

        if (t == kTransitionOpening) {

          // if the transition is shared, it doesn't matter if the transition extend beyond the in point since
          // that'll be "absorbed" by the other clip
          new_in = (shared_transition) ? open->timeline_in() : qMin(transition_start, open->timeline_in());

          new_out = qMax(transition_end, open->timeline_out());

        } else {

          new_in = qMin(transition_start, close->timeline_in());

          // if the transition is shared, it doesn't matter if the transition extend beyond the out point since
          // that'll be "absorbed" by the other clip
          new_out = (shared_transition) ? close->timeline_out() : qMax(transition_end, close->timeline_out());

        }



        clip_ref->move(ca,
                       new_in,
                       new_out,
                       clip_ref->clip_in() - (clip_ref->timeline_in() - new_in),
                       clip_ref->track());
      }
    }
  }
}

void TimelineWidget::init_ghosts() {
  for (int i=0;i<panel_timeline->ghosts.size();i++) {
    Ghost& g = panel_timeline->ghosts[i];
    ClipPtr c = olive::ActiveSequence->clips.at(g.clip);

    g.track = g.old_track = c->track();
    g.clip_in = g.old_clip_in = c->clip_in();

    if (panel_timeline->tool == TIMELINE_TOOL_SLIP) {
      g.clip_in = g.old_clip_in = c->clip_in(true);
      g.in = g.old_in = c->timeline_in(true);
      g.out = g.old_out = c->timeline_out(true);
      g.ghost_length = g.old_out - g.old_in;
    } else if (g.transition == nullptr) {
      // this ghost is for a clip
      g.in = g.old_in = c->timeline_in();
      g.out = g.old_out = c->timeline_out();
      g.ghost_length = g.old_out - g.old_in;
    } else if (g.transition == c->opening_transition) {
      g.in = g.old_in = c->timeline_in(true);
      g.ghost_length = c->opening_transition->get_length();
      g.out = g.old_out = g.in + g.ghost_length;
    } else if (g.transition == c->closing_transition) {
      g.out = g.old_out = c->timeline_out(true);
      g.ghost_length = c->closing_transition->get_length();
      g.in = g.old_in = g.out - g.ghost_length;
      g.clip_in = g.old_clip_in = c->clip_in() + c->length() - c->closing_transition->get_true_length();
    }

    // used for trim ops
    g.media_length = c->media_length();
  }
  for (int i=0;i<olive::ActiveSequence->selections.size();i++) {
    Selection& s = olive::ActiveSequence->selections[i];
    s.old_in = s.in;
    s.old_out = s.out;
    s.old_track = s.track;
  }
}

void TimelineWidget::update_ghosts(const QPoint& mouse_pos, bool lock_frame) {
  int effective_tool = panel_timeline->tool;
  if (panel_timeline->importing || panel_timeline->creating) effective_tool = TIMELINE_TOOL_POINTER;

  int mouse_track = getTrackFromScreenPoint(mouse_pos.y());
  long frame_diff = (lock_frame) ? 0 : panel_timeline->getTimelineFrameFromScreenPoint(mouse_pos.x()) - panel_timeline->drag_frame_start;
  int track_diff = ((effective_tool == TIMELINE_TOOL_SLIDE || panel_timeline->transition_select != kTransitionNone) && !panel_timeline->importing) ? 0 : mouse_track - panel_timeline->drag_track_start;
  long validator;
  long earliest_in_point = LONG_MAX;

  // first try to snap
  long fm;

  if (effective_tool != TIMELINE_TOOL_SLIP) {
    // slipping doesn't move the clips so we don't bother snapping for it
    for (int i=0;i<panel_timeline->ghosts.size();i++) {
      const Ghost& g = panel_timeline->ghosts.at(i);

      // snap ghost's in point
      if ((panel_timeline->tool != TIMELINE_TOOL_TRANSITION && panel_timeline->trim_target == -1)
          || g.trim_type == TRIM_IN
          || panel_timeline->transition_tool_open_clip > -1) {
        fm = g.old_in + frame_diff;
        if (panel_timeline->snap_to_timeline(&fm, true, true, true)) {
          frame_diff = fm - g.old_in;
          break;
        }
      }

      // snap ghost's out point
      if ((panel_timeline->tool != TIMELINE_TOOL_TRANSITION && panel_timeline->trim_target == -1)
          || g.trim_type == TRIM_OUT
          || panel_timeline->transition_tool_close_clip > -1) {
        fm = g.old_out + frame_diff;
        if (panel_timeline->snap_to_timeline(&fm, true, true, true)) {
          frame_diff = fm - g.old_out;
          break;
        }
      }

      // if the ghost is attached to a clip, snap its markers too
      if (panel_timeline->trim_target == -1 && g.clip >= 0 && panel_timeline->tool != TIMELINE_TOOL_TRANSITION) {
        ClipPtr c = olive::ActiveSequence->clips.at(g.clip);
        for (int j=0;j<c->get_markers().size();j++) {
          long marker_real_time = c->get_markers().at(j).frame + c->timeline_in() - c->clip_in();
          fm = marker_real_time + frame_diff;
          if (panel_timeline->snap_to_timeline(&fm, true, true, true)) {
            frame_diff = fm - marker_real_time;
            break;
          }
        }
      }
    }
  }

  bool clips_are_movable = (effective_tool == TIMELINE_TOOL_POINTER || effective_tool == TIMELINE_TOOL_SLIDE);

  // validate ghosts
  long temp_frame_diff = frame_diff; // cache to see if we change it (thus cancelling any snap)
  for (int i=0;i<panel_timeline->ghosts.size();i++) {
    const Ghost& g = panel_timeline->ghosts.at(i);
    Clip* c = nullptr;
    if (g.clip != -1) {
      c = olive::ActiveSequence->clips.at(g.clip).get();
    }

    const FootageStream* ms = nullptr;
    if (g.clip != -1 && c->media() != nullptr && c->media()->get_type() == MEDIA_TYPE_FOOTAGE) {
      ms = c->media_stream();
    }

    // validate ghosts for trimming
    if (panel_timeline->creating) {
      // i feel like we might need something here but we haven't so far?
    } else if (effective_tool == TIMELINE_TOOL_SLIP) {
      if ((c->media() != nullptr && c->media()->get_type() == MEDIA_TYPE_SEQUENCE)
          || (ms != nullptr && !ms->infinite_length)) {
        // prevent slip moving a clip below 0 clip_in
        validator = g.old_clip_in - frame_diff;
        if (validator < 0) frame_diff += validator;

        // prevent slip moving clip beyond media length
        validator += g.ghost_length;
        if (validator > g.media_length) frame_diff += validator - g.media_length;
      }
    } else if (g.trim_type != TRIM_NONE) {
      if (g.trim_type == TRIM_IN) {
        // prevent clip/transition length from being less than 1 frame long
        validator = g.ghost_length - frame_diff;
        if (validator < 1) frame_diff -= (1 - validator);

        // prevent timeline in from going below 0
        if (effective_tool != TIMELINE_TOOL_RIPPLE) {
          validator = g.old_in + frame_diff;
          if (validator < 0) frame_diff -= validator;
        }

        // prevent clip_in from going below 0
        if ((c->media() != nullptr && c->media()->get_type() == MEDIA_TYPE_SEQUENCE)
            || (ms != nullptr && !ms->infinite_length)) {
          validator = g.old_clip_in + frame_diff;
          if (validator < 0) frame_diff -= validator;
        }
      } else {
        // prevent clip length from being less than 1 frame long
        validator = g.ghost_length + frame_diff;
        if (validator < 1) frame_diff += (1 - validator);

        // prevent clip length exceeding media length
        if ((c->media() != nullptr && c->media()->get_type() == MEDIA_TYPE_SEQUENCE)
            || (ms != nullptr && !ms->infinite_length)) {
          validator = g.old_clip_in + g.ghost_length + frame_diff;
          if (validator > g.media_length) frame_diff -= validator - g.media_length;
        }
      }

      // prevent dual transition from going below 0 on the primary or media length on the secondary
      if (g.transition != nullptr && g.transition->secondary_clip != nullptr) {
        Clip* otc = g.transition->parent_clip;
        Clip* ctc = g.transition->secondary_clip;

        if (g.trim_type == TRIM_IN) {
          frame_diff -= g.transition->get_true_length();
        } else {
          frame_diff += g.transition->get_true_length();
        }

        validate_transitions(otc, kTransitionOpening, frame_diff);
        validate_transitions(ctc, kTransitionClosing, frame_diff);

        frame_diff = -frame_diff;
        validate_transitions(otc, kTransitionOpening, frame_diff);
        validate_transitions(ctc, kTransitionClosing, frame_diff);
        frame_diff = -frame_diff;

        if (g.trim_type == TRIM_IN) {
          frame_diff += g.transition->get_true_length();
        } else {
          frame_diff -= g.transition->get_true_length();
        }
      }

      // ripple ops
      if (effective_tool == TIMELINE_TOOL_RIPPLE) {
        for (int j=0;j<post_clips.size();j++) {
          ClipPtr post = post_clips.at(j);

          // prevent any rippled clip from going below 0
          if (panel_timeline->trim_type == TRIM_IN) {
            validator = post->timeline_in() - frame_diff;
            if (validator < 0) frame_diff += validator;
          }

          // prevent any post-clips colliding with pre-clips
          for (int k=0;k<pre_clips.size();k++) {
            ClipPtr pre = pre_clips.at(k);
            if (pre != post && pre->track() == post->track()) {
              if (panel_timeline->trim_type == TRIM_IN) {
                validator = post->timeline_in() - frame_diff - pre->timeline_out();
                if (validator < 0) frame_diff += validator;
              } else {
                validator = post->timeline_in() + frame_diff - pre->timeline_out();
                if (validator < 0) frame_diff -= validator;
              }
            }
          }
        }
      }
    } else if (clips_are_movable) { // validate ghosts for moving
      // prevent clips from moving below 0 on the timeline
      validator = g.old_in + frame_diff;
      if (validator < 0) frame_diff -= validator;

      if (g.transition != nullptr) {
        if (g.transition->secondary_clip != nullptr) {
          // prevent dual transitions from going below 0 on the primary or above media length on the secondary

          validator = g.transition->parent_clip->clip_in(true) + frame_diff;
          if (validator < 0) frame_diff -= validator;

          validator = g.transition->secondary_clip->timeline_out(true) - g.transition->secondary_clip->timeline_in(true) - g.transition->get_length() + g.transition->secondary_clip->clip_in(true) + frame_diff;
          if (validator < 0) frame_diff -= validator;

          validator = g.transition->parent_clip->clip_in() + frame_diff - g.transition->parent_clip->media_length() + g.transition->get_true_length();
          if (validator > 0) frame_diff -= validator;

          validator = g.transition->secondary_clip->timeline_out(true) - g.transition->secondary_clip->timeline_in(true) + g.transition->secondary_clip->clip_in(true) + frame_diff - g.transition->secondary_clip->media_length();
          if (validator > 0) frame_diff -= validator;
        } else {
          // prevent clip_in from going below 0
          if ((c->media() != nullptr && c->media()->get_type() == MEDIA_TYPE_SEQUENCE)
              || (ms != nullptr && !ms->infinite_length)) {
            validator = g.old_clip_in + frame_diff;
            if (validator < 0) frame_diff -= validator;
          }

          // prevent clip length exceeding media length
          if ((c->media() != nullptr && c->media()->get_type() == MEDIA_TYPE_SEQUENCE)
              || (ms != nullptr && !ms->infinite_length)) {
            validator = g.old_clip_in + g.ghost_length + frame_diff;
            if (validator > g.media_length) frame_diff -= validator - g.media_length;
          }
        }
      }

      // prevent clips from crossing tracks
      if (same_sign(g.old_track, panel_timeline->drag_track_start)) {
        while (!same_sign(g.old_track, g.old_track + track_diff)) {
          if (g.old_track < 0) {
            track_diff--;
          } else {
            track_diff++;
          }
        }
      }
    } else if (effective_tool == TIMELINE_TOOL_TRANSITION) {
      if (panel_timeline->transition_tool_open_clip == -1
          || panel_timeline->transition_tool_close_clip == -1) {
        validate_transitions(c, g.media_stream, frame_diff);
      } else {
        // open transition clip
        Clip* otc = olive::ActiveSequence->clips.at(panel_timeline->transition_tool_open_clip).get();

        // close transition clip
        Clip* ctc = olive::ActiveSequence->clips.at(panel_timeline->transition_tool_close_clip).get();

        if (g.media_stream == kTransitionClosing) {
          // swap
          Clip* temp = otc;
          otc = ctc;
          ctc = temp;
        }

        // always gets a positive frame_diff
        validate_transitions(otc, kTransitionOpening, frame_diff);
        validate_transitions(ctc, kTransitionClosing, frame_diff);

        // always gets a negative frame_diff
        frame_diff = -frame_diff;
        validate_transitions(otc, kTransitionOpening, frame_diff);
        validate_transitions(ctc, kTransitionClosing, frame_diff);
        frame_diff = -frame_diff;
      }
    }
  }

  // if the above validation changed the frame movement, it's unlikely we're still snapped
  if (temp_frame_diff != frame_diff) {
    panel_timeline->snapped = false;
  }

  // apply changes to ghosts
  for (int i=0;i<panel_timeline->ghosts.size();i++) {
    Ghost& g = panel_timeline->ghosts[i];

    if (effective_tool == TIMELINE_TOOL_SLIP) {
      g.clip_in = g.old_clip_in - frame_diff;
    } else if (g.trim_type != TRIM_NONE) {
      long ghost_diff = frame_diff;

      // prevent trimming clips from overlapping each other
      for (int j=0;j<panel_timeline->ghosts.size();j++) {
        const Ghost& comp = panel_timeline->ghosts.at(j);
        if (i != j && g.track == comp.track) {
          long validator;
          if (g.trim_type == TRIM_IN && comp.out < g.out) {
            validator = (g.old_in + ghost_diff) - comp.out;
            if (validator < 0) ghost_diff -= validator;
          } else if (comp.in > g.in) {
            validator = (g.old_out + ghost_diff) - comp.in;
            if (validator > 0) ghost_diff -= validator;
          }
        }
      }

      // apply changes
      if (g.transition != nullptr && g.transition->secondary_clip != nullptr) {
        if (g.trim_type == TRIM_IN) ghost_diff = -ghost_diff;
        g.in = g.old_in - ghost_diff;
        g.out = g.old_out + ghost_diff;
      } else if (g.trim_type == TRIM_IN) {
        g.in = g.old_in + ghost_diff;
        g.clip_in = g.old_clip_in + ghost_diff;
      } else {
        g.out = g.old_out + ghost_diff;
      }
    } else if (clips_are_movable) {
      g.track = g.old_track;
      g.in = g.old_in + frame_diff;
      g.out = g.old_out + frame_diff;

      if (g.transition != nullptr
          && g.transition == olive::ActiveSequence->clips.at(g.clip)->opening_transition) {
        g.clip_in = g.old_clip_in + frame_diff;
      }

      if (panel_timeline->importing) {
        if ((panel_timeline->video_ghosts && mouse_track < 0)
            || (panel_timeline->audio_ghosts && mouse_track >= 0)) {
          int abs_track_diff = abs(track_diff);
          if (g.old_track < 0) { // clip is video
            g.track -= abs_track_diff;
          } else { // clip is audio
            g.track += abs_track_diff;
          }
        }
      } else if (same_sign(g.old_track, panel_timeline->drag_track_start)) {
        g.track += track_diff;
      }
    } else if (effective_tool == TIMELINE_TOOL_TRANSITION) {
      if (panel_timeline->transition_tool_open_clip > -1
            && panel_timeline->transition_tool_close_clip > -1) {
        g.in = g.old_in - frame_diff;
        g.out = g.old_out + frame_diff;
      } else if (panel_timeline->transition_tool_open_clip == g.clip) {
        g.out = g.old_out + frame_diff;
      } else {
        g.in = g.old_in + frame_diff;
      }
    }

    earliest_in_point = qMin(earliest_in_point, g.in);
  }

  // apply changes to selections
  if (effective_tool != TIMELINE_TOOL_SLIP && !panel_timeline->importing && !panel_timeline->creating) {
    for (int i=0;i<olive::ActiveSequence->selections.size();i++) {
      Selection& s = olive::ActiveSequence->selections[i];
      if (panel_timeline->trim_target > -1) {
        if (panel_timeline->trim_type == TRIM_IN) {
          s.in = s.old_in + frame_diff;
        } else {
          s.out = s.old_out + frame_diff;
        }
      } else if (clips_are_movable) {
        for (int i=0;i<olive::ActiveSequence->selections.size();i++) {
          Selection& s = olive::ActiveSequence->selections[i];
          s.in = s.old_in + frame_diff;
          s.out = s.old_out + frame_diff;
          s.track = s.old_track;

          if (panel_timeline->importing) {
            int abs_track_diff = abs(track_diff);
            if (s.old_track < 0) {
              s.track -= abs_track_diff;
            } else {
              s.track += abs_track_diff;
            }
          } else {
            if (same_sign(s.track, panel_timeline->drag_track_start)) s.track += track_diff;
          }
        }
      }
    }
  }

  if (panel_timeline->importing) {
    QToolTip::showText(mapToGlobal(mouse_pos), frame_to_timecode(earliest_in_point, olive::CurrentConfig.timecode_view, olive::ActiveSequence->frame_rate));
  } else {
    QString tip = ((frame_diff < 0) ? "-" : "+") + frame_to_timecode(qAbs(frame_diff), olive::CurrentConfig.timecode_view, olive::ActiveSequence->frame_rate);
    if (panel_timeline->trim_target > -1) {
      // find which clip is being moved
      const Ghost* g = nullptr;
      for (int i=0;i<panel_timeline->ghosts.size();i++) {
        if (panel_timeline->ghosts.at(i).clip == panel_timeline->trim_target) {
          g = &panel_timeline->ghosts.at(i);
          break;
        }
      }

      if (g != nullptr) {
        tip += " " + tr("Duration:") + " ";
        long len = (g->old_out-g->old_in);
        if (panel_timeline->trim_type == TRIM_IN) {
          len -= frame_diff;
        } else {
          len += frame_diff;
        }
        tip += frame_to_timecode(len, olive::CurrentConfig.timecode_view, olive::ActiveSequence->frame_rate);
      }
    }
    QToolTip::showText(mapToGlobal(mouse_pos), tip);
  }
}
