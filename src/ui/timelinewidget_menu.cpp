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

#include <QMessageBox>
#include <QToolTip>

#include "global/config.h"
#include "global/global.h"
#include "panels/panels.h"
#include "rendering/renderfunctions.h"
#include "ui/colorlabel.h"
#include "ui/menuhelper.h"
#include "ui/menu.h"
#include "engine/undo/undo_generic.h"
#include "engine/undo/undostack.h"
#include "dialogs/newsequencedialog.h"
#include "dialogs/clippropertiesdialog.h"

void TimelineWidget::show_context_menu(const QPoint& pos) {
  if (amber::ActiveSequence != nullptr) {
    // hack because sometimes right clicking doesn't trigger mouse release event
    panel_timeline->rect_select_init = false;
    panel_timeline->rect_select_proc = false;

    Menu menu(this);

    QAction* undoAction = menu.addAction(tr("&Undo"));
    QAction* redoAction = menu.addAction(tr("&Redo"));
    connect(undoAction, &QAction::triggered, amber::Global.get(), &OliveGlobal::undo);
    connect(redoAction, &QAction::triggered, amber::Global.get(), &OliveGlobal::redo);
    undoAction->setEnabled(amber::UndoStack.canUndo());
    redoAction->setEnabled(amber::UndoStack.canRedo());
    menu.addSeparator();

    // collect all the selected clips
    QVector<Clip*> selected_clips = amber::ActiveSequence->SelectedClips();

    amber::MenuHelper.make_edit_functions_menu(&menu, !selected_clips.isEmpty());

    if (selected_clips.isEmpty()) {
      // no clips are selected

      // determine if we can perform a ripple empty space
      panel_timeline->cursor_frame = panel_timeline->getTimelineFrameFromScreenPoint(pos.x());
      panel_timeline->cursor_track = getTrackFromScreenPoint(pos.y());

      if (panel_timeline->can_ripple_empty_space(panel_timeline->cursor_frame, panel_timeline->cursor_track)) {
        QAction* ripple_delete_action = menu.addAction(tr("R&ipple Delete Empty Space"));
        connect(ripple_delete_action, &QAction::triggered, panel_timeline, &Timeline::ripple_delete_empty_space);
      }

      QAction* seq_settings = menu.addAction(tr("Sequence Settings"));
      connect(seq_settings, &QAction::triggered, this, &TimelineWidget::open_sequence_properties);
    }

    if (!selected_clips.isEmpty()) {

      bool video_clips_are_selected = false;
      bool audio_clips_are_selected = false;

      for (auto selected_clip : selected_clips) {
        if (selected_clip->track() < 0) {
          video_clips_are_selected = true;
        } else {
          audio_clips_are_selected = true;
        }
      }

      menu.addSeparator();

      menu.addAction(tr("&Speed/Duration"), amber::Global.get(), &OliveGlobal::open_speed_dialog);

      if (audio_clips_are_selected) {
        menu.addAction(tr("Auto-Cut Silence"), amber::Global.get(), &OliveGlobal::open_autocut_silence_dialog);
      }

      QAction* autoscaleAction = menu.addAction(tr("Auto-S&cale"), this, &TimelineWidget::toggle_autoscale);
      autoscaleAction->setCheckable(true);
      // set autoscale to the first selected clip
      autoscaleAction->setChecked(selected_clips.at(0)->autoscaled());

      if (amber::CurrentConfig.show_color_labels) {
        QMenu* color_menu = amber::BuildColorLabelMenu(&menu);
        connect(color_menu, &QMenu::triggered, this, [selected_clips](QAction* action) {
          int label = action->data().toInt();
          ComboAction* ca = new ComboAction(QObject::tr("Set Color Label"));
          for (auto c : selected_clips) {
            ca->append(new SetInt(c->color_label_ptr(), label));
          }
          amber::UndoStack.push(ca);
          update_ui(false);
        });
        menu.addMenu(color_menu);
      }

      amber::MenuHelper.make_clip_functions_menu(&menu);

      // stabilizer option
      /*int video_clip_count = 0;
      bool all_video_is_footage = true;
      for (int i=0;i<selected_clips.size();i++) {
        if (selected_clips.at(i)->track() < 0) {
          video_clip_count++;
          if (selected_clips.at(i)->media() == nullptr
              || selected_clips.at(i)->media()->get_type() != MEDIA_TYPE_FOOTAGE) {
            all_video_is_footage = false;
          }
        }
      }
      if (video_clip_count == 1 && all_video_is_footage) {
        QAction* stabilizerAction = menu.addAction("S&tabilizer");
        connect(stabilizerAction, SIGNAL(triggered(bool)), this, SLOT(show_stabilizer_diag()));
      }*/

      // check if all selected clips have the same media for a "Reveal In Project"
      bool same_media = true;
      rc_reveal_media = selected_clips.at(0)->media();
      for (int i=1;i<selected_clips.size();i++) {
        if (selected_clips.at(i)->media() != rc_reveal_media) {
          same_media = false;
          break;
        }
      }

      if (same_media) {
        QAction* revealInProjectAction = menu.addAction(tr("&Reveal in Project"));
        connect(revealInProjectAction, &QAction::triggered, this, &TimelineWidget::reveal_media);
      }

      menu.addAction(tr("Properties"), this, &TimelineWidget::show_clip_properties);

      menu.addSeparator();
      QAction* match_frame_action = menu.addAction(tr("Match Frame"));
      connect(match_frame_action, &QAction::triggered, panel_timeline, &Timeline::match_frame);
    }

    menu.exec(mapToGlobal(pos));
  }
}

void TimelineWidget::toggle_autoscale() {
  QVector<Clip*> selected_clips = amber::ActiveSequence->SelectedClips();

  if (!selected_clips.isEmpty()) {
    SetClipProperty* action = new SetClipProperty(kSetClipPropertyAutoscale);
    action->setText(tr("Set Autoscale"));

    for (auto c : selected_clips) {
      action->AddSetting(c, !c->autoscaled());
    }

    amber::UndoStack.push(action);
  }
}

void TimelineWidget::tooltip_timer_timeout() {
  if (amber::ActiveSequence != nullptr) {
    if (tooltip_clip < amber::ActiveSequence->clips.size()) {
      ClipPtr c = amber::ActiveSequence->clips.at(tooltip_clip);
      if (c != nullptr) {
        QToolTip::showText(QCursor::pos(),
                           tr("%1\nStart: %2\nEnd: %3\nDuration: %4").arg(
                             c->name(),
                             frame_to_timecode(c->timeline_in(), amber::CurrentConfig.timecode_view, amber::ActiveSequence->frame_rate),
                             frame_to_timecode(c->timeline_out(), amber::CurrentConfig.timecode_view, amber::ActiveSequence->frame_rate),
                             frame_to_timecode(c->length(), amber::CurrentConfig.timecode_view, amber::ActiveSequence->frame_rate)
                             ));
      }
    }
  }
  tooltip_timer.stop();
}

void TimelineWidget::open_sequence_properties() {
  QList<Media*> sequence_items;
  QList<Media*> all_top_level_items;
  for (int i=0;i<amber::project_model.childCount();i++) {
    all_top_level_items.append(amber::project_model.child(i));
  }
  panel_project->get_all_media_from_table(all_top_level_items, sequence_items, MEDIA_TYPE_SEQUENCE); // find all sequences in project
  for (auto sequence_item : sequence_items) {
    if (sequence_item->to_sequence() == amber::ActiveSequence) {
      NewSequenceDialog nsd(this, sequence_item);
      nsd.exec();
      return;
    }
  }
  QMessageBox::critical(this, tr("Error"), tr("Couldn't locate media wrapper for sequence."));
}

void TimelineWidget::show_clip_properties()
{
  // get list of selected clips
  QVector<Clip*> selected_clips = amber::ActiveSequence->SelectedClips();

  // if clips are selected, open the clip properties dialog
  if (!selected_clips.isEmpty()) {
    ClipPropertiesDialog cpd(this, selected_clips);
    cpd.exec();
  }
}

void TimelineWidget::reveal_media() {
  panel_project->reveal_media(rc_reveal_media);
}
