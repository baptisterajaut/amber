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
  if (!c) {
    qWarning() << "validate_transitions: c is null";
    return;
  }
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

// Adjusts the closing transition of clip c to make room for a new opening transition ending at transition_end.
static void adjust_closing_for_opening(ComboAction* ca, Clip* c, long transition_end, bool delete_old) {
  if (delete_old && c->opening_transition != nullptr) {
    ca->append(new DeleteTransitionCommand(c->opening_transition));
  }
  if (c->closing_transition == nullptr) return;
  if (transition_end >= c->timeline_out()) {
    ca->append(new DeleteTransitionCommand(c->closing_transition));
  } else if (transition_end > c->timeline_out() - c->closing_transition->get_true_length()) {
    ca->append(new ModifyTransitionCommand(c->closing_transition, c->timeline_out() - transition_end));
  }
}

// Adjusts the opening transition of clip c to make room for a new closing transition starting at transition_start.
static void adjust_opening_for_closing(ComboAction* ca, Clip* c, long transition_start, bool delete_old) {
  if (delete_old && c->closing_transition != nullptr) {
    ca->append(new DeleteTransitionCommand(c->closing_transition));
  }
  if (c->opening_transition == nullptr) return;
  if (transition_start <= c->timeline_in()) {
    ca->append(new DeleteTransitionCommand(c->opening_transition));
  } else if (transition_start < c->timeline_in() + c->opening_transition->get_true_length()) {
    ca->append(new ModifyTransitionCommand(c->opening_transition, transition_start - c->timeline_in()));
  }
}

void make_room_for_transition(ComboAction* ca, Clip* c, int type, long transition_start, long transition_end,
                              bool delete_old_transitions, long timeline_in = -1, long timeline_out = -1) {
  if (!ca) {
    qWarning() << "make_room_for_transition: ca is null";
    return;
  }
  if (!c) {
    qWarning() << "make_room_for_transition: c is null";
    return;
  }
  // it's possible to specify other in/out points for the clip, but default behavior is to use the ones existing
  if (timeline_in < 0) {
    timeline_in = c->timeline_in();
  }
  if (timeline_out < 0) {
    timeline_out = c->timeline_out();
  }

  if (type == kTransitionOpening) {
    adjust_closing_for_opening(ca, c, transition_end, delete_old_transitions);
  } else {
    adjust_opening_for_closing(ca, c, transition_start, delete_old_transitions);
  }
}

// Marks open/close as undeletable, clears the transition area, then restores deletability.
static void clear_transition_area(ComboAction* ca, Clip* open, Clip* close, long transition_start, long transition_end,
                                  int track) {
  if (open != nullptr) open->undeletable = true;
  if (close != nullptr) close->undeletable = true;

  QVector<Selection> areas;
  Selection s;
  s.in = transition_start;
  s.out = transition_end;
  s.track = track;
  areas.append(s);
  panel_timeline->delete_areas_and_relink(ca, areas, false);

  if (open != nullptr) open->undeletable = false;
  if (close != nullptr) close->undeletable = false;
}

// Computes new in/out bounds and resizes clip_ref if the transition extends beyond it.
static void resize_clip_for_transition(ComboAction* ca, Clip* clip_ref, int t, bool shared_transition, Clip* open,
                                       Clip* close, long transition_start, long transition_end) {
  if (clip_ref == nullptr) return;
  if (transition_start >= clip_ref->timeline_in() && transition_end <= clip_ref->timeline_out()) return;

  long new_in, new_out;
  if (t == kTransitionOpening) {
    new_in = shared_transition ? open->timeline_in() : qMin(transition_start, open->timeline_in());
    new_out = qMax(transition_end, open->timeline_out());
  } else {
    new_in = qMin(transition_start, close->timeline_in());
    new_out = shared_transition ? close->timeline_out() : qMax(transition_end, close->timeline_out());
  }

  clip_ref->move(ca, new_in, new_out, clip_ref->clip_in() - (clip_ref->timeline_in() - new_in), clip_ref->track());
}

void VerifyTransitionsAfterCreating(ComboAction* ca, Clip* open, Clip* close, long transition_start,
                                    long transition_end) {
  if (!ca) {
    qWarning() << "VerifyTransitionsAfterCreating: ca is null";
    return;
  }
  // in case the user made the transition larger than the clips, we're going to delete everything under
  // the transition ghost and extend the clips to the transition's coordinates as necessary

  if (open == nullptr && close == nullptr) {
    qWarning() << "VerifyTransitionsAfterCreating() called with two null clips";
    return;
  }

  bool shared_transition = (open != nullptr && close != nullptr);

  int track = 0;
  if (open != nullptr) track = open->track();
  if (close != nullptr) track = close->track();

  clear_transition_area(ca, open, close, transition_start, transition_end, track);

  for (int t = kTransitionOpening; t <= kTransitionClosing; t++) {
    Clip* clip_ref = (t == kTransitionOpening) ? open : close;
    if (clip_ref == nullptr) continue;

    make_room_for_transition(ca, clip_ref, t, transition_start, transition_end, true);
    resize_clip_for_transition(ca, clip_ref, t, shared_transition, open, close, transition_start, transition_end);
  }
}

void TimelineWidget::init_ghosts() {
  for (int i = 0; i < panel_timeline->ghosts.size(); i++) {
    Ghost& g = panel_timeline->ghosts[i];
    ClipPtr c = amber::ActiveSequence->clips.at(g.clip);

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
  for (auto& s : amber::ActiveSequence->selections) {
    s.old_in = s.in;
    s.old_out = s.out;
    s.old_track = s.track;
  }
}

// Tries to snap a single ghost's markers to the timeline; returns true and updates frame_diff if snapped.
static bool snapGhostMarkers(const Ghost& g, long& frame_diff) {
  if (g.clip < 0) return false;
  ClipPtr c = amber::ActiveSequence->clips.at(g.clip);
  for (int j = 0; j < c->get_markers().size(); j++) {
    long marker_real_time = c->get_markers().at(j).frame + c->timeline_in() - c->clip_in();
    long fm = marker_real_time + frame_diff;
    if (panel_timeline->snap_to_timeline(&fm, true, true, true)) {
      frame_diff = fm - marker_real_time;
      return true;
    }
  }
  return false;
}

static bool shouldSnapIn(Timeline* tl, const Ghost& g) {
  return (tl->tool != TIMELINE_TOOL_TRANSITION && tl->trim_target == -1) || g.trim_type == TRIM_IN ||
         tl->transition_tool_open_clip > -1;
}

static bool shouldSnapOut(Timeline* tl, const Ghost& g) {
  return (tl->tool != TIMELINE_TOOL_TRANSITION && tl->trim_target == -1) || g.trim_type == TRIM_OUT ||
         tl->transition_tool_close_clip > -1;
}

void TimelineWidget::updateGhostsSnap(int effective_tool, long& frame_diff) {
  if (effective_tool == TIMELINE_TOOL_SLIP) return;

  for (int i = 0; i < panel_timeline->ghosts.size(); i++) {
    const Ghost& g = panel_timeline->ghosts.at(i);
    long fm;

    if (shouldSnapIn(panel_timeline, g)) {
      fm = g.old_in + frame_diff;
      if (panel_timeline->snap_to_timeline(&fm, true, true, true)) {
        frame_diff = fm - g.old_in;
        break;
      }
    }

    if (shouldSnapOut(panel_timeline, g)) {
      fm = g.old_out + frame_diff;
      if (panel_timeline->snap_to_timeline(&fm, true, true, true)) {
        frame_diff = fm - g.old_out;
        break;
      }
    }

    if (panel_timeline->trim_target == -1 && panel_timeline->tool != TIMELINE_TOOL_TRANSITION) {
      if (snapGhostMarkers(g, frame_diff)) break;
    }
  }
}

// Returns true if the clip/media combo has finite length bounds (sequence or non-infinite footage).
static bool hasFiniteLengthBound(Clip* c, const FootageStream* ms) {
  return (c->media() != nullptr && c->media()->get_type() == MEDIA_TYPE_SEQUENCE) ||
         (ms != nullptr && !ms->infinite_length);
}

// Validates frame_diff for a slip operation on one ghost.
static void validateGhostSlip(const Ghost& g, Clip* c, const FootageStream* ms, long& frame_diff) {
  if (!hasFiniteLengthBound(c, ms)) return;

  long validator = g.old_clip_in - frame_diff;
  if (validator < 0) frame_diff += validator;

  validator += g.ghost_length;
  if (validator > g.media_length) frame_diff += validator - g.media_length;
}

// Validates frame_diff for a TRIM_IN operation on one ghost.
static void validateGhostTrimIn(const Ghost& g, Clip* c, const FootageStream* ms, int effective_tool,
                                long& frame_diff) {
  // prevent clip/transition length from being less than 1 frame long
  long validator = g.ghost_length - frame_diff;
  if (validator < 1) frame_diff -= (1 - validator);

  // prevent timeline in from going below 0
  if (effective_tool != TIMELINE_TOOL_RIPPLE) {
    validator = g.old_in + frame_diff;
    if (validator < 0) frame_diff -= validator;
  }

  // prevent clip_in from going below 0
  if (hasFiniteLengthBound(c, ms)) {
    validator = g.old_clip_in + frame_diff;
    if (validator < 0) frame_diff -= validator;
  }
}

// Validates frame_diff for a TRIM_OUT operation on one ghost.
static void validateGhostTrimOut(const Ghost& g, Clip* c, const FootageStream* ms, long& frame_diff) {
  // prevent clip length from being less than 1 frame long
  long validator = g.ghost_length + frame_diff;
  if (validator < 1) frame_diff += (1 - validator);

  // prevent clip length exceeding media length
  if (hasFiniteLengthBound(c, ms)) {
    validator = g.old_clip_in + g.ghost_length + frame_diff;
    if (validator > g.media_length) frame_diff -= validator - g.media_length;
  }
}

// Validates frame_diff for a dual transition trim (secondary_clip != nullptr).
static void validateGhostDualTransitionTrim(const Ghost& g, long& frame_diff) {
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

// Validates frame_diff for ripple post-clips (prevent going below 0 and colliding with pre-clips).
void TimelineWidget::validateGhostRipple(long& frame_diff) {
  long validator;
  for (auto post : post_clips) {
    if (panel_timeline->trim_type == TRIM_IN) {
      validator = post->timeline_in() - frame_diff;
      if (validator < 0) frame_diff += validator;
    }

    for (auto pre : pre_clips) {
      if (pre == post || pre->track() != post->track()) continue;
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

// Validates frame_diff for a movable ghost with a non-dual single-clip transition attached.
static void validateGhostMovableSingleTransition(const Ghost& g, Clip* c, const FootageStream* ms, long& frame_diff) {
  if (!hasFiniteLengthBound(c, ms)) return;

  long validator = g.old_clip_in + frame_diff;
  if (validator < 0) frame_diff -= validator;

  validator = g.old_clip_in + g.ghost_length + frame_diff;
  if (validator > g.media_length) frame_diff -= validator - g.media_length;
}

// Validates frame_diff for a movable ghost with a dual transition attached.
static void validateGhostMovableDualTransition(const Ghost& g, long& frame_diff) {
  long validator;

  validator = g.transition->parent_clip->clip_in(true) + frame_diff;
  if (validator < 0) frame_diff -= validator;

  validator = g.transition->secondary_clip->timeline_out(true) - g.transition->secondary_clip->timeline_in(true) -
              g.transition->get_length() + g.transition->secondary_clip->clip_in(true) + frame_diff;
  if (validator < 0) frame_diff -= validator;

  validator = g.transition->parent_clip->clip_in() + frame_diff - g.transition->parent_clip->media_length() +
              g.transition->get_true_length();
  if (validator > 0) frame_diff -= validator;

  validator = g.transition->secondary_clip->timeline_out(true) - g.transition->secondary_clip->timeline_in(true) +
              g.transition->secondary_clip->clip_in(true) + frame_diff - g.transition->secondary_clip->media_length();
  if (validator > 0) frame_diff -= validator;
}

// Validates frame_diff for a shared (dual-clip) transition tool operation.
static void validateGhostTransitionToolShared(const Ghost& g, long& frame_diff) {
  Clip* otc = amber::ActiveSequence->clips.at(panel_timeline->transition_tool_open_clip).get();
  Clip* ctc = amber::ActiveSequence->clips.at(panel_timeline->transition_tool_close_clip).get();

  if (g.media_stream == kTransitionClosing) {
    Clip* temp = otc;
    otc = ctc;
    ctc = temp;
  }

  validate_transitions(otc, kTransitionOpening, frame_diff);
  validate_transitions(ctc, kTransitionClosing, frame_diff);

  frame_diff = -frame_diff;
  validate_transitions(otc, kTransitionOpening, frame_diff);
  validate_transitions(ctc, kTransitionClosing, frame_diff);
  frame_diff = -frame_diff;
}

static void validateGhostMovable(const Ghost& g, Clip* c, const FootageStream* ms, long& frame_diff) {
  long validator = g.old_in + frame_diff;
  if (validator < 0) frame_diff -= validator;

  if (g.transition != nullptr) {
    if (g.transition->secondary_clip != nullptr) {
      validateGhostMovableDualTransition(g, frame_diff);
    } else {
      validateGhostMovableSingleTransition(g, c, ms, frame_diff);
    }
  }
}

static void validateGhostTrim(const Ghost& g, Clip* c, const FootageStream* ms, int effective_tool, long& frame_diff) {
  if (g.trim_type == TRIM_IN) {
    validateGhostTrimIn(g, c, ms, effective_tool, frame_diff);
  } else {
    validateGhostTrimOut(g, c, ms, frame_diff);
  }

  if (g.transition != nullptr && g.transition->secondary_clip != nullptr) {
    validateGhostDualTransitionTrim(g, frame_diff);
  }
}

void TimelineWidget::updateGhostsValidate(int effective_tool, bool clips_are_movable, long& frame_diff) {
  for (int i = 0; i < panel_timeline->ghosts.size(); i++) {
    const Ghost& g = panel_timeline->ghosts.at(i);
    Clip* c = (g.clip != -1) ? amber::ActiveSequence->clips.at(g.clip).get() : nullptr;

    const FootageStream* ms = nullptr;
    if (c != nullptr && c->media() != nullptr && c->media()->get_type() == MEDIA_TYPE_FOOTAGE) {
      ms = c->media_stream();
    }

    if (panel_timeline->creating) {
      // placeholder — no validation needed for creating yet
    } else if (effective_tool == TIMELINE_TOOL_SLIP) {
      validateGhostSlip(g, c, ms, frame_diff);
    } else if (g.trim_type != TRIM_NONE) {
      validateGhostTrim(g, c, ms, effective_tool, frame_diff);
      if (effective_tool == TIMELINE_TOOL_RIPPLE) {
        validateGhostRipple(frame_diff);
      }
    } else if (clips_are_movable) {
      validateGhostMovable(g, c, ms, frame_diff);
    } else if (effective_tool == TIMELINE_TOOL_TRANSITION) {
      if (panel_timeline->transition_tool_open_clip == -1 || panel_timeline->transition_tool_close_clip == -1) {
        validate_transitions(c, g.media_stream, frame_diff);
      } else {
        validateGhostTransitionToolShared(g, frame_diff);
      }
    }
  }
}

// Returns the resolved ghost_diff for a trim ghost, accounting for overlap with other ghosts.
static long resolveGhostTrimOverlap(int i, const Ghost& g, long frame_diff) {
  long ghost_diff = frame_diff;
  for (int j = 0; j < panel_timeline->ghosts.size(); j++) {
    if (i == j) continue;
    const Ghost& comp = panel_timeline->ghosts.at(j);
    if (g.track != comp.track) continue;

    long validator;
    if (g.trim_type == TRIM_IN && comp.out < g.out) {
      validator = (g.old_in + ghost_diff) - comp.out;
      if (validator < 0) ghost_diff -= validator;
    } else if (comp.in > g.in) {
      validator = (g.old_out + ghost_diff) - comp.in;
      if (validator > 0) ghost_diff -= validator;
    }
  }
  return ghost_diff;
}

// Applies the transition tool movement to a ghost.
static void applyGhostTransitionTool(Ghost& g, long frame_diff) {
  if (panel_timeline->transition_tool_open_clip > -1 && panel_timeline->transition_tool_close_clip > -1) {
    g.in = g.old_in - frame_diff;
    g.out = g.old_out + frame_diff;
  } else if (panel_timeline->transition_tool_open_clip == g.clip) {
    g.out = g.old_out + frame_diff;
  } else {
    g.in = g.old_in + frame_diff;
  }
}

static void applyGhostMovable(Ghost& g, long frame_diff, int track_diff, int mouse_track) {
  g.track = g.old_track;
  g.in = g.old_in + frame_diff;
  g.out = g.old_out + frame_diff;

  if (g.transition != nullptr && g.transition == amber::ActiveSequence->clips.at(g.clip)->opening_transition) {
    g.clip_in = g.old_clip_in + frame_diff;
  }

  if (panel_timeline->importing) {
    if ((panel_timeline->video_ghosts && mouse_track < 0) || (panel_timeline->audio_ghosts && mouse_track >= 0)) {
      int abs_track_diff = abs(track_diff);
      if (g.old_track < 0) {
        g.track -= abs_track_diff;
      } else {
        g.track += abs_track_diff;
      }
    }
  } else if (same_sign(g.old_track, panel_timeline->drag_track_start)) {
    g.track += track_diff;
  }
}

void TimelineWidget::updateGhostsApply(int effective_tool, bool clips_are_movable, long frame_diff, int track_diff,
                                       long& earliest_in_point) {
  int mouse_track = getTrackFromScreenPoint(mapFromGlobal(QCursor::pos()).y());

  for (int i = 0; i < panel_timeline->ghosts.size(); i++) {
    Ghost& g = panel_timeline->ghosts[i];

    if (effective_tool == TIMELINE_TOOL_SLIP) {
      g.clip_in = g.old_clip_in - frame_diff;
    } else if (g.trim_type != TRIM_NONE) {
      long ghost_diff = resolveGhostTrimOverlap(i, g, frame_diff);

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
      applyGhostMovable(g, frame_diff, track_diff, mouse_track);
    } else if (effective_tool == TIMELINE_TOOL_TRANSITION) {
      applyGhostTransitionTool(g, frame_diff);
    }

    earliest_in_point = qMin(earliest_in_point, g.in);
  }
}

void TimelineWidget::updateGhostsApplySelections(int effective_tool, bool clips_are_movable, long frame_diff,
                                                 int track_diff) {
  if (effective_tool == TIMELINE_TOOL_SLIP || panel_timeline->importing || panel_timeline->creating) return;

  for (int i = 0; i < amber::ActiveSequence->selections.size(); i++) {
    Selection& s = amber::ActiveSequence->selections[i];
    if (panel_timeline->trim_target > -1) {
      if (panel_timeline->trim_type == TRIM_IN) {
        s.in = s.old_in + frame_diff;
      } else {
        s.out = s.old_out + frame_diff;
      }
    } else if (clips_are_movable) {
      for (auto& s : amber::ActiveSequence->selections) {
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

void TimelineWidget::updateGhostsTooltip(const QPoint& mouse_pos, long frame_diff, long earliest_in_point) {
  if (panel_timeline->importing) {
    QToolTip::showText(mapToGlobal(mouse_pos), frame_to_timecode(earliest_in_point, amber::CurrentConfig.timecode_view,
                                                                 amber::ActiveSequence->frame_rate));
  } else {
    QString tip =
        ((frame_diff < 0) ? "-" : "+") +
        frame_to_timecode(qAbs(frame_diff), amber::CurrentConfig.timecode_view, amber::ActiveSequence->frame_rate);
    if (panel_timeline->trim_target > -1) {
      // find which clip is being moved
      const Ghost* g = nullptr;
      for (int i = 0; i < panel_timeline->ghosts.size(); i++) {
        if (panel_timeline->ghosts.at(i).clip == panel_timeline->trim_target) {
          g = &panel_timeline->ghosts.at(i);
          break;
        }
      }

      if (g != nullptr) {
        tip += " " + tr("Duration:") + " ";
        long len = (g->old_out - g->old_in);
        if (panel_timeline->trim_type == TRIM_IN) {
          len -= frame_diff;
        } else {
          len += frame_diff;
        }
        tip += frame_to_timecode(len, amber::CurrentConfig.timecode_view, amber::ActiveSequence->frame_rate);
      }
    }
    QToolTip::showText(mapToGlobal(mouse_pos), tip);
  }
}

void TimelineWidget::update_ghosts(const QPoint& mouse_pos, bool lock_frame) {
  int effective_tool = panel_timeline->tool;
  if (panel_timeline->importing || panel_timeline->creating) effective_tool = TIMELINE_TOOL_POINTER;

  int mouse_track = getTrackFromScreenPoint(mouse_pos.y());
  long frame_diff =
      (lock_frame) ? 0
                   : panel_timeline->getTimelineFrameFromScreenPoint(mouse_pos.x()) - panel_timeline->drag_frame_start;
  int track_diff = ((effective_tool == TIMELINE_TOOL_SLIDE || panel_timeline->transition_select != kTransitionNone) &&
                    !panel_timeline->importing)
                       ? 0
                       : mouse_track - panel_timeline->drag_track_start;
  long earliest_in_point = LONG_MAX;

  // first try to snap
  updateGhostsSnap(effective_tool, frame_diff);

  bool clips_are_movable = (effective_tool == TIMELINE_TOOL_POINTER || effective_tool == TIMELINE_TOOL_SLIDE);

  // validate ghosts
  long temp_frame_diff = frame_diff;  // cache to see if we change it (thus cancelling any snap)
  updateGhostsValidate(effective_tool, clips_are_movable, frame_diff);

  // if the above validation changed the frame movement, it's unlikely we're still snapped
  if (temp_frame_diff != frame_diff) {
    panel_timeline->snapped = false;
  }

  // also validate track crossing for movable clips
  if (clips_are_movable) {
    for (int i = 0; i < panel_timeline->ghosts.size(); i++) {
      const Ghost& g = panel_timeline->ghosts.at(i);
      if (same_sign(g.old_track, panel_timeline->drag_track_start)) {
        while (!same_sign(g.old_track, g.old_track + track_diff)) {
          if (g.old_track < 0) {
            track_diff--;
          } else {
            track_diff++;
          }
        }
      }
    }
  }

  // apply changes to ghosts
  updateGhostsApply(effective_tool, clips_are_movable, frame_diff, track_diff, earliest_in_point);

  // apply changes to selections
  updateGhostsApplySelections(effective_tool, clips_are_movable, frame_diff, track_diff);

  // show tooltip
  updateGhostsTooltip(mouse_pos, frame_diff, earliest_in_point);
}
