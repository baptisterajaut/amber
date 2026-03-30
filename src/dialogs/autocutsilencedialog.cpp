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

#include "autocutsilencedialog.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>

#include "engine/clip.h"
#include "engine/sequence.h"
#include "engine/undo/undo_clip.h"
#include "panels/panels.h"
#include "panels/timeline.h"
#include "rendering/renderfunctions.h"

AutoCutSilenceDialog::AutoCutSilenceDialog(QWidget* parent, QVector<int> clips) : QDialog(parent), clips_(clips) {
  setWindowTitle(tr("Cut Silence"));

  QVBoxLayout* main_layout = new QVBoxLayout(this);
  QGridLayout* grid = new QGridLayout();
  grid->setSpacing(6);

  grid->addWidget(new QLabel(tr("Attack Threshold:"), this), 0, 0);
  attack_threshold = new LabelSlider(this);
  attack_threshold->SetDecimalPlaces(0);
  grid->addWidget(attack_threshold, 0, 1);

  grid->addWidget(new QLabel(tr("Attack Time:"), this), 1, 0);
  attack_time = new LabelSlider(this);
  attack_time->SetDecimalPlaces(0);
  grid->addWidget(attack_time, 1, 1);

  grid->addWidget(new QLabel(tr("Release Threshold:"), this), 2, 0);
  release_threshold = new LabelSlider(this);
  release_threshold->SetDecimalPlaces(0);
  grid->addWidget(release_threshold, 2, 1);

  grid->addWidget(new QLabel(tr("Release Time:"), this), 3, 0);
  release_time = new LabelSlider(this);
  release_time->SetDecimalPlaces(0);
  grid->addWidget(release_time, 3, 1);

  main_layout->addLayout(grid);

  ripple_delete_checkbox = new QCheckBox(tr("Ripple Delete Silence Cuts"), this);
  ripple_delete_checkbox->setChecked(false);
  main_layout->addWidget(ripple_delete_checkbox);

  QHBoxLayout* gap_layout = new QHBoxLayout();
  gap_layout->addWidget(new QLabel(tr("Gap Between Clips (frames):"), this));
  gap_size_spinbox = new QSpinBox(this);
  gap_size_spinbox->setMinimum(0);
  gap_size_spinbox->setMaximum(1000);
  gap_size_spinbox->setValue(0);
  gap_layout->addWidget(gap_size_spinbox);
  gap_layout->addStretch();
  main_layout->addLayout(gap_layout);

  QDialogButtonBox* buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
  buttonBox->setCenterButtons(true);
  main_layout->addWidget(buttonBox);
  connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
  connect(buttonBox, &QDialogButtonBox::accepted, this, &AutoCutSilenceDialog::accept);
}

int AutoCutSilenceDialog::exec() {
  default_attack_threshold = 5;
  current_attack_threshold = 5;
  default_attack_time = 2;
  current_attack_time = 2;
  default_release_threshold = 2;
  current_release_threshold = 2;
  default_release_time = 5;
  current_release_time = 5;
  ripple_delete_enabled = false;
  current_gap_size = 0;

  attack_threshold->SetMinimum(1);
  attack_threshold->setEnabled(true);
  attack_threshold->SetDefault(default_attack_threshold);
  attack_threshold->SetValue(current_attack_threshold);

  attack_time->SetMinimum(1);
  attack_time->setEnabled(true);
  attack_time->SetDefault(default_attack_time);
  attack_time->SetValue(current_attack_time);

  release_threshold->SetMinimum(1);
  release_threshold->setEnabled(true);
  release_threshold->SetDefault(default_release_threshold);
  release_threshold->SetValue(current_release_threshold);

  release_time->SetMinimum(1);
  release_time->setEnabled(true);
  release_time->SetDefault(default_release_time);
  release_time->SetValue(current_release_time);

  // Load saved settings
  QSettings settings;
  ripple_delete_checkbox->setChecked(settings.value("AutoCutSilence/RippleDeleteEnabled", false).toBool());
  gap_size_spinbox->setValue(settings.value("AutoCutSilence/GapSize", 0).toInt());

  return QDialog::exec();
}

void AutoCutSilenceDialog::accept() {
  current_attack_threshold = attack_threshold->value();
  current_attack_time = attack_time->value();
  current_release_threshold = release_threshold->value();
  current_release_time = release_time->value();
  ripple_delete_enabled = ripple_delete_checkbox->isChecked();
  current_gap_size = gap_size_spinbox->value();

  // Save settings
  QSettings settings;
  settings.setValue("AutoCutSilence/RippleDeleteEnabled", ripple_delete_enabled);
  settings.setValue("AutoCutSilence/GapSize", current_gap_size);

  cut_silence();

  update_ui(true);
  QDialog::accept();
}

void AutoCutSilenceDialog::cut_silence() {
  if (amber::ActiveSequence == nullptr) return;

  ComboAction* ca = new ComboAction(tr("Auto-Cut Silence"));
  QVector<Selection> silence_selections;  // Store silence segment boundaries for later deletion

  // First pass: analyze all clips and collect split positions
  for (int j : clips_) {
    Clip* clip = amber::ActiveSequence->clips.at(j).get();

    // Check if this clip is an audio footage clip
    if (clip->track() >= 0 && clip->media() != nullptr &&
        clip->media_stream()->preview_done) {  // TODO provide warning for preview not being done

      QVector<long> split_positions;

      int clip_start = clip->timeline_in();
      const FootageStream* ms = clip->media_stream();

      long media_length = clip->media_length();
      int preview_size = ms->audio_preview.length();
      float chunk_size = (float)preview_size / media_length;  // how many audio samples to read for each fotogram

      int sample_size = qMax(current_attack_time, current_release_time) + 1;

      bool attack = false;  // status flags
      bool release = false;

      QVector<qint8> vols;
      vols.resize(sample_size);
      vols.fill(0);

      // loop through the entire sequence
      for (long i = clip_start; i < media_length + clip_start; i++) {
        long start = ((i - clip_start) *
                      chunk_size);  // audio samples are read relative to the clip, not absolute to the timeline
        int circular_index = i % sample_size;

        // read the current sample into the circular array
        qint8 tmp = 0;
        for (int k = start; k < start + chunk_size; k++) {
          tmp = qMax(tmp, qint8(qRound(double(ms->audio_preview.at(k)))));
        }
        vols[circular_index] = tmp;

        // for debug:
        // qInfo() << "i:" << i <<" - "<< i/30 <<":"<< i%30 << " - volume:" << vols[circular_index] <<"\n";

        int overthreshold = 0;
        int cut_idx = 0;  // how much to cut (backwards)

        // if current volume value is above threshold
        if (vols[circular_index] >= current_attack_threshold && !attack) {  // if we get one sample over the threshold
          for (int k = 0; k < sample_size;
               k++) {  // count how many times this happened before within sample_size range (avoid false activations)
            int back_idx = (((circular_index - k) % sample_size) + sample_size) % sample_size;  // positive modulus
            if (vols[back_idx] > current_attack_threshold) {
              overthreshold++;
              cut_idx = k + 1;
            }
          }
          // if we reached threshold over the set tolerance
          if (overthreshold >= current_attack_time) {
            split_positions.append(i - cut_idx);
            attack = true;
            release = false;
            // qInfo() << "\n\n Current vol: "<<vols[circular_index]<<" attack at " << i-cut_idx << "\n\n";
          }
          overthreshold = 0;
          cut_idx = 0;
        } else if (vols[circular_index] < current_release_threshold &&
                   !release) {                     // if we get one sample under the threshold
          for (int k = 0; k < sample_size; k++) {  // count how many times this happened before within sample_size range
            int back_idx = (((circular_index - k) % sample_size) + sample_size) % sample_size;  // positive modulus
            if (vols[back_idx] < current_release_threshold) overthreshold++;
          }
          // if we reached threshold over the set tolerance
          if (overthreshold >= current_release_time) {  // must be <= sample_size
            attack = false;
            release = true;
            split_positions.append(i);
            // qInfo() << "\n\n Current vol: "<<vols[circular_index]<<" release at " << i << "\n\n";
          }
          overthreshold = 0;
        }
      }

      // If ripple delete is enabled, build silence segment selections from split positions
      // Splits come in pairs: [silence_start, silence_end, silence_start, silence_end, ...]
      if (ripple_delete_enabled && split_positions.size() >= 2) {
        for (int i = 1; i < split_positions.size(); i += 2) {
          Selection s;
          s.in = split_positions.at(i - 1);
          s.out = split_positions.at(i);
          s.track = clip->track();
          silence_selections.append(s);
        }
      }

      // Split the clip at all positions
      panel_timeline->split_clip_at_positions(ca, j, split_positions);
    }
  }

  // Push the split action first
  if (ca->hasActions()) {
    amber::UndoStack.push(ca);
  } else {
    delete ca;
  }

  // If ripple delete is enabled, perform deletion and ripple in a separate action
  if (ripple_delete_enabled && silence_selections.size() > 0) {
    ComboAction* delete_ca = new ComboAction(tr("Auto-Cut Silence (Ripple Delete)"));

    // Process silence selections in reverse order (back to front) so positions don't shift
    // Build a list of (clip_index, silence_length) pairs for deletion
    QVector<QPair<int, long>> clips_and_lengths;

    for (int i = 0; i < amber::ActiveSequence->clips.size(); i++) {
      ClipPtr c = amber::ActiveSequence->clips.at(i);
      if (c != nullptr && !c->undeletable) {
        // Check if this clip matches exactly with any silence selection
        for (const auto& s : silence_selections) {
          if (c->track() == s.track && c->timeline_in() == s.in && c->timeline_out() == s.out) {
            long silence_length = s.out - s.in;
            clips_and_lengths.append(qMakePair(i, silence_length));
            break;
          }
        }
      }
    }

    // Delete and ripple in reverse order (from end to beginning) so timeline positions remain valid
    for (int i = clips_and_lengths.size() - 1; i >= 0; i--) {
      int clip_idx = clips_and_lengths.at(i).first;
      long silence_length = clips_and_lengths.at(i).second;

      ClipPtr clip_to_delete = amber::ActiveSequence->clips.at(clip_idx);
      long ripple_point = clip_to_delete->timeline_in();

      delete_ca->append(new DeleteClipAction(amber::ActiveSequence.get(), clip_idx));

      // If gap size is set, only ripple by (silence_length - gap_size)
      // This leaves a gap where the silence was
      long ripple_length = silence_length - current_gap_size;
      ripple_clips(delete_ca, amber::ActiveSequence.get(), ripple_point, -ripple_length);
    }

    if (delete_ca->hasActions()) {
      amber::UndoStack.push(delete_ca);
    } else {
      delete delete_ca;
    }
  }
}
