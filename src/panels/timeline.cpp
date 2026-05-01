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

#include <limits>

#include <QCheckBox>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QMessageBox>
#include <QPainter>
#include <QPushButton>
#include <QScreen>
#include <QScrollBar>
#include <QSplitter>
#include <QStatusBar>
#include <QTime>
#include <QtMath>

#include "engine/cacher.h"
#include "engine/undo/undostack.h"
#include "global/config.h"
#include "global/debug.h"
#include "global/global.h"
#include "panels/panels.h"
#include "panels/timeline_layout.h"
#include "project/clipboard.h"
#include "project/projectelements.h"
#include "rendering/audio.h"
#include "rendering/renderfunctions.h"
#include "ui/audiomonitor.h"
#include "ui/cursors.h"
#include "ui/flowlayout.h"
#include "ui/icons.h"
#include "ui/mainwindow.h"
#include "ui/menu.h"
#include "ui/resizablescrollbar.h"
#include "ui/timelineheader.h"
#include "ui/timelinewidget.h"
#include "ui/viewerwidget.h"

int amber::timeline::kTrackDefaultHeight = 40;
int amber::timeline::kTrackMinHeight = 30;
int amber::timeline::kTrackHeightIncrement = 10;

Timeline::Timeline(QWidget* parent)
    : Panel(parent)

{
  setup_ui();

  headers->viewer = panel_sequence_viewer;

  timeline_area->scrollBar = verticalScrollbar;

  tool_buttons.append(toolArrowButton);
  tool_buttons.append(toolEditButton);
  tool_buttons.append(toolRippleButton);
  tool_buttons.append(toolRazorButton);
  tool_buttons.append(toolSlipButton);
  tool_buttons.append(toolSlideButton);
  tool_buttons.append(toolTrackSelectButton);
  tool_buttons.append(toolTransitionButton);
  tool_buttons.append(toolHandButton);

  toolArrowButton->click();

  connect(horizontalScrollBar, &ResizableScrollBar::valueChanged, this, &Timeline::setScroll);
  connect(verticalScrollbar, &QScrollBar::valueChanged, timeline_area, &TimelineWidget::setScroll);
  connect(horizontalScrollBar, &ResizableScrollBar::resize_move, this, &Timeline::resize_move);

  update_sequence();

  Retranslate();
}

Timeline::~Timeline() = default;

// Retranslate() moved to timeline_ui.cpp

// split_clip_at_positions() moved to timeline_splitting.cpp

void Timeline::previous_cut() {
  if (amber::ActiveSequence != nullptr && amber::ActiveSequence->playhead > 0) {
    long p_cut = 0;
    for (int i = 0; i < amber::ActiveSequence->clips.size(); i++) {
      ClipPtr c = amber::ActiveSequence->clips.at(i);
      if (c != nullptr) {
        if (c->timeline_out() > p_cut && c->timeline_out() < amber::ActiveSequence->playhead) {
          p_cut = c->timeline_out();
        } else if (c->timeline_in() > p_cut && c->timeline_in() < amber::ActiveSequence->playhead) {
          p_cut = c->timeline_in();
        }
      }
    }
    panel_sequence_viewer->seek(p_cut);
  }
}

void Timeline::next_cut() {
  if (amber::ActiveSequence != nullptr) {
    bool seek_enabled = false;
    long n_cut = LONG_MAX;
    for (int i = 0; i < amber::ActiveSequence->clips.size(); i++) {
      ClipPtr c = amber::ActiveSequence->clips.at(i);
      if (c != nullptr) {
        if (c->timeline_in() < n_cut && c->timeline_in() > amber::ActiveSequence->playhead) {
          n_cut = c->timeline_in();
          seek_enabled = true;
        } else if (c->timeline_out() < n_cut && c->timeline_out() > amber::ActiveSequence->playhead) {
          n_cut = c->timeline_out();
          seek_enabled = true;
        }
      }
    }
    if (seek_enabled) panel_sequence_viewer->seek(n_cut);
  }
}

void ripple_clips(ComboAction* ca, Sequence* s, long point, long length, const QVector<int>& ignore) {
  ca->append(new RippleAction(s, point, length, ignore));
}

// toggle_show_all() moved to timeline_ui.cpp

// ---------------------------------------------------------------------------
// create_ghosts_from_media helpers
// ---------------------------------------------------------------------------

struct MediaImportInfo {
  Footage* footage{nullptr};
  Sequence* seq{nullptr};
  long sequence_length{0};
  long default_clip_in{0};
  long default_clip_out{0};
  bool can_import{false};
};

static MediaImportInfo classify_media_for_import(Media* medium, Sequence* seq) {
  MediaImportInfo info;
  switch (medium->get_type()) {
    case MEDIA_TYPE_FOOTAGE:
      info.footage = medium->to_footage();
      info.can_import = info.footage->ready;
      if (info.footage->using_inout) {
        double source_fr = 30;
        if (info.footage->video_tracks.size() > 0 && !qIsNull(info.footage->video_tracks.at(0).video_frame_rate)) {
          source_fr = info.footage->video_tracks.at(0).video_frame_rate * info.footage->speed;
        }
        info.default_clip_in = rescale_frame_number(info.footage->in, source_fr, seq->frame_rate);
        info.default_clip_out = rescale_frame_number(info.footage->out, source_fr, seq->frame_rate);
      }
      break;
    case MEDIA_TYPE_SEQUENCE:
      info.seq = medium->to_sequence().get();
      info.sequence_length = info.seq->getEndFrame();
      if (seq != nullptr) {
        info.sequence_length = rescale_frame_number(info.sequence_length, info.seq->frame_rate, seq->frame_rate);
      }
      info.can_import = (info.seq != seq && info.sequence_length != 0);
      if (info.seq->using_workarea) {
        info.default_clip_in = rescale_frame_number(info.seq->workarea_in, info.seq->frame_rate, seq->frame_rate);
        info.default_clip_out = rescale_frame_number(info.seq->workarea_out, info.seq->frame_rate, seq->frame_rate);
      }
      break;
    default:
      info.can_import = false;
  }
  return info;
}

static void append_footage_ghosts(Timeline* tl, Ghost g, const MediaImportInfo& info,
                                  amber::timeline::MediaImportData import_data, Sequence* seq, long entry_point) {
  Footage* m = info.footage;
  if (m->video_tracks.size() > 0 && m->video_tracks.at(0).infinite_length && m->audio_tracks.size() == 0) {
    g.out = g.in + amber::CurrentConfig.default_still_length;
  } else {
    long length = m->get_length_in_frames(seq->frame_rate);
    g.out = entry_point + length - info.default_clip_in;
    if (m->using_inout) {
      g.out -= (length - info.default_clip_out);
    }
  }

  if (import_data.type() == amber::timeline::kImportAudioOnly || import_data.type() == amber::timeline::kImportBoth) {
    for (int j = 0; j < m->audio_tracks.size(); j++) {
      if (m->audio_tracks.at(j).enabled) {
        g.track = j;
        g.media_stream = m->audio_tracks.at(j).file_index;
        tl->ghosts.append(g);
        tl->audio_ghosts = true;
      }
    }
  }

  if (import_data.type() == amber::timeline::kImportVideoOnly || import_data.type() == amber::timeline::kImportBoth) {
    for (int j = 0; j < m->video_tracks.size(); j++) {
      if (m->video_tracks.at(j).enabled) {
        g.track = -1 - j;
        g.media_stream = m->video_tracks.at(j).file_index;
        tl->ghosts.append(g);
        tl->video_ghosts = true;
      }
    }
  }
}

static void append_sequence_ghosts(Timeline* tl, Ghost g, const MediaImportInfo& info,
                                   amber::timeline::MediaImportData import_data, long entry_point) {
  Sequence* s = info.seq;
  g.out = entry_point + info.sequence_length - info.default_clip_in;
  if (s->using_workarea) {
    g.out -= (info.sequence_length - info.default_clip_out);
  }

  if (import_data.type() == amber::timeline::kImportVideoOnly || import_data.type() == amber::timeline::kImportBoth) {
    g.track = -1;
    tl->ghosts.append(g);
  }

  if (import_data.type() == amber::timeline::kImportAudioOnly || import_data.type() == amber::timeline::kImportBoth) {
    g.track = 0;
    tl->ghosts.append(g);
  }

  tl->video_ghosts = true;
  tl->audio_ghosts = true;
}

void Timeline::create_ghosts_from_media(Sequence* seq, long entry_point,
                                        QVector<amber::timeline::MediaImportData>& media_list) {
  video_ghosts = false;
  audio_ghosts = false;

  for (auto import_data : media_list) {
    Media* medium = import_data.media();
    MediaImportInfo info = classify_media_for_import(medium, seq);
    if (!info.can_import) continue;

    Ghost g;
    g.clip = -1;
    g.trim_type = TRIM_NONE;
    g.old_clip_in = g.clip_in = info.default_clip_in;
    g.media = medium;
    g.in = entry_point;
    g.transition = nullptr;

    switch (medium->get_type()) {
      case MEDIA_TYPE_FOOTAGE:
        append_footage_ghosts(this, g, info, import_data, seq, entry_point);
        entry_point = ghosts.isEmpty() ? entry_point : ghosts.last().out;
        break;
      case MEDIA_TYPE_SEQUENCE:
        append_sequence_ghosts(this, g, info, import_data, entry_point);
        entry_point = ghosts.isEmpty() ? entry_point : ghosts.last().out;
        break;
      default:
        break;
    }
  }

  for (auto& g : ghosts) {
    g.old_in = g.in;
    g.old_out = g.out;
    g.old_track = g.track;
  }
}

void Timeline::add_clips_from_ghosts(ComboAction* ca, Sequence* s) {
  // add clips
  long earliest_point = LONG_MAX;
  QVector<ClipPtr> added_clips;
  for (const auto& g : ghosts) {
    earliest_point = qMin(earliest_point, g.in);

    ClipPtr c = std::make_shared<Clip>(s);
    c->set_media(g.media, g.media_stream);
    c->set_timeline_in(g.in);
    c->set_timeline_out(g.out);
    c->set_clip_in(g.clip_in);
    c->set_track(g.track);
    if (c->media()->get_type() == MEDIA_TYPE_FOOTAGE) {
      Footage* m = c->media()->to_footage();
      if (m->video_tracks.size() == 0) {
        // audio only (greenish)
        c->set_color(128, 192, 128);
      } else if (m->audio_tracks.size() == 0) {
        // video only (orangeish)
        c->set_color(192, 160, 128);
      } else {
        // video and audio (blueish)
        c->set_color(128, 128, 192);
      }
      c->set_name(m->name);
    } else if (c->media()->get_type() == MEDIA_TYPE_SEQUENCE) {
      // sequence (red?ish?)
      c->set_color(192, 128, 128);

      c->set_name(c->media()->to_sequence()->name);
    }
    c->refresh();
    added_clips.append(c);
  }
  ca->append(new AddClipCommand(s, added_clips));

  // link clips from the same media
  for (int i = 0; i < added_clips.size(); i++) {
    ClipPtr c = added_clips.at(i);
    for (int j = 0; j < added_clips.size(); j++) {
      ClipPtr cc = added_clips.at(j);
      if (c != cc && c->media() == cc->media()) {
        c->linked.append(j);
      }
    }

    if (amber::CurrentConfig.add_default_effects_to_clips) {
      if (c->track() < 0) {
        // add default video effects
        c->effects.append(
            Effect::Create(c.get(), Effect::GetInternalMeta(EFFECT_INTERNAL_TRANSFORM, EFFECT_TYPE_EFFECT)));
      } else {
        // add default audio effects
        c->effects.append(Effect::Create(c.get(), Effect::GetInternalMeta(EFFECT_INTERNAL_VOLUME, EFFECT_TYPE_EFFECT)));
        c->effects.append(Effect::Create(c.get(), Effect::GetInternalMeta(EFFECT_INTERNAL_PAN, EFFECT_TYPE_EFFECT)));
      }
    }
  }
  if (amber::CurrentConfig.enable_seek_to_import) {
    panel_sequence_viewer->seek(earliest_point);
  }
  ghosts.clear();
  importing = false;
  snapped = false;
}

void Timeline::add_transition() {
  ComboAction* ca = new ComboAction(tr("Add Transition"));
  bool adding = false;

  for (const auto& clip : amber::ActiveSequence->clips) {
    Clip* c = clip.get();
    if (c != nullptr && c->IsSelected()) {
      int transition_to_add = (c->track() < 0) ? TRANSITION_INTERNAL_CROSSDISSOLVE : TRANSITION_INTERNAL_LINEARFADE;
      if (c->opening_transition == nullptr) {
        ca->append(new AddTransitionCommand(c, nullptr, nullptr,
                                            Effect::GetInternalMeta(transition_to_add, EFFECT_TYPE_TRANSITION),
                                            amber::CurrentConfig.default_transition_length));
        adding = true;
      }
      if (c->closing_transition == nullptr) {
        ca->append(new AddTransitionCommand(nullptr, c, nullptr,
                                            Effect::GetInternalMeta(transition_to_add, EFFECT_TYPE_TRANSITION),
                                            amber::CurrentConfig.default_transition_length));
        adding = true;
      }
    }
  }

  if (adding) {
    amber::UndoStack.push(ca);
  } else {
    delete ca;
  }

  update_ui(true);
}

// Move each ghost to a free track if it overlaps an existing clip (excluding selected_clips).
static void resolve_ghost_track_collisions(QVector<Ghost>& ghosts, Sequence* seq,
                                           const QVector<int>& selected_clips) {
  for (int j = 0; j < seq->clips.size(); j++) {
    Clip* c = seq->clips.at(j).get();
    if (c == nullptr || selected_clips.contains(j)) continue;

    for (auto& g : ghosts) {
      bool overlaps = c->track() == g.track && !(c->timeline_out() <= g.in || c->timeline_in() >= g.out);
      if (overlaps) {
        g.track += (g.track < 0) ? -1 : 1;
        j = -1;  // restart scan
        break;
      }
    }
  }
}

void Timeline::nest() {
  if (amber::ActiveSequence == nullptr) return;

  QVector<int> selected_clips = amber::ActiveSequence->SelectedClipIndexes();
  if (selected_clips.isEmpty()) return;

  long earliest_point = LONG_MAX;
  for (int selected_clip : selected_clips) {
    earliest_point = qMin(amber::ActiveSequence->clips.at(selected_clip)->timeline_in(), earliest_point);
  }

  ComboAction* ca = new ComboAction(tr("Nest Clip(s)"));

  SequencePtr s = std::make_shared<Sequence>();
  s->name = panel_project->get_next_sequence_name(tr("Nested Sequence"));
  s->width = amber::ActiveSequence->width;
  s->height = amber::ActiveSequence->height;
  s->frame_rate = amber::ActiveSequence->frame_rate;
  s->audio_frequency = amber::ActiveSequence->audio_frequency;
  s->audio_layout = amber::ActiveSequence->audio_layout;

  for (int selected_clip : selected_clips) {
    ca->append(new DeleteClipAction(amber::ActiveSequence.get(), selected_clip));
    ClipPtr copy(amber::ActiveSequence->clips.at(selected_clip)->copy(s.get()));
    copy->set_timeline_in(copy->timeline_in() - earliest_point);
    copy->set_timeline_out(copy->timeline_out() - earliest_point);
    s->clips.append(copy);
  }

  relink_clips_using_ids(selected_clips, s->clips);

  MediaPtr m = panel_project->create_sequence_internal(ca, s, false, nullptr);

  QVector<amber::timeline::MediaImportData> media_list;
  media_list.append(m.get());
  create_ghosts_from_media(amber::ActiveSequence.get(), earliest_point, media_list);

  resolve_ghost_track_collisions(ghosts, amber::ActiveSequence.get(), selected_clips);

  add_clips_from_ghosts(ca, amber::ActiveSequence.get());

  panel_graph_editor->set_row(nullptr);
  panel_effect_controls->Clear(true);
  amber::ActiveSequence->selections.clear();

  amber::UndoStack.push(ca);
  update_ui(true);
}

// Expand a single nested sequence clip into individual clips in the parent sequence.
// Appends expanded clips to new_clips and returns the inner_to_new index mapping.
static QMap<int, int> unnest_single_clip(Clip* nested_clip, Sequence* parent_seq, QVector<ClipPtr>& new_clips) {
  Sequence* inner_seq = nested_clip->media()->to_sequence().get();
  long nest_in = nested_clip->timeline_in();
  long nest_clip_in = nested_clip->clip_in();
  long nest_duration = nested_clip->timeline_out() - nested_clip->timeline_in();
  long visible_start = nest_clip_in;
  long visible_end = nest_clip_in + nest_duration;
  double rate_factor = parent_seq->frame_rate / inner_seq->frame_rate;
  int video_offset = nested_clip->track() - (-1);
  int audio_offset = -(nested_clip->track() + 1);

  QMap<int, int> inner_to_new;

  for (int i = 0; i < inner_seq->clips.size(); i++) {
    const auto& inner_clip = inner_seq->clips.at(i);
    if (inner_clip == nullptr) continue;

    long in = inner_clip->timeline_in();
    long out = inner_clip->timeline_out();
    if (out <= visible_start || in >= visible_end) continue;

    ClipPtr c = inner_clip->copy(parent_seq);
    int inner_track = inner_clip->track();
    c->set_track(inner_track < 0 ? inner_track + video_offset : inner_track + audio_offset);

    if (in < visible_start) {
      c->set_clip_in(c->clip_in() + (visible_start - in));
      in = visible_start;
    }
    if (out > visible_end) out = visible_end;

    c->set_timeline_in(nest_in + qRound((in - visible_start) * rate_factor));
    c->set_timeline_out(nest_in + qRound((out - visible_start) * rate_factor));
    c->refresh();

    inner_to_new.insert(i, new_clips.size());
    new_clips.append(c);
  }

  // Restore A/V links
  for (auto it = inner_to_new.constBegin(); it != inner_to_new.constEnd(); ++it) {
    const auto& inner_clip = inner_seq->clips.at(it.key());
    ClipPtr& new_clip = new_clips[it.value()];
    for (int linked_inner_idx : inner_clip->linked) {
      auto mapped = inner_to_new.find(linked_inner_idx);
      if (mapped != inner_to_new.end()) {
        new_clip->linked.append(mapped.value());
      }
    }
  }

  return inner_to_new;
}

void Timeline::unnest() {
  if (amber::ActiveSequence == nullptr) return;

  QVector<int> selected_clips = amber::ActiveSequence->SelectedClipIndexes();
  if (selected_clips.isEmpty()) return;

  ComboAction* ca = new ComboAction(tr("Unnest Clip(s)"));
  QVector<ClipPtr> new_clips;
  bool any_unnested = false;

  for (int idx : selected_clips) {
    Clip* nested_clip = amber::ActiveSequence->clips.at(idx).get();
    if (nested_clip == nullptr || nested_clip->media() == nullptr) continue;
    if (nested_clip->media()->get_type() != MEDIA_TYPE_SEQUENCE) continue;
    if (nested_clip->media()->to_sequence().get() == nullptr) continue;

    unnest_single_clip(nested_clip, amber::ActiveSequence.get(), new_clips);
    ca->append(new DeleteClipAction(amber::ActiveSequence.get(), idx));
    any_unnested = true;
  }

  if (!any_unnested) {
    delete ca;
    return;
  }

  ca->append(new AddClipCommand(amber::ActiveSequence.get(), new_clips));

  panel_graph_editor->set_row(nullptr);
  panel_effect_controls->Clear(true);
  amber::ActiveSequence->selections.clear();

  amber::UndoStack.push(ca);
  update_ui(true);
}

// update_sequence() moved to timeline_ui.cpp

int Timeline::get_snap_range() { return getFrameFromScreenPoint(zoom, 10); }

// focused() moved to timeline_ui.cpp

// repaint_timeline() moved to timeline_ui.cpp

void Timeline::select_all() {
  if (amber::ActiveSequence != nullptr) {
    amber::ActiveSequence->selections.clear();
    for (int i = 0; i < amber::ActiveSequence->clips.size(); i++) {
      ClipPtr c = amber::ActiveSequence->clips.at(i);
      if (c != nullptr) {
        Selection s;
        s.in = c->timeline_in();
        s.out = c->timeline_out();
        s.track = c->track();
        amber::ActiveSequence->selections.append(s);
      }
    }
    repaint_timeline();
  }
}

void Timeline::scroll_to_frame(long frame) {
  scroll_to_frame_internal(horizontalScrollBar, frame, zoom, timeline_area_widget->width());
}

void Timeline::scroll_to_track(int track) {
  if (!amber::ActiveSequence) return;

  amber::timeline_layout::TrackHeights h;
  int video_count = 0, audio_count = 0;
  amber::ActiveSequence->getTrackLimits(&video_count, &audio_count);
  for (int t = -1; t >= video_count; --t) h.video.append(GetTrackHeight(t));
  for (int t = 0; t <= audio_count; ++t) h.audio.append(GetTrackHeight(t));

  int target_top = amber::timeline_layout::track_top_y(h, track);
  verticalScrollbar->setValue(qMin(target_top, verticalScrollbar->value()));
}

int Timeline::SeamY() {
  if (!seam_y_dirty_) return seam_y_cache_;
  if (!amber::ActiveSequence) {
    seam_y_cache_ = 0;
    seam_y_dirty_ = false;
    return 0;
  }
  amber::timeline_layout::TrackHeights h;
  int video_count = 0, audio_count = 0;
  amber::ActiveSequence->getTrackLimits(&video_count, &audio_count);
  for (int t = -1; t >= video_count; --t) h.video.append(GetTrackHeight(t));
  for (int t = 0; t <= audio_count; ++t) h.audio.append(GetTrackHeight(t));
  seam_y_cache_ = amber::timeline_layout::seam_y(h);
  seam_y_dirty_ = false;
  return seam_y_cache_;
}

void Timeline::select_from_playhead() {
  amber::ActiveSequence->selections.clear();
  for (int i = 0; i < amber::ActiveSequence->clips.size(); i++) {
    ClipPtr c = amber::ActiveSequence->clips.at(i);
    if (c != nullptr && c->timeline_in() <= amber::ActiveSequence->playhead &&
        c->timeline_out() > amber::ActiveSequence->playhead) {
      Selection s;
      s.in = c->timeline_in();
      s.out = c->timeline_out();
      s.track = c->track();
      amber::ActiveSequence->selections.append(s);
    }
  }
}

bool Timeline::can_ripple_empty_space(long frame, int track) {
  bool can_ripple_delete = true;
  bool at_end_of_sequence = true;
  rc_ripple_min = 0;
  rc_ripple_max = LONG_MAX;

  for (auto c : amber::ActiveSequence->clips) {
    if (c != nullptr) {
      if (c->timeline_in() > frame || c->timeline_out() > frame) {
        at_end_of_sequence = false;
      }
      if (c->track() == track) {
        if (c->timeline_in() <= frame && c->timeline_out() >= frame) {
          can_ripple_delete = false;
          break;
        } else if (c->timeline_out() < frame) {
          rc_ripple_min = qMax(rc_ripple_min, c->timeline_out());
        } else if (c->timeline_in() > frame) {
          rc_ripple_max = qMin(rc_ripple_max, c->timeline_in());
        }
      }
    }
  }

  return (can_ripple_delete && !at_end_of_sequence);
}

void Timeline::ripple_delete_empty_space() {
  if (rc_ripple_max == LONG_MAX) return;

  QVector<Selection> sels;

  Selection s;
  s.in = rc_ripple_min;
  s.out = rc_ripple_max;
  s.track = cursor_track;

  sels.append(s);

  delete_selection(sels, true);
}

// resizeEvent() moved to timeline_ui.cpp

void Timeline::delete_in_out_internal(bool ripple) {
  if (amber::ActiveSequence != nullptr && amber::ActiveSequence->using_workarea) {
    QVector<Selection> areas;
    int video_tracks = 0, audio_tracks = 0;
    amber::ActiveSequence->getTrackLimits(&video_tracks, &audio_tracks);
    for (int i = video_tracks; i <= audio_tracks; i++) {
      Selection s;
      s.in = amber::ActiveSequence->workarea_in;
      s.out = amber::ActiveSequence->workarea_out;
      s.track = i;
      areas.append(s);
    }
    ComboAction* ca = new ComboAction(ripple ? tr("Ripple Delete In/Out") : tr("Delete In/Out"));
    delete_areas_and_relink(ca, areas, true);
    if (ripple)
      ripple_clips(ca, amber::ActiveSequence.get(), amber::ActiveSequence->workarea_in,
                   amber::ActiveSequence->workarea_in - amber::ActiveSequence->workarea_out);
    ca->append(new SetTimelineInOutCommand(amber::ActiveSequence.get(), false, 0, 0));
    amber::UndoStack.push(ca);
    update_ui(true);
  }
}

void Timeline::toggle_enable_on_selected_clips() {
  if (amber::ActiveSequence != nullptr) {
    // get currently selected clips
    QVector<Clip*> selected_clips = amber::ActiveSequence->SelectedClips();

    if (!selected_clips.isEmpty()) {
      // if clips are selected, create an undoable action
      SetClipProperty* set_action = new SetClipProperty(kSetClipPropertyEnabled);
      set_action->setText(tr("Toggle Clip(s)"));

      // add each selected clip to the action
      for (auto c : selected_clips) {
        set_action->AddSetting(c, !c->enabled());
      }

      // push the action
      amber::UndoStack.push(set_action);
      update_ui(false);
    }
  }
}

// Compute the ripple point and clamped ripple length from a set of selections and the clip list.
// Returns true if ripple can proceed.
static bool clip_overlaps_any_selection(Clip* c, const QVector<Selection>& selections) {
  for (const auto& s : selections) {
    if (s.track == c->track() && !(c->timeline_in() < s.in && c->timeline_out() < s.in) &&
        !(c->timeline_in() > s.out && c->timeline_out() > s.out)) {
      return true;
    }
  }
  return false;
}

static void clamp_ripple_length(Clip* c, const QVector<ClipPtr>& clips, long& ripple_length) {
  for (auto cc : clips) {
    if (cc != nullptr && cc->track() == c->track() && cc->timeline_in() > c->timeline_out() &&
        cc->timeline_in() < c->timeline_out() + ripple_length) {
      ripple_length = cc->timeline_in() - c->timeline_out();
    }
  }
}

static bool compute_ripple_params(const QVector<Selection>& selections, const QVector<ClipPtr>& clips,
                                  long& ripple_point, long& ripple_length) {
  ripple_point = selections.at(0).in;
  ripple_length = selections.at(0).out - selections.at(0).in;
  for (int i = 1; i < selections.size(); i++) {
    ripple_point = qMin(ripple_point, selections.at(i).in);
    ripple_length = qMin(ripple_length, selections.at(i).out - selections.at(i).in);
  }
  ripple_point++;

  for (int i = 0; i < clips.size(); i++) {
    ClipPtr c = clips.at(i);
    if (c == nullptr || c->timeline_in() >= ripple_point || c->timeline_out() <= ripple_point) continue;
    if (clip_overlaps_any_selection(c.get(), selections)) continue;
    clamp_ripple_length(c.get(), clips, ripple_length);
  }
  return true;
}

void Timeline::delete_selection(QVector<Selection>& selections, bool ripple_delete) {
  if (selections.isEmpty()) return;

  panel_graph_editor->set_row(nullptr);
  panel_effect_controls->Clear(true);

  ComboAction* ca = new ComboAction(ripple_delete ? tr("Ripple Delete") : tr("Delete"));
  delete_areas_and_relink(ca, selections, !ripple_delete);

  if (ripple_delete) {
    long ripple_point = 0;
    long ripple_length = 0;
    compute_ripple_params(selections, amber::ActiveSequence->clips, ripple_point, ripple_length);
    ripple_clips(ca, amber::ActiveSequence.get(), ripple_point, -ripple_length);
    panel_sequence_viewer->seek(ripple_point - 1);
    selections.clear();
  }

  amber::UndoStack.push(ca);
  update_ui(true);
}

void Timeline::set_zoom_value(double v) {
  // set zoom value
  zoom = v;

  // update header zoom to match
  headers->update_zoom(zoom);

  // set flag that zoom has just changed to prevent auto-scrolling since we change the scroll below
  zoom_just_changed = true;

  // set scrollbar to center the playhead
  if (amber::ActiveSequence != nullptr) {
    // update scrollbar maximum value for new zoom
    set_sb_max();

    if (!horizontalScrollBar->is_resizing()) {
      center_scroll_to_playhead(horizontalScrollBar, zoom, amber::ActiveSequence->playhead);
    }
  }

  // repaint the timeline for the new zoom/location
  repaint_timeline();

  // reset after all recursive calls have completed, so auto-scroll
  // cannot interfere during the entire zoom operation
  zoom_just_changed = false;
}

void Timeline::multiply_zoom(double m) {
  showing_all = false;
  set_zoom_value(zoom * m);
}

void Timeline::decheck_tool_buttons(QObject* sender) {
  for (int i = 0; i < tool_buttons.count(); i++) {
    tool_buttons[i]->setChecked(tool_buttons.at(i) == sender);
  }
}

void Timeline::zoom_in() { multiply_zoom(2.0); }

void Timeline::zoom_out() { multiply_zoom(0.5); }

int Timeline::GetTrackHeight(int track) {
  for (auto track_height : track_heights) {
    if (track_height.index == track) {
      return track_height.height;
    }
  }
  return amber::timeline::kTrackDefaultHeight;
}

void Timeline::SetTrackHeight(int track, int height) {
  seam_y_dirty_ = true;
  for (auto& track_height : track_heights) {
    if (track_height.index == track) {
      track_height.height = height;
      return;
    }
  }

  // we don't have a track height, so set a new one
  TimelineTrackHeight t;
  t.index = track;
  t.height = height;
  track_heights.append(t);
}

void Timeline::ChangeTrackHeightUniformly(int diff) {
  // get range of tracks currently active
  int min_track, max_track;
  amber::ActiveSequence->getTrackLimits(&min_track, &max_track);

  // for each active track, set the track to increase/decrease based on `diff`
  for (int i = min_track; i <= max_track; i++) {
    SetTrackHeight(i, qMax(GetTrackHeight(i) + diff, amber::timeline::kTrackMinHeight));
  }

  // update the timeline
  repaint_timeline();
}

void Timeline::IncreaseTrackHeight() { ChangeTrackHeightUniformly(amber::timeline::kTrackHeightIncrement); }

void Timeline::DecreaseTrackHeight() { ChangeTrackHeightUniformly(-amber::timeline::kTrackHeightIncrement); }

void Timeline::snapping_clicked(bool checked) { snapping = checked; }

// split_clip() (both overloads), split_clip_and_relink(), clean_up_selections(),
// selection_contains_transition() moved to timeline_splitting.cpp

// Process a single clip against a deletion selection area, handling all overlap cases.
// Returns a non-null ClipPtr (post-split) only for the "split middle" case.
static ClipPtr process_clip_in_area(Timeline* tl, ComboAction* ca, int j, Clip* c, const Selection& s) {
  if (selection_contains_transition(s, c, kTransitionOpening)) {
    ca->append(new DeleteTransitionCommand(c->opening_transition));
    return nullptr;
  }
  if (selection_contains_transition(s, c, kTransitionClosing)) {
    ca->append(new DeleteTransitionCommand(c->closing_transition));
    return nullptr;
  }
  if (c->timeline_in() >= s.in && c->timeline_out() <= s.out) {
    // Clip falls entirely within deletion area
    ca->append(new DeleteClipAction(amber::ActiveSequence.get(), j));
    return nullptr;
  }
  if (c->timeline_in() < s.in && c->timeline_out() > s.out) {
    // Middle of clip is within deletion area — split it
    return tl->split_clip(ca, true, j, s.in, s.out);
  }
  if (c->timeline_in() < s.in && c->timeline_out() > s.in) {
    // Only out point is in deletion area
    c->move(ca, c->timeline_in(), s.in, c->clip_in(), c->track());
    if (c->closing_transition != nullptr) {
      if (s.in < c->timeline_out() - c->closing_transition->get_true_length()) {
        ca->append(new DeleteTransitionCommand(c->closing_transition));
      } else {
        ca->append(new ModifyTransitionCommand(c->closing_transition,
                                               c->closing_transition->get_true_length() - (c->timeline_out() - s.in)));
      }
    }
    return nullptr;
  }
  if (c->timeline_in() < s.out && c->timeline_out() > s.out) {
    // Only in point is in deletion area
    c->move(ca, s.out, c->timeline_out(), c->clip_in() + (s.out - c->timeline_in()), c->track());
    if (c->opening_transition != nullptr) {
      if (s.out > c->timeline_in() + c->opening_transition->get_true_length()) {
        ca->append(new DeleteTransitionCommand(c->opening_transition));
      } else {
        ca->append(new ModifyTransitionCommand(c->opening_transition,
                                               c->opening_transition->get_true_length() - (s.out - c->timeline_in())));
      }
    }
    return nullptr;
  }
  return nullptr;
}

void Timeline::delete_areas_and_relink(ComboAction* ca, QVector<Selection>& areas, bool deselect_areas) {
  clean_up_selections(areas);

  panel_graph_editor->set_row(nullptr);
  panel_effect_controls->Clear(true);

  QVector<int> pre_clips;
  QVector<ClipPtr> post_clips;

  for (const auto& s : areas) {
    for (int j = 0; j < amber::ActiveSequence->clips.size(); j++) {
      Clip* c = amber::ActiveSequence->clips.at(j).get();
      if (c == nullptr || c->track() != s.track || c->undeletable) continue;

      ClipPtr post = process_clip_in_area(this, ca, j, c, s);
      if (post != nullptr) {
        pre_clips.append(j);
        post_clips.append(post);
      }
    }
  }

  if (deselect_areas) {
    QVector<Selection> area_copy = areas;
    for (const auto& s : area_copy) {
      deselect_area(s.in, s.out, s.track);
    }
  }

  relink_clips_using_ids(pre_clips, post_clips);
  ca->append(new AddClipCommand(amber::ActiveSequence.get(), post_clips));
}

// copy(), relink_clips_using_ids(), paste() moved to timeline_clipboard.cpp

// ---------------------------------------------------------------------------
// edit_to_point_internal helpers
// ---------------------------------------------------------------------------

struct EditPointInfo {
  int track_min{INT_MAX};
  int track_max{INT_MIN};
  long sequence_end{0};
  bool playhead_falls_on_in{false};
  bool playhead_falls_on_out{false};
  long next_cut{LONG_MAX};
  long prev_cut{0};
};

static EditPointInfo collect_edit_point_info(const QVector<ClipPtr>& clips, long playhead) {
  EditPointInfo info;
  for (int i = 0; i < clips.size(); i++) {
    ClipPtr c = clips.at(i);
    if (c == nullptr) continue;
    info.track_min = qMin(info.track_min, c->track());
    info.track_max = qMax(info.track_max, c->track());
    info.sequence_end = qMax(c->timeline_out(), info.sequence_end);
    if (c->timeline_in() == playhead) info.playhead_falls_on_in = true;
    if (c->timeline_out() == playhead) info.playhead_falls_on_out = true;
    if (c->timeline_in() > playhead) info.next_cut = qMin(c->timeline_in(), info.next_cut);
    if (c->timeline_out() > playhead) info.next_cut = qMin(c->timeline_out(), info.next_cut);
    if (c->timeline_in() < playhead) info.prev_cut = qMax(c->timeline_in(), info.prev_cut);
    if (c->timeline_out() < playhead) info.prev_cut = qMax(c->timeline_out(), info.prev_cut);
  }
  return info;
}

static QVector<Selection> build_selections_for_track_range(int track_min, int track_max, long sel_in, long sel_out) {
  QVector<Selection> areas;
  Selection s;
  s.in = sel_in;
  s.out = sel_out;
  for (int i = track_min; i <= track_max; i++) {
    s.track = i;
    areas.append(s);
  }
  return areas;
}

static void edit_to_point_on_boundary(Timeline* tl, ComboAction* ca, bool in, bool ripple, long playhead,
                                      const EditPointInfo& info, long& seek, bool& push_undo) {
  if (!ripple) {
    push_undo = false;
    return;
  }
  long in_point = playhead;
  if (!in) {
    in_point--;
    seek--;
  }
  if (in_point >= 0) {
    auto areas = build_selections_for_track_range(info.track_min, info.track_max, in_point, in_point + 1);
    tl->delete_areas_and_relink(ca, areas, true);
    ripple_clips(ca, amber::ActiveSequence.get(), in_point, -1);
  } else {
    push_undo = false;
  }
}

static void edit_to_point_off_boundary(Timeline* tl, ComboAction* ca, bool in, bool ripple, long playhead,
                                       const EditPointInfo& info, long& seek, bool& push_undo) {
  if (in) seek = info.prev_cut;
  long sel_in = in ? info.prev_cut : playhead;
  long sel_out = in ? playhead : info.next_cut;
  if (sel_in == sel_out) {
    push_undo = false;
  } else {
    auto areas = build_selections_for_track_range(info.track_min, info.track_max, sel_in, sel_out);
    tl->delete_areas_and_relink(ca, areas, true);
    if (ripple) ripple_clips(ca, amber::ActiveSequence.get(), sel_in, sel_in - sel_out);
  }
}

void Timeline::edit_to_point_internal(bool in, bool ripple) {
  if (amber::ActiveSequence == nullptr) return;
  if (amber::ActiveSequence->clips.isEmpty()) {
    panel_sequence_viewer->seek(0);
    return;
  }

  long playhead = amber::ActiveSequence->playhead;
  EditPointInfo info = collect_edit_point_info(amber::ActiveSequence->clips, playhead);
  info.next_cut = qMin(info.sequence_end, info.next_cut);

  QVector<Selection> areas;
  ComboAction* ca = new ComboAction(ripple ? tr("Ripple Edit") : tr("Trim"));
  bool push_undo = true;
  long seek = playhead;

  bool on_boundary =
      (in && (info.playhead_falls_on_out || (info.playhead_falls_on_in && playhead == 0))) ||
      (!in && (info.playhead_falls_on_in || (info.playhead_falls_on_out && playhead == info.sequence_end)));

  if (on_boundary) {
    edit_to_point_on_boundary(this, ca, in, ripple, playhead, info, seek, push_undo);
  } else {
    edit_to_point_off_boundary(this, ca, in, ripple, playhead, info, seek, push_undo);
  }

  if (push_undo) {
    amber::UndoStack.push(ca);
    update_ui(true);
    if (seek != playhead && ripple) {
      panel_sequence_viewer->seek(seek);
    }
  } else {
    delete ca;
  }
}

// split_selection(), split_all_clips_at_point(), split_at_playhead() moved to timeline_splitting.cpp

void Timeline::ripple_delete() {
  if (amber::ActiveSequence != nullptr) {
    if (amber::ActiveSequence->selections.size() > 0) {
      panel_timeline->delete_selection(amber::ActiveSequence->selections, true);
    } else if (amber::CurrentConfig.hover_focus && get_focused_panel() == panel_timeline) {
      if (panel_timeline->can_ripple_empty_space(panel_timeline->cursor_frame, panel_timeline->cursor_track)) {
        panel_timeline->ripple_delete_empty_space();
      }
    }
  }
}

void Timeline::deselect_area(long in, long out, int track) {
  int len = amber::ActiveSequence->selections.size();
  for (int i = 0; i < len; i++) {
    Selection& s = amber::ActiveSequence->selections[i];
    if (s.track == track) {
      if (s.in >= in && s.out <= out) {
        // whole selection is in deselect area
        amber::ActiveSequence->selections.removeAt(i);
        i--;
        len--;
      } else if (s.in < in && s.out > out) {
        // middle of selection is in deselect area
        Selection new_sel;
        new_sel.in = out;
        new_sel.out = s.out;
        new_sel.track = s.track;
        amber::ActiveSequence->selections.append(new_sel);

        s.out = in;
      } else if (s.in < in && s.out > in) {
        // only out point is in deselect area
        s.out = in;
      } else if (s.in < out && s.out > out) {
        // only in point is in deselect area
        s.in = out;
      }
    }
  }
}

bool Timeline::snap_to_point(long point, long* l) {
  int limit = get_snap_range();
  if (*l > point - limit - 1 && *l < point + limit + 1) {
    snap_point = point;
    *l = point;
    snapped = true;
    return true;
  }
  return false;
}

// Try to snap to all snap points on a single clip. Returns true if snapped.
static bool snap_to_clip_points(Timeline* tl, const ClipPtr& c, long* l, bool prefer_outgoing) {
  if (tl->snap_to_point(c->timeline_in(), l)) return true;
  if (tl->snap_to_point(prefer_outgoing ? c->timeline_out() - 1 : c->timeline_out(), l)) return true;
  if (c->opening_transition != nullptr &&
      tl->snap_to_point(c->timeline_in() + c->opening_transition->get_true_length(), l)) {
    return true;
  }
  if (c->closing_transition != nullptr &&
      tl->snap_to_point(c->timeline_out() - c->closing_transition->get_true_length(), l)) {
    return true;
  }
  for (int j = 0; j < c->get_markers().size(); j++) {
    if (tl->snap_to_point(c->get_markers().at(j).frame + c->timeline_in() - c->clip_in(), l)) {
      return true;
    }
  }
  return false;
}

static bool resolve_outgoing_pref() {
  bool pref = amber::CurrentConfig.snap_to_outgoing_clip;
  static constexpr Qt::KeyboardModifier kModifiers[] = {Qt::ShiftModifier, Qt::ControlModifier, Qt::AltModifier};
  int mod_idx = qBound(0, amber::CurrentConfig.snap_outgoing_modifier, 2);
  if (QGuiApplication::keyboardModifiers() & kModifiers[mod_idx]) {
    pref = !pref;
  }
  return pref;
}

bool Timeline::toolSupportsInsert() const { return tool == TIMELINE_TOOL_POINTER || tool == TIMELINE_TOOL_TRACK_SELECT || importing || creating; }

bool Timeline::snap_to_markers(long* l) {
  for (const auto& marker : amber::ActiveSequence->markers) {
    if (snap_to_point(marker.frame, l)) return true;
  }
  return false;
}

bool Timeline::snap_to_timeline(long* l, bool use_playhead, bool use_markers, bool use_workarea, bool for_playhead) {
  snapped = false;
  if (!snapping) return false;

  if (use_playhead && !panel_sequence_viewer->playing) {
    if (snap_to_point(amber::ActiveSequence->playhead, l)) return true;
  }

  if (use_markers && snap_to_markers(l)) return true;

  if (use_workarea && amber::ActiveSequence->using_workarea) {
    if (snap_to_point(amber::ActiveSequence->workarea_in, l)) return true;
    if (snap_to_point(amber::ActiveSequence->workarea_out, l)) return true;
  }

  bool prefer_outgoing = for_playhead && resolve_outgoing_pref();

  for (auto c : amber::ActiveSequence->clips) {
    if (c != nullptr && snap_to_clip_points(this, c, l, prefer_outgoing)) return true;
  }
  return false;
}

// set_marker() moved to timeline_ui.cpp

void Timeline::delete_inout() { panel_timeline->delete_in_out_internal(false); }

void Timeline::ripple_delete_inout() { panel_timeline->delete_in_out_internal(true); }

void Timeline::ripple_to_in_point() { panel_timeline->edit_to_point_internal(true, true); }

void Timeline::ripple_to_out_point() { panel_timeline->edit_to_point_internal(false, true); }

void Timeline::edit_to_in_point() { panel_timeline->edit_to_point_internal(true, false); }

void Timeline::edit_to_out_point() { panel_timeline->edit_to_point_internal(false, false); }

void Timeline::toggle_links() {
  LinkCommand* command = new LinkCommand();
  command->setText(tr("Toggle Links"));
  command->s = amber::ActiveSequence.get();
  for (int i = 0; i < amber::ActiveSequence->clips.size(); i++) {
    Clip* c = amber::ActiveSequence->clips.at(i).get();
    if (c != nullptr && c->IsSelected()) {
      if (!command->clips.contains(i)) command->clips.append(i);

      if (c->linked.size() > 0) {
        command->link = false;  // prioritize unlinking

        for (int j : c->linked) {  // add links to the command
          if (!command->clips.contains(j)) command->clips.append(j);
        }
      }
    }
  }
  if (command->clips.size() > 0) {
    amber::UndoStack.push(command);
    repaint_timeline();
  } else {
    delete command;
  }
}

void Timeline::deselect() {
  amber::ActiveSequence->selections.clear();
  repaint_timeline();
}

long getFrameFromScreenPoint(double zoom, int x) {
  long f = qRound(double(x) / zoom);
  if (f < 0) {
    return 0;
  }
  return f;
}

int getScreenPointFromFrame(double zoom, long frame) {
  const double v = double(frame) * zoom;
  if (v >= double(std::numeric_limits<int>::max())) return std::numeric_limits<int>::max();
  if (v <= double(std::numeric_limits<int>::min())) return std::numeric_limits<int>::min();
  return qRound(v);
}

long Timeline::getTimelineFrameFromScreenPoint(int x) { return getFrameFromScreenPoint(zoom, x + scroll); }

int Timeline::getTimelineScreenPointFromFrame(long frame) { return getScreenPointFromFrame(zoom, frame) - scroll; }

void Timeline::add_btn_click() {
  Menu add_menu(this);

  QAction* titleMenuItem = new QAction(&add_menu);
  titleMenuItem->setText(tr("Title..."));
  titleMenuItem->setData(ADD_OBJ_TITLE);
  add_menu.addAction(titleMenuItem);

  QAction* solidMenuItem = new QAction(&add_menu);
  solidMenuItem->setText(tr("Solid Color..."));
  solidMenuItem->setData(ADD_OBJ_SOLID);
  add_menu.addAction(solidMenuItem);

  QAction* barsMenuItem = new QAction(&add_menu);
  barsMenuItem->setText(tr("Bars..."));
  barsMenuItem->setData(ADD_OBJ_BARS);
  add_menu.addAction(barsMenuItem);

  QAction* gradientMenuItem = new QAction(&add_menu);
  gradientMenuItem->setText(tr("Gradient..."));
  gradientMenuItem->setData(ADD_OBJ_GRADIENT);
  add_menu.addAction(gradientMenuItem);

  add_menu.addSeparator();

  QAction* toneMenuItem = new QAction(&add_menu);
  toneMenuItem->setText(tr("Tone..."));
  toneMenuItem->setData(ADD_OBJ_TONE);
  add_menu.addAction(toneMenuItem);

  QAction* noiseMenuItem = new QAction(&add_menu);
  noiseMenuItem->setText(tr("Noise..."));
  noiseMenuItem->setData(ADD_OBJ_NOISE);
  add_menu.addAction(noiseMenuItem);

  connect(&add_menu, &QMenu::triggered, this, &Timeline::add_menu_item);

  add_menu.exec(QCursor::pos());
}

void Timeline::add_menu_item(QAction* action) {
  creating = true;
  creating_object = action->data().toInt();
}

void Timeline::setScroll(int s) {
  scroll = s;
  headers->set_scroll(s);
  repaint_timeline();
}

void Timeline::record_btn_click() {
  if (amber::ActiveProjectFilename.isEmpty()) {
    QMessageBox::critical(this, tr("Unsaved Project"),
                          tr("You must save this project before you can record audio in it."), QMessageBox::Ok);
  } else {
    creating = true;
    creating_object = ADD_OBJ_AUDIO;
    amber::MainWindow->statusBar()->showMessage(tr("Click on the timeline where you want to start recording (drag to "
                                                   "limit the recording to a certain timeframe)"),
                                                10000);
  }
}

void Timeline::transition_tool_click() {
  creating = false;

  Menu transition_menu(this);

  for (const auto& em : effects) {
    if (em.type == EFFECT_TYPE_TRANSITION && em.subtype == EFFECT_TYPE_VIDEO) {
      QAction* a = transition_menu.addAction(em.name);
      a->setObjectName("v");
      a->setData(reinterpret_cast<quintptr>(&em));
    }
  }

  transition_menu.addSeparator();

  for (const auto& em : effects) {
    if (em.type == EFFECT_TYPE_TRANSITION && em.subtype == EFFECT_TYPE_AUDIO) {
      QAction* a = transition_menu.addAction(em.name);
      a->setObjectName("a");
      a->setData(reinterpret_cast<quintptr>(&em));
    }
  }

  connect(&transition_menu, &QMenu::triggered, this, &Timeline::transition_menu_select);

  toolTransitionButton->setChecked(false);

  transition_menu.exec(QCursor::pos());
}

void Timeline::transition_menu_select(QAction* a) {
  transition_tool_meta = reinterpret_cast<const EffectMeta*>(a->data().value<quintptr>());

  if (a->objectName() == "v") {
    transition_tool_side = -1;
  } else {
    transition_tool_side = 1;
  }

  decheck_tool_buttons(sender());
  timeline_area->setCursor(Qt::CrossCursor);
  tool = TIMELINE_TOOL_TRANSITION;
  toolTransitionButton->setChecked(true);
}

void Timeline::resize_move(double z) { set_zoom_value(zoom * z); }

// set_sb_max(), UpdateTitle(), setup_ui() moved to timeline_ui.cpp

void Timeline::set_tool() {
  QPushButton* button = static_cast<QPushButton*>(sender());
  decheck_tool_buttons(button);
  tool = button->property("tool").toInt();
  creating = false;
  switch (tool) {
    case TIMELINE_TOOL_EDIT:
      timeline_area->setCursor(Qt::IBeamCursor);
      break;
    case TIMELINE_TOOL_RAZOR:
      timeline_area->setCursor(amber::cursor::Razor);
      break;
    case TIMELINE_TOOL_HAND:
      timeline_area->setCursor(Qt::OpenHandCursor);
      break;
    default:
      timeline_area->setCursor(Qt::ArrowCursor);
  }
}

void Timeline::match_frame() {
  if (!amber::ActiveSequence) return;

  long playhead = amber::ActiveSequence->playhead;

  // Find topmost video clip at playhead (lowest track index = topmost)
  Clip* topmost = nullptr;
  for (const auto& c : amber::ActiveSequence->clips) {
    if (c == nullptr) continue;
    if (c->track() >= 0) continue;  // skip audio tracks
    if (playhead < c->timeline_in(true) || playhead >= c->timeline_out(true)) continue;
    if (c->media() == nullptr || c->media()->get_type() != MEDIA_TYPE_FOOTAGE) continue;
    if (topmost == nullptr || c->track() < topmost->track()) {
      topmost = c.get();
    }
  }

  if (topmost == nullptr) return;

  // Calculate source frame: clip_in + offset within clip
  long offset = playhead - topmost->timeline_in();
  long source_frame = topmost->clip_in() + offset;

  // Open in footage viewer at that frame
  panel_footage_viewer->set_media(topmost->media());
  panel_footage_viewer->seek(source_frame);
}

void Timeline::three_point_insert() { three_point_edit(true); }

void Timeline::three_point_overwrite() { three_point_edit(false); }

// ---------------------------------------------------------------------------
// three_point_edit helpers
// ---------------------------------------------------------------------------

struct ThreePointRange {
  long src_in{0};
  long src_out{0};
  bool valid{false};
};

// Resolve the source clip range and destination target position for a three-point edit.
static bool resolve_three_point_range(SequencePtr src_seq, Sequence* dst_seq, double src_rate, double dst_rate,
                                      long& clip_in_dst, long& duration_dst, long& target_pos) {
  long src_in = 0;
  long src_out = src_seq->clips.isEmpty() ? 0 : src_seq->clips.at(0)->timeline_out();
  bool source_has_in = false;
  bool source_has_out = false;

  if (src_seq->using_workarea) {
    src_in = src_seq->workarea_in;
    source_has_in = true;
    if (src_seq->workarea_out > src_in) {
      src_out = src_seq->workarea_out;
      source_has_out = true;
    }
  }

  if (!source_has_in && !source_has_out && !dst_seq->using_workarea) return false;

  if (!source_has_out && dst_seq->using_workarea) {
    long seq_duration = dst_seq->workarea_out - dst_seq->workarea_in;
    long dur_src = rescale_frame_number(seq_duration, dst_rate, src_rate);
    src_out = src_in + dur_src;
  }

  target_pos = dst_seq->playhead;
  if (dst_seq->using_workarea && dst_seq->workarea_in >= 0) {
    target_pos = dst_seq->workarea_in;
  }

  clip_in_dst = rescale_frame_number(src_in, src_rate, dst_rate);
  duration_dst = rescale_frame_number(src_out - src_in, src_rate, dst_rate);
  return duration_dst > 0;
}

static QVector<ClipPtr> build_three_point_clips(Footage* footage, Media* media, Sequence* seq, long timeline_in,
                                                long timeline_out, long clip_in_dst) {
  QVector<ClipPtr> clips;
  bool has_video = !footage->video_tracks.isEmpty();
  bool has_audio = !footage->audio_tracks.isEmpty();

  if (has_video) {
    ClipPtr vc = std::make_shared<Clip>(seq);
    vc->set_media(media, footage->video_tracks.at(0).file_index);
    vc->set_timeline_in(timeline_in);
    vc->set_timeline_out(timeline_out);
    vc->set_clip_in(clip_in_dst);
    vc->set_track(-1);
    vc->refresh();
    clips.append(vc);
  }

  if (has_audio) {
    ClipPtr ac = std::make_shared<Clip>(seq);
    ac->set_media(media, footage->audio_tracks.at(0).file_index);
    ac->set_timeline_in(timeline_in);
    ac->set_timeline_out(timeline_out);
    ac->set_clip_in(clip_in_dst);
    ac->set_track(0);
    ac->refresh();
    clips.append(ac);
  }

  if (has_video && has_audio) {
    clips[0]->linked.append(1);
    clips[1]->linked.append(0);
  }

  return clips;
}

void Timeline::three_point_edit(bool insert) {
  if (amber::ActiveSequence == nullptr) return;
  if (panel_footage_viewer->media == nullptr) return;
  if (panel_footage_viewer->media->get_type() != MEDIA_TYPE_FOOTAGE) return;

  Footage* footage = panel_footage_viewer->media->to_footage();
  SequencePtr src_seq = panel_footage_viewer->seq;
  if (src_seq == nullptr) return;
  if (footage->video_tracks.isEmpty() && footage->audio_tracks.isEmpty()) return;

  double src_rate = src_seq->frame_rate;
  double dst_rate = amber::ActiveSequence->frame_rate;
  long clip_in_dst = 0;
  long duration_dst = 0;
  long target_pos = 0;

  if (!resolve_three_point_range(src_seq, amber::ActiveSequence.get(), src_rate, dst_rate, clip_in_dst, duration_dst,
                                 target_pos)) {
    return;
  }

  long timeline_in = target_pos;
  long timeline_out = target_pos + duration_dst;

  QVector<ClipPtr> new_clips = build_three_point_clips(
      footage, panel_footage_viewer->media, amber::ActiveSequence.get(), timeline_in, timeline_out, clip_in_dst);

  ComboAction* ca = new ComboAction(insert ? tr("Insert Edit") : tr("Overwrite Edit"));

  if (insert) {
    split_cache.clear();
    split_all_clips_at_point(ca, target_pos);
    ripple_clips(ca, amber::ActiveSequence.get(), target_pos, duration_dst);
  } else {
    QVector<Selection> delete_areas;
    for (const auto& c : new_clips) {
      Selection s;
      s.in = c->timeline_in();
      s.out = c->timeline_out();
      s.track = c->track();
      delete_areas.append(s);
    }
    delete_areas_and_relink(ca, delete_areas, false);
  }

  ca->append(new AddClipCommand(amber::ActiveSequence.get(), new_clips));
  amber::UndoStack.push(ca);

  update_ui(true);
  panel_sequence_viewer->seek(timeline_out);
}

static bool has_any_frozen_clip(const QVector<Clip*>& clips) {
  for (auto* c : clips) {
    if (qFuzzyIsNull(c->speed().value)) return true;
  }
  return false;
}

void Timeline::freeze_frame() {
  if (amber::ActiveSequence == nullptr) return;

  QVector<Clip*> selected = amber::ActiveSequence->SelectedClips();
  if (!selected.isEmpty() && has_any_frozen_clip(selected)) {
    unfreeze_frame();
    return;
  }

  long playhead = amber::ActiveSequence->playhead;

  QVector<int> clip_indexes;
  for (int i = 0; i < amber::ActiveSequence->clips.size(); i++) {
    Clip* c = amber::ActiveSequence->clips.at(i).get();
    if (c != nullptr && c->track() < 0 && c->timeline_in() < playhead && c->timeline_out() > playhead) {
      clip_indexes.append(i);
    }
  }
  if (clip_indexes.isEmpty()) return;

  ComboAction* ca = new ComboAction(tr("Freeze Frame"));
  QVector<int> pre_clips;
  QVector<ClipPtr> post_clips;

  for (int idx : clip_indexes) {
    ClipPtr post = split_clip(ca, true, idx, playhead);
    if (post != nullptr) {
      pre_clips.append(idx);
      post_clips.append(post);
    }
  }

  if (post_clips.isEmpty()) {
    delete ca;
    return;
  }

  relink_clips_using_ids(pre_clips, post_clips);

  for (auto& post : post_clips) {
    ClipSpeed frozen;
    frozen.value = 0.0;
    frozen.maintain_audio_pitch = false;
    post->set_speed(frozen);
  }

  ca->append(new AddClipCommand(amber::ActiveSequence.get(), post_clips));
  amber::UndoStack.push(ca);
  update_ui(true);
}

void Timeline::unfreeze_frame() {
  if (amber::ActiveSequence == nullptr) return;

  QVector<Clip*> frozen_clips = amber::ActiveSequence->SelectedClips();
  if (frozen_clips.isEmpty()) return;

  ComboAction* ca = new ComboAction(tr("Unfreeze Frame"));
  bool changed = false;

  for (auto* c : frozen_clips) {
    if (qFuzzyIsNull(c->speed().value)) {
      ca->append(new SetSpeedAction(c, 1.0));
      changed = true;
    }
  }

  if (changed) {
    amber::UndoStack.push(ca);
    update_ui(true);
  } else {
    delete ca;
  }
}

void amber::timeline::MultiplyTrackSizesByDPI() {
  kTrackDefaultHeight *= QGuiApplication::primaryScreen()->devicePixelRatio();
  kTrackMinHeight *= QGuiApplication::primaryScreen()->devicePixelRatio();
  kTrackHeightIncrement *= QGuiApplication::primaryScreen()->devicePixelRatio();
}
