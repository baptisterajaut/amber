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

#include "viewer.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
}

#include <QActionGroup>
#include <QAudioSink>
#include <QDrag>
#include <QHBoxLayout>
#include <QMenu>
#include <QMimeData>
#include <QPainter>
#include <QPushButton>
#include <QRegularExpression>
#include <QStringList>
#include <QTimer>
#include <QToolButton>
#include <QtMath>

#include "engine/clip.h"
#include "engine/sequence.h"
#include "engine/undo/undo.h"
#include "engine/undo/undostack.h"
#include "global/config.h"
#include "global/debug.h"
#include "global/global.h"
#include "panels/effectcontrols.h"
#include "panels/panels.h"
#include "panels/project.h"
#include "project/footage.h"
#include "project/media.h"
#include "rendering/audio.h"
#include "rendering/renderfunctions.h"
#include "timeline.h"
#include "ui/audiomonitor.h"
#include "ui/icons.h"
#include "ui/labelslider.h"
#include "ui/resizablescrollbar.h"
#include "ui/timelineheader.h"
#include "ui/viewercontainer.h"

#define FRAMES_IN_ONE_MINUTE 1798    // 1800 - 2
#define FRAMES_IN_TEN_MINUTES 17978  // (FRAMES_IN_ONE_MINUTE * 10) - 2

Viewer::Viewer(QWidget* parent)
    : Panel(parent),

      seq(nullptr)

{
  setup_ui();

  headers->viewer = this;
  headers->snapping = false;
  headers->show_text(false);
  viewer_container->viewer = this;
  viewer_widget = viewer_container->child;
  viewer_widget->viewer = this;
  set_media(nullptr);

  current_timecode_slider->setEnabled(false);
  current_timecode_slider->SetMinimum(0);
  current_timecode_slider->SetDefault(qSNaN());
  current_timecode_slider->SetValue(0);
  current_timecode_slider->SetDisplayType(LabelSlider::FrameNumber);
  connect(current_timecode_slider, &LabelSlider::valueChanged, this, &Viewer::update_playhead);

  recording_flasher.setInterval(500);

  connect(&playback_updater, &QTimer::timeout, this, &Viewer::timer_update);
  connect(&recording_flasher, &QTimer::timeout, this, &Viewer::recording_flasher_update);
  connect(horizontal_bar, &ResizableScrollBar::valueChanged, headers, &TimelineHeader::set_scroll);
  connect(horizontal_bar, &ResizableScrollBar::valueChanged, viewer_widget, &ViewerWidget::set_waveform_scroll);
  connect(horizontal_bar, &ResizableScrollBar::resize_move, this, &Viewer::resize_move);

  update_playhead_timecode(0);
  update_end_timecode();
}

Viewer::~Viewer() = default;

void Viewer::Retranslate() {
  /// Viewer panels are retranslated through the MainWindow to differentiate Media and Sequence Viewers
  //  update_window_title();
}

bool Viewer::is_focused() {
  return headers->hasFocus() || viewer_widget->hasFocus() || go_to_start_button->hasFocus() ||
         prev_frame_button->hasFocus() || play_button->hasFocus() || next_frame_button->hasFocus() ||
         go_to_end_frame->hasFocus();
}

bool Viewer::is_main_sequence() { return main_sequence; }

void Viewer::set_main_sequence() { set_sequence(true, amber::ActiveSequence); }

void Viewer::reset_all_audio() {
  if (seq != nullptr) {
    // Compute new buffer reference frame before any reset, so the cacher
    // sees the updated value when it observes audio_reset_ == true.
    long new_frame = seq->playhead;
    if (playback_speed < 0) {
      long last_frame = 0;
      for (auto c : seq->clips) {
        if (c != nullptr) {
          last_frame = qMax(last_frame, c->timeline_out());
        }
      }
      new_frame = last_frame - new_frame;
    }
    clear_audio_ibuffer(new_frame);

    for (auto c : seq->clips) {
      if (c != nullptr) {
        c->reset_audio();
      }
    }
  } else {
    clear_audio_ibuffer();
  }
}

long timecode_to_frame(const QString& s, int view, double frame_rate) {
  QList<QString> list = s.split(QRegularExpression("[:;]"));

  if (view == amber::kTimecodeFrames || (list.size() == 1 && view != amber::kTimecodeMilliseconds)) {
    return s.toLong();
  }

  int frRound = qRound(frame_rate);
  int hours, minutes, seconds, frames;

  if (view == amber::kTimecodeMilliseconds) {
    long milliseconds = s.toLong();

    hours = milliseconds / 3600000;
    milliseconds -= (hours * 3600000);
    minutes = milliseconds / 60000;
    milliseconds -= (minutes * 60000);
    seconds = milliseconds / 1000;
    milliseconds -= (seconds * 1000);
    frames = qRound64((milliseconds * 0.001) * frame_rate);

    seconds = qRound64(seconds * frame_rate);
    minutes = qRound64(minutes * frame_rate * 60);
    hours = qRound64(hours * frame_rate * 3600);
  } else {
    hours = ((list.size() > 0) ? list.at(0).toInt() : 0) * frRound * 3600;
    minutes = ((list.size() > 1) ? list.at(1).toInt() : 0) * frRound * 60;
    seconds = ((list.size() > 2) ? list.at(2).toInt() : 0) * frRound;
    frames = (list.size() > 3) ? list.at(3).toInt() : 0;
  }

  int f = (frames + seconds + minutes + hours);

  if ((view == amber::kTimecodeDrop || view == amber::kTimecodeMilliseconds) && frame_rate_is_droppable(frame_rate)) {
    // return drop
    int d;
    int m;

    int dropFrames =
        qRound(frame_rate *
               .066666);  // Number of frames to drop on the minute marks is the nearest integer to 6% of the framerate
    int framesPer10Minutes = qRound(frame_rate * 60 * 10);         // Number of frames per ten minutes
    int framesPerMinute = (qRound(frame_rate) * 60) - dropFrames;  // Number of frames per minute is the round of the
                                                                   // framerate * 60 minus the number of dropped frames

    d = f / framesPer10Minutes;
    f -= dropFrames * 9 * d;

    m = f % framesPer10Minutes;

    if (m > dropFrames) {
      f -= (dropFrames * ((m - dropFrames) / framesPerMinute));
    }
  }

  // return non-drop
  return f;
}

QString frame_to_timecode(long f, int view, double frame_rate) {
  if (view == amber::kTimecodeFrames) {
    return QString::number(f);
  }

  // return timecode
  int hours = 0;
  int mins = 0;
  int secs = 0;
  int frames = 0;
  QString token = ":";

  if ((view == amber::kTimecodeDrop || view == amber::kTimecodeMilliseconds) && frame_rate_is_droppable(frame_rate)) {
    // CONVERT A FRAME NUMBER TO DROP FRAME TIMECODE
    // Code by David Heidelberger, adapted from Andrew Duncan, further adapted for Olive by Olive Team
    // Given an int called framenumber and a double called framerate
    // Framerate should be 29.97, 59.94, or 23.976, otherwise the calculations will be off.

    int d;
    int m;

    int dropFrames =
        qRound(frame_rate *
               .066666);  // Number of frames to drop on the minute marks is the nearest integer to 6% of the framerate
    int framesPerHour = qRound(frame_rate * 60 * 60);  // Number of frqRound64ames in an hour
    int framesPer24Hours = framesPerHour * 24;         // Number of frames in a day - timecode rolls over after 24 hours
    int framesPer10Minutes = qRound(frame_rate * 60 * 10);         // Number of frames per ten minutes
    int framesPerMinute = (qRound(frame_rate) * 60) - dropFrames;  // Number of frames per minute is the round of the
                                                                   // framerate * 60 minus the number of dropped frames

    // If framenumber is greater than 24 hrs, next operation will rollover clock
    f = f % framesPer24Hours;  // % is the modulus operator, which returns a remainder. a % b = the remainder of a/b

    d = f / framesPer10Minutes;  // \ means integer division, which is a/b without a remainder. Some languages you could
                                 // use floor(a/b)
    m = f % framesPer10Minutes;

    // In the original post, the next line read m>1, which only worked for 29.97. Jean-Baptiste Mardelle correctly
    // pointed out that m should be compared to dropFrames.
    if (m > dropFrames) {
      f = f + (dropFrames * 9 * d) + dropFrames * ((m - dropFrames) / framesPerMinute);
    } else {
      f = f + dropFrames * 9 * d;
    }

    int frRound = qRound(frame_rate);
    frames = f % frRound;
    secs = (f / frRound) % 60;
    mins = ((f / frRound) / 60) % 60;
    hours = (((f / frRound) / 60) / 60);

    token = ";";
  } else {
    // non-drop timecode

    int int_fps = qRound(frame_rate);
    hours = f / (3600 * int_fps);
    mins = f / (60 * int_fps) % 60;
    secs = f / int_fps % 60;
    frames = f % int_fps;
  }
  if (view == amber::kTimecodeMilliseconds) {
    return QString::number((hours * 3600000) + (mins * 60000) + (secs * 1000) + qCeil(frames * 1000 / frame_rate));
  }
  return QString(QString::number(hours).rightJustified(2, '0') + ":" + QString::number(mins).rightJustified(2, '0') +
                 ":" + QString::number(secs).rightJustified(2, '0') + token +
                 QString::number(frames).rightJustified(2, '0'));
}

bool frame_rate_is_droppable(double rate) {
  return (qFuzzyCompare(rate, 23.976) || qFuzzyCompare(rate, 29.97) || qFuzzyCompare(rate, 59.94));
}

void Viewer::seek(long p, bool enable_scrub_audio) {
  if (seq == nullptr) return;
  pause(false);
  if (main_sequence) {
    seq->playhead = p;
  } else {
    seq->playhead = qMin(seq->getEndFrame(), qMax(0L, p));
  }
  bool update_fx = false;
  if (main_sequence) {
    panel_timeline->scroll_to_frame(p);
    panel_effect_controls->scroll_to_frame(p);
    if (amber::CurrentConfig.seek_also_selects) {
      panel_timeline->select_from_playhead();
      update_fx = true;
    }
  }
  bool fast_scrub = scrub_elapsed_valid_ && scrub_elapsed_.elapsed() < 16;
  scrub_elapsed_.start();
  scrub_elapsed_valid_ = true;

  // Only reset audio and trigger a new scrub grain when no grain is currently
  // playing.  While a grain is in flight the playhead moves freely but we don't
  // touch the audio buffer — same decoupling as video (viewer lags, playhead doesn't).
  if (enable_scrub_audio) {
    bool scrub_audio = amber::CurrentConfig.enable_audio_scrubbing && !fast_scrub && !audio_scrub_data_ready.load();
    if (scrub_audio) {
      reset_all_audio();
      audio_scrub_id.fetch_add(1);
    }
  }
  last_playhead = seq->playhead;
  update_parents(update_fx, fast_scrub);
}

void Viewer::go_to_start() {
  if (seq != nullptr) seek(0);
}

void Viewer::go_to_end() {
  if (seq != nullptr) seek(seq->getEndFrame());
}

void Viewer::close_media() { set_media(nullptr); }

void Viewer::go_to_in() {
  if (seq != nullptr) {
    if (seq->using_workarea) {
      seek(seq->workarea_in);
    } else {
      go_to_start();
    }
  }
}

void Viewer::previous_frame() {
  if (seq != nullptr && seq->playhead > 0) {
    precise_next_render_ = true;
    seek(seq->playhead - 1);
  }
}

void Viewer::next_frame() {
  if (seq != nullptr) {
    precise_next_render_ = true;
    seek(seq->playhead + 1);
  }
}

void Viewer::previous_frames() {
  if (seq != nullptr) {
    precise_next_render_ = true;
    seek(qMax(0L, seq->playhead - amber::CurrentConfig.frame_skip_step));
  }
}

void Viewer::next_frames() {
  if (seq != nullptr) {
    precise_next_render_ = true;
    seek(seq->playhead + amber::CurrentConfig.frame_skip_step);
  }
}

void Viewer::go_to_out() {
  if (seq != nullptr) {
    if (seq->using_workarea) {
      seek(seq->workarea_out);
    } else {
      go_to_end();
    }
  }
}

void Viewer::cue_recording(long start, long end, int track) {
  recording_start = start;
  recording_end = end;
  recording_track = track;
  panel_sequence_viewer->seek(recording_start);
  cue_recording_internal = true;
  recording_flasher.start();
}

void Viewer::uncue_recording() {
  cue_recording_internal = false;
  recording_flasher.stop();
  play_button->setStyleSheet(QString());
}

bool Viewer::is_recording_cued() { return cue_recording_internal; }

void Viewer::toggle_play() {
  if (playing) {
    pause();
  } else {
    play();
  }
}

void Viewer::increase_speed() {
  int new_speed = playback_speed + 1;
  if (new_speed == 0) {
    new_speed++;
  }
  set_playback_speed(new_speed);
}

void Viewer::decrease_speed() {
  int new_speed = playback_speed - 1;
  if (new_speed == 0) {
    new_speed--;
  }
  set_playback_speed(new_speed);
}

void Viewer::play(bool in_to_out) {
  if (seq == nullptr) {
    return;
  }

  if (panel_sequence_viewer->playing) panel_sequence_viewer->pause();
  if (panel_footage_viewer->playing) panel_footage_viewer->pause();

  long sequence_end_frame = seq->getEndFrame();
  if (sequence_end_frame == 0) {
    return;
  }

  playing_in_to_out = in_to_out;

  if (playing_in_to_out) {
    uncue_recording();
  }

  bool seek_to_in = (seq->using_workarea && (amber::CurrentConfig.loop || playing_in_to_out));
  if (!is_recording_cued() && playback_speed >= 0 &&
      (playing_in_to_out || (amber::CurrentConfig.auto_seek_to_beginning && seq->playhead >= sequence_end_frame) ||
       (seek_to_in && seq->playhead >= seq->workarea_out))) {
    seek(seek_to_in ? seq->workarea_in : 0);
  }

  if (playback_speed == 0) {
    playback_speed = 1;
  }

  reset_all_audio();
  if (is_recording_cued() && !start_recording()) {
    qCritical() << "Failed to record audio";
    return;
  }

  playhead_start = seq->playhead;
  playing = true;
  SetAudioWakeObject(this);
  set_playpause_icon(false);
  start_msecs = QDateTime::currentMSecsSinceEpoch();

  timer_update();
}

void Viewer::play_wake() {
  if (!playing) return;
  start_msecs = QDateTime::currentMSecsSinceEpoch();
  playback_updater.start();
  if (audio_thread != nullptr) audio_thread->notifyReceiver();
}

void Viewer::pause(bool clear_buffer) {
  playing = false;
  SetAudioWakeObject(nullptr);
  set_playpause_icon(true);
  playback_updater.stop();
  playback_speed = 0;

  // Flush audio buffer on pause to prevent stale samples from playing
  if (clear_buffer) {
    clear_audio_ibuffer();
  }

  // Reset VU meter immediately (null-check: panel_timeline is created after Viewer in alloc_panels)
  if (panel_timeline != nullptr && panel_timeline->audio_monitor != nullptr) {
    panel_timeline->audio_monitor->clear();
  }

  if (is_recording_cued()) {
    uncue_recording();

    if (recording) {
      stop_recording();

      // import audio
      QStringList file_list;
      file_list.append(get_recorded_audio_filename());
      panel_project->process_file_list(file_list);

      // add it to the sequence
      if (panel_project->last_imported_media.isEmpty()) return;
      ClipPtr c = std::make_shared<Clip>(seq.get());
      Media* m = panel_project->last_imported_media.at(0);
      Footage* f = m->to_footage();

      // wait for footage to be completely ready before taking metadata from it
      f->ready_lock.lock();

      c->set_media(m, 0);  // latest media
      c->set_timeline_in(recording_start);
      c->set_timeline_out(recording_start + f->get_length_in_frames(seq->frame_rate));
      c->set_clip_in(0);
      c->set_track(recording_track);
      c->set_color(128, 192, 128);
      c->set_name(m->get_name());

      f->ready_lock.unlock();

      QVector<ClipPtr> add_clips;
      add_clips.append(c);
      auto* cmd = new AddClipCommand(seq.get(), add_clips);
      cmd->setText(tr("Add Recorded Clip"));
      amber::UndoStack.push(cmd);  // add clip
    }
  }
}

void Viewer::update_playhead_timecode(long p) { current_timecode_slider->SetValue(p); }

void Viewer::update_end_timecode() {
  end_timecode->setText(
      (seq == nullptr) ? frame_to_timecode(0, amber::CurrentConfig.timecode_view, 30)
                       : frame_to_timecode(seq->getEndFrame(), amber::CurrentConfig.timecode_view, seq->frame_rate));
}

void Viewer::update_header_zoom() {
  if (seq != nullptr) {
    long sequenceEndFrame = seq->getEndFrame();
    if (cached_end_frame != sequenceEndFrame) {
      minimum_zoom = (sequenceEndFrame > 0) ? ((double)headers->width() / (double)sequenceEndFrame) : 1;
      headers->update_zoom(qMax(headers->get_zoom(), minimum_zoom));
      set_sb_max();
      viewer_widget->waveform_zoom = headers->get_zoom();
    } else {
      headers->update();
    }
  }
}

void Viewer::update_parents(bool reload_fx, bool scrubbing) {
  if (main_sequence) {
    update_ui(reload_fx, scrubbing);
  } else {
    update_viewer();
    panel_timeline->repaint_timeline();
  }
}

int Viewer::get_playback_speed() { return playback_speed; }

void Viewer::set_marker() { set_marker_internal(seq.get()); }

void Viewer::resizeEvent(QResizeEvent* e) {
  QDockWidget::resizeEvent(e);
  if (seq != nullptr) {
    set_sb_max();
    viewer_widget->update();
  }
}

void Viewer::update_viewer() {
  update_header_zoom();
  viewer_widget->frame_update();
  if (seq != nullptr) {
    update_playhead_timecode(seq->playhead);
  }
  update_end_timecode();
}

void Viewer::initiate_drag(amber::timeline::MediaImportType drag_type) {
  // FIXME: This should contain actual metadata rather than fake metadata

  QDrag* drag = new QDrag(this);
  QMimeData* mimeData = new QMimeData;
  mimeData->setText(QString::number(drag_type));
  drag->setMimeData(mimeData);
  drag->exec();
}

void Viewer::clear_in() {
  if (seq != nullptr && seq->using_workarea) {
    auto* cmd = new SetTimelineInOutCommand(seq.get(), true, 0, seq->workarea_out);
    cmd->setText(tr("Clear In Point"));
    amber::UndoStack.push(cmd);
    update_parents();
  }
}

void Viewer::clear_out() {
  if (seq != nullptr && seq->using_workarea) {
    auto* cmd = new SetTimelineInOutCommand(seq.get(), true, seq->workarea_in, seq->getEndFrame());
    cmd->setText(tr("Clear Out Point"));
    amber::UndoStack.push(cmd);
    update_parents();
  }
}

void Viewer::clear_inout_point() {
  if (seq != nullptr && seq->using_workarea) {
    auto* cmd = new SetTimelineInOutCommand(seq.get(), false, 0, 0);
    cmd->setText(tr("Clear In/Out"));
    amber::UndoStack.push(cmd);
    update_parents();
  }
}

void Viewer::set_in_point() {
  if (seq != nullptr) {
    headers->set_in_point(seq->playhead);
  }
}

void Viewer::set_out_point() {
  if (seq != nullptr) {
    headers->set_out_point(seq->playhead);
  }
}

void Viewer::set_zoom(bool in) {
  if (seq != nullptr) {
    set_zoom_value(in ? headers->get_zoom() * 2 : qMax(minimum_zoom, headers->get_zoom() * 0.5));
  }
}

void Viewer::set_panel_name(const QString& n) {
  panel_name = n;
  update_window_title();
}

void Viewer::show_videoaudio_buttons(bool s) {
  video_only_button->setVisible(s);
  audio_only_button->setVisible(s);
}

void Viewer::update_window_title() {
  QString name;
  if (seq == nullptr) {
    name = tr("(none)");
  } else {
    name = seq->name;
  }
  setWindowTitle(QString("%1: %2").arg(panel_name, name));
}

void Viewer::set_zoom_value(double d) {
  headers->update_zoom(d);
  if (viewer_widget->waveform) {
    viewer_widget->waveform_zoom = d;
    viewer_widget->update();
  }
  if (seq != nullptr) {
    set_sb_max();
    if (!horizontal_bar->is_resizing()) center_scroll_to_playhead(horizontal_bar, headers->get_zoom(), seq->playhead);
  }
}

void Viewer::set_sb_max() {
  if (seq == nullptr) return;
  headers->set_scrollbar_max(horizontal_bar, seq->getEndFrame(), headers->width());
}

void Viewer::set_playback_speed(int s) {
  int clamped = qMin(3, qMax(s, -3));
  if (clamped == playback_speed) return;  // no change, avoid needless audio reset
  pause();
  playback_speed = clamped;
  if (playback_speed != 0) {
    play();
  }
}

long Viewer::get_seq_in() {
  return ((amber::CurrentConfig.loop || playing_in_to_out) && seq->using_workarea) ? seq->workarea_in : 0;
}

long Viewer::get_seq_out() {
  return ((amber::CurrentConfig.loop || playing_in_to_out) && seq->using_workarea &&
          previous_playhead < seq->workarea_out)
             ? seq->workarea_out
             : seq->getEndFrame();
}

void Viewer::setup_ui() {
  QWidget* contents = new QWidget();

  QVBoxLayout* layout = new QVBoxLayout(contents);
  layout->setSpacing(0);
  layout->setContentsMargins(0, 0, 0, 0);

  setWidget(contents);

  viewer_container = new ViewerContainer();
  viewer_container->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
  layout->addWidget(viewer_container);

  headers = new TimelineHeader();
  layout->addWidget(headers);

  horizontal_bar = new ResizableScrollBar();
  horizontal_bar->setSingleStep(20);
  horizontal_bar->setOrientation(Qt::Horizontal);
  layout->addWidget(horizontal_bar);

  QWidget* lower_controls = new QWidget();

  QHBoxLayout* lower_control_layout = new QHBoxLayout(lower_controls);
  lower_control_layout->setContentsMargins(0, 0, 0, 0);

  QSizePolicy timecode_container_policy(QSizePolicy::Minimum, QSizePolicy::Maximum);
  QSizePolicy lower_control_policy(QSizePolicy::Maximum, QSizePolicy::Maximum);

  // Current time code container
  QWidget* current_timecode_container = new QWidget();
  current_timecode_container->setSizePolicy(timecode_container_policy);
  QHBoxLayout* current_timecode_container_layout = new QHBoxLayout(current_timecode_container);
  current_timecode_container_layout->setSpacing(0);
  current_timecode_container_layout->setContentsMargins(0, 0, 0, 0);
  current_timecode_slider = new LabelSlider();
  QFont timecode_font = current_timecode_slider->font();
  timecode_font.setBold(true);
  current_timecode_slider->setFont(timecode_font);
  current_timecode_container_layout->addWidget(current_timecode_slider);
  lower_control_layout->addWidget(current_timecode_container);

  // Left controls container
  QWidget* left_controls = new QWidget();
  left_controls->setSizePolicy(lower_control_policy);
  lower_control_layout->addWidget(left_controls);

  // Playback controls container
  QWidget* playback_controls = new QWidget();
  playback_controls->setSizePolicy(lower_control_policy);

  QHBoxLayout* playback_control_layout = new QHBoxLayout(playback_controls);
  playback_control_layout->setSpacing(0);
  playback_control_layout->setContentsMargins(0, 0, 0, 0);
  playback_control_layout->addStretch();

  go_to_start_button = new QPushButton();
  go_to_start_button->setIcon(amber::icon::ViewerGoToStart);
  connect(go_to_start_button, &QPushButton::clicked, this, &Viewer::go_to_in);
  playback_control_layout->addWidget(go_to_start_button);

  prev_frame_button = new QPushButton();
  prev_frame_button->setIcon(amber::icon::ViewerPrevFrame);
  connect(prev_frame_button, &QPushButton::clicked, this, &Viewer::previous_frame);
  playback_control_layout->addWidget(prev_frame_button);

  play_button = new QPushButton();
  play_button->setIcon(amber::icon::ViewerPlay);
  connect(play_button, &QPushButton::clicked, this, &Viewer::toggle_play);
  playback_control_layout->addWidget(play_button);

  next_frame_button = new QPushButton();
  next_frame_button->setIcon(amber::icon::ViewerNextFrame);
  connect(next_frame_button, &QPushButton::clicked, this, &Viewer::next_frame);
  playback_control_layout->addWidget(next_frame_button);

  go_to_end_frame = new QPushButton();
  go_to_end_frame->setIcon(amber::icon::ViewerGoToEnd);
  connect(go_to_end_frame, &QPushButton::clicked, this, &Viewer::go_to_out);
  playback_control_layout->addWidget(go_to_end_frame);

  playback_control_layout->addStretch();

  lower_control_layout->addWidget(playback_controls);

  // Right controls container
  QWidget* right_controls = new QWidget();
  right_controls->setSizePolicy(lower_control_policy);

  QHBoxLayout* right_control_layout = new QHBoxLayout(right_controls);
  right_control_layout->setSpacing(0);
  right_control_layout->setContentsMargins(0, 0, 0, 0);
  right_control_layout->addStretch();

  // Preview resolution toggle
  preview_res_button_ = new QToolButton();
  preview_res_button_->setPopupMode(QToolButton::InstantPopup);
  preview_res_button_->setToolTip(tr("Preview Resolution"));
  QMenu* preview_res_menu = new QMenu(preview_res_button_);
  QActionGroup* preview_res_group = new QActionGroup(preview_res_menu);
  auto add_res_action = [&](const QString& text, int divider) {
    QAction* a = preview_res_menu->addAction(text);
    a->setData(divider);
    a->setCheckable(true);
    a->setChecked(amber::CurrentConfig.preview_resolution_divider == divider);
    preview_res_group->addAction(a);
  };
  add_res_action(tr("Full"), 1);
  add_res_action(tr("1/2"), 2);
  add_res_action(tr("1/4"), 4);
  connect(preview_res_menu, &QMenu::triggered, this, &Viewer::set_preview_resolution);
  connect(preview_res_menu, &QMenu::aboutToShow, this, [preview_res_menu]() {
    int div = amber::CurrentConfig.preview_resolution_divider;
    for (QAction* a : preview_res_menu->actions()) {
      a->setChecked(a->data().toInt() == div);
    }
  });
  preview_res_button_->setMenu(preview_res_menu);
  update_preview_res_label();
  right_control_layout->addWidget(preview_res_button_);

  video_only_button = new QPushButton();
  video_only_button->setToolTip(tr("Drag video only"));
  video_only_button->setIcon(amber::icon::MediaVideo);
  video_only_button->setVisible(false);
  right_control_layout->addWidget(video_only_button);
  connect(video_only_button, &QPushButton::pressed, this, &Viewer::drag_video_only);

  audio_only_button = new QPushButton();
  audio_only_button->setToolTip(tr("Drag audio only"));
  audio_only_button->setIcon(amber::icon::MediaAudio);
  audio_only_button->setVisible(false);
  right_control_layout->addWidget(audio_only_button);
  connect(audio_only_button, &QPushButton::pressed, this, &Viewer::drag_audio_only);

  right_control_layout->addStretch();

  lower_control_layout->addWidget(right_controls);

  // End time code container
  QWidget* end_timecode_container = new QWidget();
  end_timecode_container->setSizePolicy(timecode_container_policy);

  QHBoxLayout* end_timecode_layout = new QHBoxLayout(end_timecode_container);
  end_timecode_layout->setSpacing(0);
  end_timecode_layout->setContentsMargins(0, 0, 0, 0);

  end_timecode = new QLabel();
  end_timecode->setFont(timecode_font);
  end_timecode->setAlignment(Qt::AlignRight | Qt::AlignTrailing | Qt::AlignVCenter);
  end_timecode_layout->addWidget(end_timecode);

  lower_control_layout->addWidget(end_timecode_container);

  layout->addWidget(lower_controls);
}

void Viewer::set_media(Media* m) {
  main_sequence = false;
  media = m;

  SequencePtr new_sequence = nullptr;

  if (media != nullptr) {
    switch (media->get_type()) {
      case MEDIA_TYPE_FOOTAGE: {
        Footage* footage = media->to_footage();

        marker_ref = &footage->markers;

        new_sequence = std::make_shared<Sequence>();
        created_sequence = true;
        new_sequence->wrapper_sequence = true;
        new_sequence->name = footage->name;

        new_sequence->using_workarea = footage->using_inout;
        if (footage->using_inout) {
          new_sequence->workarea_in = footage->in;
          new_sequence->workarea_out = footage->out;
        }

        new_sequence->frame_rate = amber::CurrentConfig.default_sequence_framerate;

        if (footage->video_tracks.size() > 0) {
          const FootageStream& video_stream = footage->video_tracks.at(0);
          new_sequence->width = video_stream.video_width;
          new_sequence->height = video_stream.video_height;
          if (video_stream.video_frame_rate > 0 && !video_stream.infinite_length) {
            new_sequence->frame_rate = video_stream.video_frame_rate * footage->speed;
          }

          ClipPtr c = std::make_shared<Clip>(new_sequence.get());
          c->set_media(media, video_stream.file_index);
          c->set_timeline_in(0);
          c->set_timeline_out(footage->get_length_in_frames(new_sequence->frame_rate));
          if (c->timeline_out() <= 0) {
            c->set_timeline_out(amber::CurrentConfig.default_still_length);
          }
          c->set_track(-1);
          c->set_clip_in(0);
          c->refresh();
          new_sequence->clips.append(c);
        } else {
          new_sequence->width = amber::CurrentConfig.default_sequence_width;
          new_sequence->height = amber::CurrentConfig.default_sequence_height;
        }

        if (footage->audio_tracks.size() > 0) {
          const FootageStream& audio_stream = footage->audio_tracks.at(0);
          new_sequence->audio_frequency = audio_stream.audio_frequency;

          ClipPtr c = std::make_shared<Clip>(new_sequence.get());
          c->set_media(media, audio_stream.file_index);
          c->set_timeline_in(0);
          c->set_timeline_out(footage->get_length_in_frames(new_sequence->frame_rate));
          c->set_track(0);
          c->set_clip_in(0);
          c->refresh();
          new_sequence->clips.append(c);

          if (footage->video_tracks.size() == 0) {
            viewer_widget->waveform = true;
            viewer_widget->waveform_clip = c;
            viewer_widget->waveform_ms = &audio_stream;
            viewer_widget->frame_update();
          }
        } else {
          new_sequence->audio_frequency = amber::CurrentConfig.default_sequence_audio_frequency;
        }

        new_sequence->audio_layout = AV_CH_LAYOUT_STEREO;
      } break;
      case MEDIA_TYPE_SEQUENCE:
        new_sequence = media->to_sequence();
        break;
    }
  }

  set_sequence(false, new_sequence);
}

void Viewer::update_playhead() { seek(current_timecode_slider->value()); }

void Viewer::timer_update() {
  previous_playhead = seq->playhead;

  seq->playhead = qMax(0, qRound(playhead_start + ((QDateTime::currentMSecsSinceEpoch() - start_msecs) * 0.001 *
                                                   seq->frame_rate * playback_speed)));

  if (amber::CurrentConfig.seek_also_selects) {
    panel_timeline->select_from_playhead();
  }

  update_parents(amber::CurrentConfig.seek_also_selects);

  if (playing) {
    if (playback_speed < 0 && seq->playhead == 0) {
      pause();
    } else if (recording) {
      if (recording_start != recording_end && seq->playhead >= recording_end) {
        pause();
      }
    } else if (playback_speed > 0) {
      long end_frame = seq->getEndFrame();
      if ((amber::CurrentConfig.auto_seek_to_beginning || previous_playhead < end_frame) &&
          seq->playhead >= end_frame) {
        pause();
      }
      if (seq->using_workarea && seq->playhead >= seq->workarea_out) {
        if (amber::CurrentConfig.loop) {
          // loop
          play();
        } else if (playing_in_to_out) {
          pause();
        }
      }
    }
  }
}

void Viewer::recording_flasher_update() {
  if (play_button->styleSheet().isEmpty()) {
    play_button->setStyleSheet("background: red;");
  } else {
    play_button->setStyleSheet(QString());
  }
}

void Viewer::resize_move(double d) { set_zoom_value(headers->get_zoom() * d); }

void Viewer::drag_video_only() { initiate_drag(amber::timeline::kImportVideoOnly); }

void Viewer::drag_audio_only() { initiate_drag(amber::timeline::kImportAudioOnly); }

void Viewer::set_preview_resolution(QAction* action) {
  amber::CurrentConfig.preview_resolution_divider = action->data().toInt();
  update_preview_res_label();
  panel_sequence_viewer->update_parents();
}

void Viewer::update_preview_res_label() {
  int div = amber::CurrentConfig.preview_resolution_divider;
  switch (div) {
    case 2: preview_res_button_->setText(tr("1/2")); break;
    case 4: preview_res_button_->setText(tr("1/4")); break;
    default: preview_res_button_->setText(tr("Full")); break;
  }
}

void Viewer::clean_created_seq() {
  viewer_widget->waveform = false;

  if (created_sequence) {
    // TODO delete undo commands referencing this sequence to avoid crashes
    /*
    for (int i=0;i<amber::UndoStack.count();i++) {
      const QUndoCommand* oa = amber::UndoStack.command(i);
      if (typeid(*oa) == typeid(SetTimelineInOutCommand)) {
      }
    }
    */

    // Delete the current sequence
    seq.reset();

    created_sequence = false;
  }
}

void Viewer::set_sequence(bool main, SequencePtr s) {
  pause();

  reset_all_audio();

  viewer_widget->wait_until_render_is_paused();

  // If we had a current sequence open, close it
  if (seq != nullptr) {
    close_active_clips(seq.get());
  }

  clean_created_seq();

  main_sequence = main;

  seq = (main) ? amber::ActiveSequence : s;

  bool null_sequence = (seq == nullptr);

  headers->setEnabled(!null_sequence);
  current_timecode_slider->setEnabled(!null_sequence);
  viewer_widget->setEnabled(!null_sequence);
  viewer_widget->setVisible(!null_sequence);
  go_to_start_button->setEnabled(!null_sequence);
  prev_frame_button->setEnabled(!null_sequence);
  play_button->setEnabled(!null_sequence);
  next_frame_button->setEnabled(!null_sequence);
  go_to_end_frame->setEnabled(!null_sequence);
  video_only_button->setEnabled(!null_sequence);
  audio_only_button->setEnabled(!null_sequence);

  if (!null_sequence) {
    current_timecode_slider->SetFrameRate(seq->frame_rate);

    playback_updater.setInterval(qFloor(1000 / seq->frame_rate));

    update_playhead_timecode(seq->playhead);
    update_end_timecode();

    viewer_container->adjust();

    if (!created_sequence) {
      marker_ref = &seq->markers;
    }
  } else {
    update_playhead_timecode(0);
    update_end_timecode();
  }

  update_window_title();

  update_header_zoom();

  viewer_widget->frame_update();

  update();
}

void Viewer::set_playpause_icon(bool play) {
  play_button->setIcon(play ? amber::icon::ViewerPlay : amber::icon::ViewerPause);
}
