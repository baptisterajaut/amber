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

#include "speeddialog.h"

#include <QDialogButtonBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>

#include "effects/effect.h"
#include "engine/sequence.h"
#include "engine/undo/undo.h"
#include "engine/undo/undostack.h"
#include "panels/panels.h"
#include "panels/timeline.h"
#include "project/footage.h"
#include "project/media.h"
#include "rendering/renderfunctions.h"

SpeedDialog::SpeedDialog(QWidget* parent, QVector<Clip*> clips) : QDialog(parent) {
  setWindowTitle(tr("Speed/Duration"));

  clips_ = clips;

  QVBoxLayout* main_layout = new QVBoxLayout(this);

  QGridLayout* grid = new QGridLayout();
  grid->setSpacing(6);

  grid->addWidget(new QLabel(tr("Speed:"), this), 0, 0);
  percent = new LabelSlider(this);
  percent->SetDecimalPlaces(2);
  percent->SetDisplayType(LabelSlider::Percent);
  percent->SetDefault(1);
  grid->addWidget(percent, 0, 1);

  grid->addWidget(new QLabel(tr("Frame Rate:"), this), 1, 0);
  frame_rate = new LabelSlider(this);
  frame_rate->SetDecimalPlaces(3);
  grid->addWidget(frame_rate, 1, 1);

  grid->addWidget(new QLabel(tr("Duration:"), this), 2, 0);
  duration = new LabelSlider(this);
  duration->SetDisplayType(LabelSlider::FrameNumber);
  if (amber::ActiveSequence != nullptr) {
    duration->SetFrameRate(amber::ActiveSequence->frame_rate);
  }
  grid->addWidget(duration, 2, 1);

  main_layout->addLayout(grid);

  reverse = new QCheckBox(tr("Reverse"), this);
  maintain_pitch = new QCheckBox(tr("Maintain Audio Pitch"), this);
  ripple = new QCheckBox(tr("Ripple Changes"), this);

  main_layout->addWidget(reverse);
  main_layout->addWidget(maintain_pitch);
  main_layout->addWidget(ripple);

  QHBoxLayout* loop_row = new QHBoxLayout();
  loop_row->addWidget(new QLabel(tr("Loop Mode:"), this));
  loop_combo_ = new QComboBox(this);
  loop_combo_->addItems({tr("None"), tr("Loop"), tr("Clamp")});
  loop_row->addWidget(loop_combo_);
  main_layout->addLayout(loop_row);

  QDialogButtonBox* buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
  buttonBox->setCenterButtons(true);
  main_layout->addWidget(buttonBox);
  connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
  connect(buttonBox, &QDialogButtonBox::accepted, this, &SpeedDialog::accept);

  connect(percent, &LabelSlider::valueChanged, this, &SpeedDialog::percent_update);
  connect(frame_rate, &LabelSlider::valueChanged, this, &SpeedDialog::frame_rate_update);
  connect(duration, &LabelSlider::valueChanged, this, &SpeedDialog::duration_update);
}

// Process a video clip during exec() initialisation.
// Updates frame-rate accumulators in-place.
static void init_exec_video_clip(Clip* c, bool& enable_frame_rate, double& default_frame_rate,
                                 double& current_frame_rate) {
  if (c->media() != nullptr && c->media()->get_type() == MEDIA_TYPE_FOOTAGE) {
    FootageStream* ms = c->media_stream();
    if (ms != nullptr && ms->infinite_length) return;
  }

  double media_frame_rate = c->media_frame_rate();
  if (enable_frame_rate) {
    if (!qIsNaN(default_frame_rate) && !qFuzzyCompare(media_frame_rate, default_frame_rate)) {
      default_frame_rate = qSNaN();
    }
    if (!qIsNaN(current_frame_rate) && !qFuzzyCompare(media_frame_rate * c->speed().value, current_frame_rate)) {
      current_frame_rate = qSNaN();
    }
  } else {
    default_frame_rate = media_frame_rate;
    current_frame_rate = media_frame_rate * c->speed().value;
    enable_frame_rate = true;
  }
}

// Process an audio clip during exec() initialisation.
// Updates maintain_pitch checkbox state.
void SpeedDialog::init_exec_audio_clip(Clip* c, bool& multiple_audio) {
  maintain_pitch->setEnabled(true);
  if (!multiple_audio) {
    maintain_pitch->setChecked(c->speed().maintain_audio_pitch);
    multiple_audio = true;
  } else if (!maintain_pitch->isTristate() && maintain_pitch->isChecked() != c->speed().maintain_audio_pitch) {
    maintain_pitch->setCheckState(Qt::PartiallyChecked);
    maintain_pitch->setTristate(true);
  }
}

void SpeedDialog::init_exec_loop_combo() {
  int first_loop = clips_.first()->loop_mode();
  for (int i = 1; i < clips_.size(); i++) {
    if (clips_.at(i)->loop_mode() != first_loop) {
      loop_combo_->setCurrentIndex(-1);  // mixed
      return;
    }
  }
  loop_combo_->setCurrentIndex(first_loop);
}

int SpeedDialog::exec() {
  bool enable_frame_rate = false;
  bool multiple_audio = false;
  maintain_pitch->setEnabled(false);

  double default_frame_rate = qSNaN();
  double current_frame_rate = qSNaN();
  double current_percent = qSNaN();
  long default_length = -1;
  long current_length = -1;

  for (int i = 0; i < clips_.size(); i++) {
    Clip* c = clips_.at(i);
    double clip_percent = c->speed().value;

    if (c->track() < 0) {
      init_exec_video_clip(c, enable_frame_rate, default_frame_rate, current_frame_rate);
    } else {
      init_exec_audio_clip(c, multiple_audio);
    }

    if (i == 0) {
      reverse->setChecked(c->reversed());
    } else if (c->reversed() != reverse->isChecked()) {
      reverse->setTristate(true);
      reverse->setCheckState(Qt::PartiallyChecked);
    }

    long clip_default_length = qRound(c->length() * clip_percent);
    if (i == 0) {
      current_length = c->length();
      default_length = clip_default_length;
      current_percent = clip_percent;
    } else {
      if (current_length != -1 && c->length() != current_length) current_length = -1;
      if (default_length != -1 && clip_default_length != default_length) default_length = -1;
      if (!qIsNaN(current_percent) && !qFuzzyCompare(clip_percent, current_percent)) current_percent = qSNaN();
    }
  }

  init_exec_loop_combo();

  frame_rate->SetMinimum(1);
  percent->SetMinimum(0.0001);
  duration->SetMinimum(1);

  frame_rate->setEnabled(enable_frame_rate);
  frame_rate->SetDefault(default_frame_rate);
  frame_rate->SetValue(current_frame_rate);
  percent->SetValue(current_percent);
  duration->SetDefault(default_length);
  duration->SetValue((current_length == -1) ? qSNaN() : current_length);

  return QDialog::exec();
}

void SpeedDialog::percent_update() {
  bool got_fr = false;
  double fr_val = qSNaN();
  long len_val = -1;

  for (int i = 0; i < clips_.size(); i++) {
    Clip* c = clips_.at(i);

    // get frame rate
    if (frame_rate->isEnabled() && c->track() < 0) {
      double clip_fr = c->media_frame_rate() * percent->value();
      if (got_fr) {
        if (!qIsNaN(fr_val) && !qFuzzyCompare(fr_val, clip_fr)) {
          fr_val = qSNaN();
        }
      } else {
        fr_val = clip_fr;
        got_fr = true;
      }
    }

    // get duration
    long clip_default_length = qRound(c->length() * c->speed().value);
    long new_clip_length = qRound(clip_default_length / percent->value());
    if (i == 0) {
      len_val = new_clip_length;
    } else if (len_val > -1 && len_val != new_clip_length) {
      len_val = -1;
    }
  }

  frame_rate->SetValue(fr_val);
  duration->SetValue((len_val == -1) ? qSNaN() : len_val);
}

void SpeedDialog::duration_update() {
  double pc_val = qSNaN();
  bool got_fr = false;
  double fr_val = qSNaN();

  for (int i = 0; i < clips_.size(); i++) {
    Clip* c = clips_.at(i);

    // get percent
    long clip_default_length = qRound(c->length() * c->speed().value);
    double clip_pc = clip_default_length / duration->value();
    if (i == 0) {
      pc_val = clip_pc;
    } else if (!qIsNaN(pc_val) && !qFuzzyCompare(clip_pc, pc_val)) {
      pc_val = qSNaN();
    }

    // get frame rate
    if (frame_rate->isEnabled() && c->track() < 0) {
      double clip_fr = c->media_frame_rate() * clip_pc;
      if (got_fr) {
        if (!qIsNaN(fr_val) && !qFuzzyCompare(fr_val, clip_fr)) {
          fr_val = qSNaN();
        }
      } else {
        fr_val = clip_fr;
        got_fr = true;
      }
    }
  }

  frame_rate->SetValue(fr_val);
  percent->SetValue(pc_val);
}

static long frame_rate_update_audio_len(const QVector<Clip*>& clips, double old_pc_val, double pc_val,
                                        long current_len_val) {
  long len_val = current_len_val;
  for (auto c : clips) {
    if (c->track() < 0) continue;
    long new_clip_len =
        (qIsNaN(old_pc_val) || qIsNaN(pc_val)) ? c->length() : qRound((c->length() * c->speed().value) / pc_val);
    if (len_val > -1 && new_clip_len != len_val) return -1;
  }
  return len_val;
}

void SpeedDialog::frame_rate_update() {
  double old_pc_val = qSNaN();
  bool got_pc_val = false;
  double pc_val = qSNaN();
  bool got_len_val = false;
  long len_val = -1;

  for (int i = 0; i < clips_.size(); i++) {
    Clip* c = clips_.at(i);

    if (i == 0) {
      old_pc_val = c->speed().value;
    } else if (!qIsNaN(old_pc_val) && !qFuzzyCompare(c->speed().value, old_pc_val)) {
      old_pc_val = qSNaN();
    }

    if (c->track() >= 0) continue;

    double new_clip_speed = frame_rate->value() / c->media_frame_rate();
    if (!got_pc_val) {
      pc_val = new_clip_speed;
      got_pc_val = true;
    } else if (!qIsNaN(pc_val) && !qFuzzyCompare(pc_val, new_clip_speed)) {
      pc_val = qSNaN();
    }

    long new_clip_len = (c->length() * c->speed().value) / new_clip_speed;
    if (!got_len_val) {
      len_val = new_clip_len;
      got_len_val = true;
    } else if (len_val > -1 && new_clip_len != len_val) {
      len_val = -1;
    }
  }

  len_val = frame_rate_update_audio_len(clips_, old_pc_val, pc_val, len_val);

  percent->SetValue(pc_val);
  duration->SetValue((len_val == -1) ? qSNaN() : len_val);
}

void set_speed(ComboAction* ca, Clip* c, double speed, bool ripple, long& ep, long& lr) {
  panel_timeline->deselect_area(c->timeline_in(), c->timeline_out(), c->track());

  long proposed_out = c->timeline_out();
  double multiplier = qFuzzyIsNull(speed) ? 1.0 : (c->speed().value / speed);
  proposed_out = qRound(c->timeline_in() + (c->length() * multiplier));
  ca->append(new SetSpeedAction(c, speed));
  if (!ripple && proposed_out > c->timeline_out()) {
    for (int i = 0; i < c->sequence->clips.size(); i++) {
      ClipPtr compare = c->sequence->clips.at(i);
      if (compare != nullptr && compare->track() == c->track() && compare->timeline_in() >= c->timeline_out() &&
          compare->timeline_in() < proposed_out) {
        proposed_out = compare->timeline_in();
      }
    }
  }
  ep = qMin(ep, c->timeline_out());
  lr = qMax(lr, proposed_out - c->timeline_out());
  c->move(ca, c->timeline_in(), proposed_out, qRound(c->clip_in() * multiplier), c->track());

  c->refactor_frame_rate(ca, multiplier, false);

  Selection sel;
  sel.in = c->timeline_in();
  sel.out = proposed_out;
  sel.track = c->track();
  amber::ActiveSequence->selections.append(sel);
}

void SpeedDialog::apply_speed_change(ComboAction* ca, long& earliest_point, long& longest_ripple) {
  if (!qIsNaN(percent->value())) {
    for (auto c : clips_) {
      set_speed(ca, c, percent->value(), ripple->isChecked(), earliest_point, longest_ripple);
    }
    return;
  }

  if (!qIsNaN(frame_rate->value())) {
    bool can_change_all = true;
    double cached_speed = clips_.first()->speed().value;
    double cached_fr = qSNaN();

    for (int i = 0; i < clips_.size(); i++) {
      Clip* c = clips_.at(i);
      if (i > 0 && !qFuzzyCompare(cached_speed, c->speed().value)) can_change_all = false;
      if (c->track() < 0) {
        if (qIsNaN(cached_fr)) {
          cached_fr = c->media_frame_rate();
        } else if (!qFuzzyCompare(cached_fr, c->media_frame_rate())) {
          can_change_all = false;
          break;
        }
      }
    }

    for (auto c : clips_) {
      if (c->track() < 0) {
        set_speed(ca, c, frame_rate->value() / c->media_frame_rate(), ripple->isChecked(), earliest_point,
                  longest_ripple);
      } else if (can_change_all) {
        set_speed(ca, c, frame_rate->value() / cached_fr, ripple->isChecked(), earliest_point, longest_ripple);
      }
    }
    return;
  }

  if (!qIsNaN(duration->value())) {
    for (auto c : clips_) {
      set_speed(ca, c, (c->length() * c->speed().value) / duration->value(), ripple->isChecked(), earliest_point,
                longest_ripple);
    }
  }
}

void SpeedDialog::accept() {
  ComboAction* ca = new ComboAction(tr("Change Speed"));
  SetClipProperty* audio_pitch_action = new SetClipProperty(kSetClipPropertyMaintainAudioPitch);
  SetClipProperty* reversed_action = new SetClipProperty(kSetClipPropertyReversed);
  SetSelectionsCommand* sel_command = new SetSelectionsCommand(amber::ActiveSequence.get());
  sel_command->old_data = amber::ActiveSequence->selections;

  long earliest_point = LONG_MAX;
  long longest_ripple = LONG_MIN;

  for (auto c : clips_) {
    if (c->IsOpen()) c->Close(true);

    if (c->track() >= 0 && maintain_pitch->checkState() != Qt::PartiallyChecked &&
        c->speed().maintain_audio_pitch != maintain_pitch->isChecked()) {
      audio_pitch_action->AddSetting(c, maintain_pitch->isChecked());
    }

    if (reverse->checkState() != Qt::PartiallyChecked && c->reversed() != reverse->isChecked()) {
      long new_clip_in = (c->media_length() - (c->length() + c->clip_in()));
      c->move(ca, c->timeline_in(), c->timeline_out(), new_clip_in, c->track());
      c->set_clip_in(new_clip_in);
      reversed_action->AddSetting(c, reverse->isChecked());
    }
  }

  apply_speed_change(ca, earliest_point, longest_ripple);

  if (ripple->isChecked()) {
    ripple_clips(ca, clips_.at(0)->sequence, earliest_point, longest_ripple);
  }

  sel_command->new_data = amber::ActiveSequence->selections;
  ca->append(sel_command);
  ca->append(reversed_action);
  ca->append(audio_pitch_action);

  if (loop_combo_->currentIndex() >= 0) {
    for (auto c : clips_) {
      if (c->loop_mode() != loop_combo_->currentIndex()) {
        ca->append(new SetInt(c->loop_mode_ptr(), loop_combo_->currentIndex()));
      }
    }
  }

  amber::UndoStack.push(ca);
  update_ui(true);
  QDialog::accept();
}
