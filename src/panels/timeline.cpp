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

#include <QTime>
#include <QScrollBar>
#include <QtMath>
#include <QGuiApplication>
#include <QScreen>
#include <QPainter>
#include <QInputDialog>
#include <QMessageBox>
#include <QCheckBox>
#include <QPushButton>
#include <QHBoxLayout>
#include <QSplitter>
#include <QStatusBar>

#include "global/global.h"
#include "panels/panels.h"
#include "project/projectelements.h"
#include "ui/timelinewidget.h"
#include "ui/icons.h"
#include "ui/viewerwidget.h"
#include "rendering/audio.h"
#include "engine/cacher.h"
#include "rendering/renderfunctions.h"
#include "global/config.h"
#include "project/clipboard.h"
#include "ui/timelineheader.h"
#include "ui/resizablescrollbar.h"
#include "ui/audiomonitor.h"
#include "ui/flowlayout.h"
#include "ui/cursors.h"
#include "ui/mainwindow.h"
#include "engine/undo/undostack.h"
#include "global/debug.h"
#include "ui/menu.h"

int amber::timeline::kTrackDefaultHeight = 40;
int amber::timeline::kTrackMinHeight = 30;
int amber::timeline::kTrackHeightIncrement = 10;

Timeline::Timeline(QWidget *parent) :
  Panel(parent)
  
{
  setup_ui();

  headers->viewer = panel_sequence_viewer;

  video_area->bottom_align = true;
  video_area->scrollBar = videoScrollbar;
  audio_area->scrollBar = audioScrollbar;

  tool_buttons.append(toolArrowButton);
  tool_buttons.append(toolEditButton);
  tool_buttons.append(toolRippleButton);
  tool_buttons.append(toolRazorButton);
  tool_buttons.append(toolSlipButton);
  tool_buttons.append(toolSlideButton);
  tool_buttons.append(toolTransitionButton);
  tool_buttons.append(toolHandButton);

  toolArrowButton->click();

  connect(horizontalScrollBar, &ResizableScrollBar::valueChanged, this, &Timeline::setScroll);
  connect(videoScrollbar, &QScrollBar::valueChanged, video_area, &TimelineWidget::setScroll);
  connect(audioScrollbar, &QScrollBar::valueChanged, audio_area, &TimelineWidget::setScroll);
  connect(horizontalScrollBar, &ResizableScrollBar::resize_move, this, &Timeline::resize_move);

  update_sequence();

  Retranslate();
}

Timeline::~Timeline() = default;

// Retranslate() moved to timeline_ui.cpp

// split_clip_at_positions() moved to timeline_splitting.cpp

void Timeline::previous_cut() {
  if (amber::ActiveSequence != nullptr
      && amber::ActiveSequence->playhead > 0) {
    long p_cut = 0;
    for (int i=0;i<amber::ActiveSequence->clips.size();i++) {
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
    for (int i=0;i<amber::ActiveSequence->clips.size();i++) {
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

void Timeline::create_ghosts_from_media(Sequence* seq, long entry_point, QVector<amber::timeline::MediaImportData>& media_list) {
  video_ghosts = false;
  audio_ghosts = false;

  for (auto import_data : media_list) {
    bool can_import = true;

    Media* medium = import_data.media();
    Footage* m = nullptr;
    Sequence* s = nullptr;
    long sequence_length = 0;
    long default_clip_in = 0;
    long default_clip_out = 0;

    switch (medium->get_type()) {
    case MEDIA_TYPE_FOOTAGE:
      m = medium->to_footage();
      can_import = m->ready;
      if (m->using_inout) {
        double source_fr = 30;
        if (m->video_tracks.size() > 0 && !qIsNull(m->video_tracks.at(0).video_frame_rate)) {
          source_fr = m->video_tracks.at(0).video_frame_rate * m->speed;
        }
        default_clip_in = rescale_frame_number(m->in, source_fr, seq->frame_rate);
        default_clip_out = rescale_frame_number(m->out, source_fr, seq->frame_rate);
      }
      break;
    case MEDIA_TYPE_SEQUENCE:
      s = medium->to_sequence().get();
      sequence_length = s->getEndFrame();
      if (seq != nullptr) sequence_length = rescale_frame_number(sequence_length, s->frame_rate, seq->frame_rate);
      can_import = (s != seq && sequence_length != 0);
      if (s->using_workarea) {
        default_clip_in = rescale_frame_number(s->workarea_in, s->frame_rate, seq->frame_rate);
        default_clip_out = rescale_frame_number(s->workarea_out, s->frame_rate, seq->frame_rate);
      }
      break;
    default:
      can_import = false;
    }

    if (can_import) {
      Ghost g;
      g.clip = -1;
      g.trim_type = TRIM_NONE;
      g.old_clip_in = g.clip_in = default_clip_in;
      g.media = medium;
      g.in = entry_point;
      g.transition = nullptr;

      switch (medium->get_type()) {
      case MEDIA_TYPE_FOOTAGE:
        // is video source a still image?
        if (m->video_tracks.size() > 0 && m->video_tracks.at(0).infinite_length && m->audio_tracks.size() == 0) {
          g.out = g.in + 100;
        } else {
          long length = m->get_length_in_frames(seq->frame_rate);
          g.out = entry_point + length - default_clip_in;
          if (m->using_inout) {
            g.out -= (length - default_clip_out);
          }
        }

        if (import_data.type() == amber::timeline::kImportAudioOnly
            || import_data.type() == amber::timeline::kImportBoth) {
          for (int j=0;j<m->audio_tracks.size();j++) {
            if (m->audio_tracks.at(j).enabled) {
              g.track = j;
              g.media_stream = m->audio_tracks.at(j).file_index;
              ghosts.append(g);
              audio_ghosts = true;
            }
          }
        }

        if (import_data.type() == amber::timeline::kImportVideoOnly
            || import_data.type() == amber::timeline::kImportBoth) {
          for (int j=0;j<m->video_tracks.size();j++) {
            if (m->video_tracks.at(j).enabled) {
              g.track = -1-j;
              g.media_stream = m->video_tracks.at(j).file_index;
              ghosts.append(g);
              video_ghosts = true;
            }
          }
        }
        break;
      case MEDIA_TYPE_SEQUENCE:
        g.out = entry_point + sequence_length - default_clip_in;

        if (s->using_workarea) {
          g.out -= (sequence_length - default_clip_out);
        }

        if (import_data.type() == amber::timeline::kImportVideoOnly
            || import_data.type() == amber::timeline::kImportBoth) {
          g.track = -1;
          ghosts.append(g);
        }

        if (import_data.type() == amber::timeline::kImportAudioOnly
            || import_data.type() == amber::timeline::kImportBoth) {
          g.track = 0;
          ghosts.append(g);
        }

        video_ghosts = true;
        audio_ghosts = true;
        break;
      }
      entry_point = g.out;
    }
  }
  for (auto & g : ghosts) {
    g.old_in = g.in;
    g.old_out = g.out;
    g.old_track = g.track;
  }
}

void Timeline::add_clips_from_ghosts(ComboAction* ca, Sequence* s) {
  // add clips
  long earliest_point = LONG_MAX;
  QVector<ClipPtr> added_clips;
  for (const auto & g : ghosts) {
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
  for (int i=0;i<added_clips.size();i++) {
    ClipPtr c = added_clips.at(i);
    for (int j=0;j<added_clips.size();j++) {
      ClipPtr cc = added_clips.at(j);
      if (c != cc && c->media() == cc->media()) {
        c->linked.append(j);
      }
    }

    if (amber::CurrentConfig.add_default_effects_to_clips) {
      if (c->track() < 0) {
        // add default video effects
        c->effects.append(Effect::Create(c.get(), Effect::GetInternalMeta(EFFECT_INTERNAL_TRANSFORM, EFFECT_TYPE_EFFECT)));
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
  ComboAction* ca = new ComboAction();
  bool adding = false;

  for (const auto & clip : amber::ActiveSequence->clips) {
    Clip* c = clip.get();
    if (c != nullptr && c->IsSelected()) {
      int transition_to_add = (c->track() < 0) ? TRANSITION_INTERNAL_CROSSDISSOLVE : TRANSITION_INTERNAL_LINEARFADE;
      if (c->opening_transition == nullptr) {
        ca->append(new AddTransitionCommand(c,
                                            nullptr,
                                            nullptr,
                                            Effect::GetInternalMeta(transition_to_add, EFFECT_TYPE_TRANSITION),
                                            amber::CurrentConfig.default_transition_length));
        adding = true;
      }
      if (c->closing_transition == nullptr) {
        ca->append(new AddTransitionCommand(nullptr,
                                            c,
                                            nullptr,
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

void Timeline::nest() {
  if (amber::ActiveSequence != nullptr) {
    // get selected clips
    QVector<int> selected_clips = amber::ActiveSequence->SelectedClipIndexes();

    // nest them
    if (!selected_clips.isEmpty()) {

      // get earliest point in selected clips
      long earliest_point = LONG_MAX;
      for (int selected_clip : selected_clips) {
        earliest_point = qMin(amber::ActiveSequence->clips.at(selected_clip)->timeline_in(), earliest_point);
      }

      ComboAction* ca = new ComboAction();

      // create "nest" sequence with the same attributes as the current sequence
      SequencePtr s = std::make_shared<Sequence>();

      s->name = panel_project->get_next_sequence_name(tr("Nested Sequence"));
      s->width = amber::ActiveSequence->width;
      s->height = amber::ActiveSequence->height;
      s->frame_rate = amber::ActiveSequence->frame_rate;
      s->audio_frequency = amber::ActiveSequence->audio_frequency;
      s->audio_layout = amber::ActiveSequence->audio_layout;

      // copy all selected clips to the nest
      for (int selected_clip : selected_clips) {
        // delete clip from old sequence
        ca->append(new DeleteClipAction(amber::ActiveSequence.get(), selected_clip));

        // copy to new
        ClipPtr copy(amber::ActiveSequence->clips.at(selected_clip)->copy(s.get()));
        copy->set_timeline_in(copy->timeline_in() - earliest_point);
        copy->set_timeline_out(copy->timeline_out() - earliest_point);
        s->clips.append(copy);
      }

      // relink clips in new nested sequences
      relink_clips_using_ids(selected_clips, s->clips);

      // add sequence to project
      MediaPtr m = panel_project->create_sequence_internal(ca, s, false, nullptr);

      // add nested sequence to active sequence
      QVector<amber::timeline::MediaImportData> media_list;
      media_list.append(m.get());
      create_ghosts_from_media(amber::ActiveSequence.get(), earliest_point, media_list);

      // ensure ghosts won't overlap anything
      for (int j=0;j<amber::ActiveSequence->clips.size();j++) {
        Clip* c = amber::ActiveSequence->clips.at(j).get();
        if (c != nullptr && !selected_clips.contains(j)) {
          for (auto & g : ghosts) {
            if (c->track() == g.track
                && !((c->timeline_in() < g.in
                && c->timeline_out() < g.in)
                || (c->timeline_in() > g.out
                    && c->timeline_out() > g.out))) {
              // There's a clip occupied by the space taken up by this ghost. Move up/down a track, and seek again
              if (g.track < 0) {
                g.track--;
              } else {
                g.track++;
              }
              j = -1;
              break;
            }
          }
        }
      }


      add_clips_from_ghosts(ca, amber::ActiveSequence.get());

      panel_graph_editor->set_row(nullptr);
      panel_effect_controls->Clear(true);
      amber::ActiveSequence->selections.clear();

      amber::UndoStack.push(ca);

      update_ui(true);
    }
  }
}

// update_sequence() moved to timeline_ui.cpp

int Timeline::get_snap_range() {
  return getFrameFromScreenPoint(zoom, 10);
}

// focused() moved to timeline_ui.cpp

// repaint_timeline() moved to timeline_ui.cpp

void Timeline::select_all() {
  if (amber::ActiveSequence != nullptr) {
    amber::ActiveSequence->selections.clear();
    for (int i=0;i<amber::ActiveSequence->clips.size();i++) {
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
  scroll_to_frame_internal(horizontalScrollBar, frame, zoom, timeline_area->width());
}

void Timeline::select_from_playhead() {
  amber::ActiveSequence->selections.clear();
  for (int i=0;i<amber::ActiveSequence->clips.size();i++) {
    ClipPtr c = amber::ActiveSequence->clips.at(i);
    if (c != nullptr
        && c->timeline_in() <= amber::ActiveSequence->playhead
        && c->timeline_out() > amber::ActiveSequence->playhead) {
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
    for (int i=video_tracks;i<=audio_tracks;i++) {
      Selection s;
      s.in = amber::ActiveSequence->workarea_in;
      s.out = amber::ActiveSequence->workarea_out;
      s.track = i;
      areas.append(s);
    }
    ComboAction* ca = new ComboAction();
    delete_areas_and_relink(ca, areas, true);
    if (ripple) ripple_clips(ca,
                             amber::ActiveSequence.get(),
                             amber::ActiveSequence->workarea_in,
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

void Timeline::delete_selection(QVector<Selection>& selections, bool ripple_delete) {
  if (selections.size() > 0) {
    panel_graph_editor->set_row(nullptr);
    panel_effect_controls->Clear(true);

    ComboAction* ca = new ComboAction();

    // delete the areas currently selected by `selections`
    // if we're ripple deleting, we don't want to delete the selections since we still need them for the ripple
    delete_areas_and_relink(ca, selections, !ripple_delete);

    if (ripple_delete) {
      long ripple_point = selections.at(0).in;
      long ripple_length = selections.at(0).out - selections.at(0).in;

      // retrieve ripple_point and ripple_length from current selection
      for (int i=1;i<selections.size();i++) {
        const Selection& s = selections.at(i);
        ripple_point = qMin(ripple_point, s.in);
        ripple_length = qMin(ripple_length, s.out - s.in);
      }

      // it feels a little more intuitive with this here
      ripple_point++;

      bool can_ripple = true;
      for (int i=0;i<amber::ActiveSequence->clips.size();i++) {
        ClipPtr c = amber::ActiveSequence->clips.at(i);
        if (c != nullptr && c->timeline_in() < ripple_point && c->timeline_out() > ripple_point) {
          // conflict detected, but this clip may be getting deleted so let's check
          bool deleted = false;
          for (const auto & s : selections) {
            if (s.track == c->track()
                && !(c->timeline_in() < s.in && c->timeline_out() < s.in)
                && !(c->timeline_in() > s.out && c->timeline_out() > s.out)) {
              deleted = true;
              break;
            }
          }
          if (!deleted) {
            for (auto cc : amber::ActiveSequence->clips) {
              if (cc != nullptr
                  && cc->track() == c->track()
                  && cc->timeline_in() > c->timeline_out()
                  && cc->timeline_in() < c->timeline_out() + ripple_length) {
                ripple_length = cc->timeline_in() - c->timeline_out();
              }
            }
          }
        }
      }

      if (can_ripple) {
        ripple_clips(ca, amber::ActiveSequence.get(), ripple_point, -ripple_length);
        panel_sequence_viewer->seek(ripple_point-1);
      }

      // if we're rippling, we can clear the selections here - if we're not rippling, delete_areas_and_relink() will
      // clear the selections for us
      selections.clear();
    }

    amber::UndoStack.push(ca);

    update_ui(true);
  }
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
}

void Timeline::multiply_zoom(double m) {
  showing_all = false;
  set_zoom_value(zoom * m);
}

void Timeline::decheck_tool_buttons(QObject* sender) {
  for (int i=0;i<tool_buttons.count();i++) {
    tool_buttons[i]->setChecked(tool_buttons.at(i) == sender);
  }
}

void Timeline::zoom_in() {
  multiply_zoom(2.0);
}

void Timeline::zoom_out() {
  multiply_zoom(0.5);
}

int Timeline::GetTrackHeight(int track) {
  for (auto track_height : track_heights) {
    if (track_height.index == track) {
      return track_height.height;
    }
  }
  return amber::timeline::kTrackDefaultHeight;
}

void Timeline::SetTrackHeight(int track, int height) {
  for (auto & track_height : track_heights) {
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
  for (int i=min_track;i<=max_track;i++) {
    SetTrackHeight(i, qMax(GetTrackHeight(i) + diff, amber::timeline::kTrackMinHeight));
  }

  // update the timeline
  repaint_timeline();
}

void Timeline::IncreaseTrackHeight() {
  ChangeTrackHeightUniformly(amber::timeline::kTrackHeightIncrement);
}

void Timeline::DecreaseTrackHeight() {
  ChangeTrackHeightUniformly(-amber::timeline::kTrackHeightIncrement);
}

void Timeline::snapping_clicked(bool checked) {
  snapping = checked;
}

// split_clip() (both overloads), split_clip_and_relink(), clean_up_selections(),
// selection_contains_transition() moved to timeline_splitting.cpp

void Timeline::delete_areas_and_relink(ComboAction* ca, QVector<Selection>& areas, bool deselect_areas) {
  clean_up_selections(areas);

  panel_graph_editor->set_row(nullptr);
  panel_effect_controls->Clear(true);

  QVector<int> pre_clips;
  QVector<ClipPtr> post_clips;

  for (const auto & s : areas) {
    for (int j=0;j<amber::ActiveSequence->clips.size();j++) {
      Clip* c = amber::ActiveSequence->clips.at(j).get();
      if (c != nullptr && c->track() == s.track && !c->undeletable) {
        if (selection_contains_transition(s, c, kTransitionOpening)) {
          // delete opening transition
          ca->append(new DeleteTransitionCommand(c->opening_transition));
        } else if (selection_contains_transition(s, c, kTransitionClosing)) {
          // delete closing transition
          ca->append(new DeleteTransitionCommand(c->closing_transition));
        } else if (c->timeline_in() >= s.in && c->timeline_out() <= s.out) {
          // clips falls entirely within deletion area
          ca->append(new DeleteClipAction(amber::ActiveSequence.get(), j));
        } else if (c->timeline_in() < s.in && c->timeline_out() > s.out) {
          // middle of clip is within deletion area

          // duplicate clip
          ClipPtr post = split_clip(ca, true, j, s.in, s.out);

          pre_clips.append(j);
          post_clips.append(post);
        } else if (c->timeline_in() < s.in && c->timeline_out() > s.in) {
          // only out point is in deletion area
          c->move(ca, c->timeline_in(), s.in, c->clip_in(), c->track());

          if (c->closing_transition != nullptr) {
            if (s.in < c->timeline_out() - c->closing_transition->get_true_length()) {
              ca->append(new DeleteTransitionCommand(c->closing_transition));
            } else {
              ca->append(new ModifyTransitionCommand(c->closing_transition, c->closing_transition->get_true_length() - (c->timeline_out() - s.in)));
            }
          }
        } else if (c->timeline_in() < s.out && c->timeline_out() > s.out) {
          // only in point is in deletion area
          c->move(ca, s.out, c->timeline_out(), c->clip_in() + (s.out - c->timeline_in()), c->track());

          if (c->opening_transition != nullptr) {
            if (s.out > c->timeline_in() + c->opening_transition->get_true_length()) {
              ca->append(new DeleteTransitionCommand(c->opening_transition));
            } else {
              ca->append(new ModifyTransitionCommand(c->opening_transition, c->opening_transition->get_true_length() - (s.out - c->timeline_in())));
            }
          }
        }
      }
    }
  }

  // deselect selected clip areas
  if (deselect_areas) {
    QVector<Selection> area_copy = areas;
    for (const auto & s : area_copy) {
      deselect_area(s.in, s.out, s.track);
    }
  }

  relink_clips_using_ids(pre_clips, post_clips);
  ca->append(new AddClipCommand(amber::ActiveSequence.get(), post_clips));
}

// copy(), relink_clips_using_ids(), paste() moved to timeline_clipboard.cpp

void Timeline::edit_to_point_internal(bool in, bool ripple) {
  if (amber::ActiveSequence != nullptr) {
    if (amber::ActiveSequence->clips.size() > 0) {
      // get track count
      int track_min = INT_MAX;
      int track_max = INT_MIN;
      long sequence_end = 0;

      bool playhead_falls_on_in = false;
      bool playhead_falls_on_out = false;
      long next_cut = LONG_MAX;
      long prev_cut = 0;

      // find closest in point to playhead
      for (int i=0;i<amber::ActiveSequence->clips.size();i++) {
        ClipPtr c = amber::ActiveSequence->clips.at(i);
        if (c != nullptr) {
          track_min = qMin(track_min, c->track());
          track_max = qMax(track_max, c->track());

          sequence_end = qMax(c->timeline_out(), sequence_end);

          if (c->timeline_in() == amber::ActiveSequence->playhead)
            playhead_falls_on_in = true;
          if (c->timeline_out() == amber::ActiveSequence->playhead)
            playhead_falls_on_out = true;
          if (c->timeline_in() > amber::ActiveSequence->playhead)
            next_cut = qMin(c->timeline_in(), next_cut);
          if (c->timeline_out() > amber::ActiveSequence->playhead)
            next_cut = qMin(c->timeline_out(), next_cut);
          if (c->timeline_in() < amber::ActiveSequence->playhead)
            prev_cut = qMax(c->timeline_in(), prev_cut);
          if (c->timeline_out() < amber::ActiveSequence->playhead)
            prev_cut = qMax(c->timeline_out(), prev_cut);
        }
      }

      next_cut = qMin(sequence_end, next_cut);

      QVector<Selection> areas;
      ComboAction* ca = new ComboAction();
      bool push_undo = true;
      long seek = amber::ActiveSequence->playhead;

      if ((in && (playhead_falls_on_out || (playhead_falls_on_in && amber::ActiveSequence->playhead == 0)))
          || (!in && (playhead_falls_on_in || (playhead_falls_on_out && amber::ActiveSequence->playhead == sequence_end)))) { // one frame mode
        if (ripple) {
          // set up deletion areas based on track count
          long in_point = amber::ActiveSequence->playhead;
          if (!in) {
            in_point--;
            seek--;
          }

          if (in_point >= 0) {
            Selection s;
            s.in = in_point;
            s.out = in_point + 1;
            for (int i=track_min;i<=track_max;i++) {
              s.track = i;
              areas.append(s);
            }

            // trim and move clips around the in point
            delete_areas_and_relink(ca, areas, true);

            if (ripple) ripple_clips(ca, amber::ActiveSequence.get(), in_point, -1);
          } else {
            push_undo = false;
          }
        } else {
          push_undo = false;
        }
      } else {
        // set up deletion areas based on track count
        Selection s;
        if (in) seek = prev_cut;
        s.in = in ? prev_cut : amber::ActiveSequence->playhead;
        s.out = in ? amber::ActiveSequence->playhead : next_cut;

        if (s.in == s.out) {
          push_undo = false;
        } else {
          for (int i=track_min;i<=track_max;i++) {
            s.track = i;
            areas.append(s);
          }

          // trim and move clips around the in point
          delete_areas_and_relink(ca, areas, true);
          if (ripple) ripple_clips(ca, amber::ActiveSequence.get(), s.in, s.in - s.out);
        }
      }

      if (push_undo) {
        amber::UndoStack.push(ca);

        update_ui(true);

        if (seek != amber::ActiveSequence->playhead && ripple) {
          panel_sequence_viewer->seek(seek);
        }
      } else {
        delete ca;
      }
    } else {
      panel_sequence_viewer->seek(0);
    }
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
  for (int i=0;i<len;i++) {
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
  if (*l > point-limit-1 && *l < point+limit+1) {
    snap_point = point;
    *l = point;
    snapped = true;
    return true;
  }
  return false;
}

bool Timeline::snap_to_timeline(long* l, bool use_playhead, bool use_markers, bool use_workarea, bool for_playhead) {
  snapped = false;
  if (snapping) {
    if (use_playhead && !panel_sequence_viewer->playing) {
      // snap to playhead
      if (snap_to_point(amber::ActiveSequence->playhead, l)) return true;
    }

    // snap to marker
    if (use_markers) {
      for (const auto & marker : amber::ActiveSequence->markers) {
        if (snap_to_point(marker.frame, l)) return true;
      }
    }

    // snap to in/out
    if (use_workarea && amber::ActiveSequence->using_workarea) {
      if (snap_to_point(amber::ActiveSequence->workarea_in, l)) return true;
      if (snap_to_point(amber::ActiveSequence->workarea_out, l)) return true;
    }

    // When seeking the playhead, snap to timeline_out - 1 so the viewer shows the
    // last frame of the outgoing clip. Hold modifier key to invert the setting.
    bool outgoing_pref = amber::CurrentConfig.snap_to_outgoing_clip;
    if (for_playhead) {
      static constexpr Qt::KeyboardModifier kModifiers[] = {Qt::ShiftModifier, Qt::ControlModifier, Qt::AltModifier};
      int mod_idx = qBound(0, amber::CurrentConfig.snap_outgoing_modifier, 2);
      if (QGuiApplication::keyboardModifiers() & kModifiers[mod_idx]) {
        outgoing_pref = !outgoing_pref;
      }
    }
    bool prefer_outgoing = for_playhead && outgoing_pref;

    // snap to clip/transition
    for (auto c : amber::ActiveSequence->clips) {
      if (c != nullptr) {
        if (snap_to_point(c->timeline_in(), l)) {
          return true;
        } else if (snap_to_point(prefer_outgoing ? c->timeline_out() - 1 : c->timeline_out(), l)) {
          return true;
        } else if (c->opening_transition != nullptr
                   && snap_to_point(c->timeline_in() + c->opening_transition->get_true_length(), l)) {
          return true;
        } else if (c->closing_transition != nullptr
                   && snap_to_point(c->timeline_out() - c->closing_transition->get_true_length(), l)) {
          return true;
        } else {
          // try to snap to clip markers
          for (int j=0;j<c->get_markers().size();j++) {
            if (snap_to_point(c->get_markers().at(j).frame + c->timeline_in() - c->clip_in(), l)) {
              return true;
            }
          }
        }
      }
    }
  }
  return false;
}

// set_marker() moved to timeline_ui.cpp

void Timeline::delete_inout() {
  panel_timeline->delete_in_out_internal(false);
}

void Timeline::ripple_delete_inout() {
  panel_timeline->delete_in_out_internal(true);
}

void Timeline::ripple_to_in_point() {
  panel_timeline->edit_to_point_internal(true, true);
}

void Timeline::ripple_to_out_point() {
  panel_timeline->edit_to_point_internal(false, true);
}

void Timeline::edit_to_in_point() {
  panel_timeline->edit_to_point_internal(true, false);
}

void Timeline::edit_to_out_point() {
  panel_timeline->edit_to_point_internal(false, false);
}

void Timeline::toggle_links() {
  LinkCommand* command = new LinkCommand();
  command->s = amber::ActiveSequence.get();
  for (int i=0;i<amber::ActiveSequence->clips.size();i++) {
    Clip* c = amber::ActiveSequence->clips.at(i).get();
    if (c != nullptr && c->IsSelected()) {
      if (!command->clips.contains(i)) command->clips.append(i);

      if (c->linked.size() > 0) {
        command->link = false; // prioritize unlinking

        for (int j : c->linked) { // add links to the command
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
  return qRound(double(frame)*zoom);
}

long Timeline::getTimelineFrameFromScreenPoint(int x) {
  return getFrameFromScreenPoint(zoom, x + scroll);
}

int Timeline::getTimelineScreenPointFromFrame(long frame) {
  return getScreenPointFromFrame(zoom, frame) - scroll;
}

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
    QMessageBox::critical(this,
                          tr("Unsaved Project"),
                          tr("You must save this project before you can record audio in it."),
                          QMessageBox::Ok);
  } else {
    creating = true;
    creating_object = ADD_OBJ_AUDIO;
    amber::MainWindow->statusBar()->showMessage(
          tr("Click on the timeline where you want to start recording (drag to limit the recording to a certain timeframe)"),
          10000);
  }
}

void Timeline::transition_tool_click() {
  creating = false;

  Menu transition_menu(this);

  for (const auto & em : effects) {
    if (em.type == EFFECT_TYPE_TRANSITION && em.subtype == EFFECT_TYPE_VIDEO) {
      QAction* a = transition_menu.addAction(em.name);
      a->setObjectName("v");
      a->setData(reinterpret_cast<quintptr>(&em));
    }
  }

  transition_menu.addSeparator();

  for (const auto & em : effects) {
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

void Timeline::resize_move(double z) {
  set_zoom_value(zoom * z);
}

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

void amber::timeline::MultiplyTrackSizesByDPI()
{
  kTrackDefaultHeight *= QGuiApplication::primaryScreen()->devicePixelRatio();
  kTrackMinHeight *= QGuiApplication::primaryScreen()->devicePixelRatio();
  kTrackHeightIncrement *= QGuiApplication::primaryScreen()->devicePixelRatio();
}
