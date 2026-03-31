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

#include <QDialogButtonBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSettings>

#include <algorithm>

#include "engine/sequence.h"
#include "engine/undo/undo_clip.h"
#include "panels/panels.h"
#include "panels/timeline.h"
#include "rendering/renderfunctions.h"

AutoCutSilenceDialog::AutoCutSilenceDialog(QWidget *parent, QVector<int> clips) :
  QDialog(parent),
  clips_(clips)
{
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

int AutoCutSilenceDialog::exec()
{
  default_attack_threshold = 5;
  current_attack_threshold = 5;
  default_attack_time = 2;
  current_attack_time = 2;
  default_release_threshold = 2;
  current_release_threshold = 2;
  default_release_time = 5;
  current_release_time = 5;

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

  QSettings settings;
  ripple_delete_checkbox->setChecked(settings.value("AutoCutSilence/RippleDelete", false).toBool());
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

  QSettings settings;
  settings.setValue("AutoCutSilence/RippleDelete", ripple_delete_enabled);
  settings.setValue("AutoCutSilence/GapSize", current_gap_size);

  CutResult result = cut_silence();

  if (result == NoAudioClips) {
    QMessageBox::information(this, tr("Cut Silence"),
                             tr("No audio found in the selected clips."));
    return;
  }
  if (result == NoAudioDetected) {
    QMessageBox::information(this, tr("Cut Silence"),
                             tr("No audio detected above threshold — nothing to cut."));
    return;
  }
  if (result == NoSilenceDetected) {
    QMessageBox::information(this, tr("Cut Silence"),
                             tr("No silence detected below threshold — nothing to cut."));
    return;
  }

  update_ui(true);
  QDialog::accept();
}

AutoCutSilenceDialog::CutResult AutoCutSilenceDialog::cut_silence() {
  if (amber::ActiveSequence == nullptr) return NoAudioClips;

  ComboAction* ca = new ComboAction(tr("Auto-Cut Silence"));

  // Silence segments collected across all clips for ripple delete
  struct SilenceSegment {
    long in;
    long out;
    int track;
  };
  QVector<SilenceSegment> silence_segments;
  bool any_audio_processed = false;
  bool any_attack_triggered = false;

  // Loop over clips provided to this dialog
  for (int j : clips_) {

    Clip* clip = amber::ActiveSequence->clips.at(j).get();

    // Check if this clip is an audio footage clip
    if (clip->track() >= 0
        && clip->media() != nullptr
        && clip->media_stream()->preview_done) { // TODO provide warning for preview not being done

      any_audio_processed = true;

      QVector<long> split_positions;

      long clip_start = clip->timeline_in();
      long clip_end = clip->timeline_out();
      const FootageStream* ms = clip->media_stream();

      long media_length = clip->media_length();
      int preview_size = ms->audio_preview.length();
      float chunk_size = (float)preview_size/media_length;  // how many audio samples to read for each fotogram

      int sample_size = qMax(current_attack_time, current_release_time)+1;

      bool attack = false;    // status flags
      bool release = false;
      long audio_seg_start = -1; // track current audio segment start for ripple delete

      // Audio segments collected during detection (for building silence as complement)
      struct AudioSegment { long in; long out; };
      QVector<AudioSegment> audio_segments;

      QVector<qint8> vols;
      vols.resize(sample_size);
      vols.fill(0);

      // loop through the visible portion of the clip
      for (long i=clip_start;i<clip_end;i++) {
        long start = ((i-clip_start+clip->clip_in())*chunk_size);  // offset by clip_in to handle trimmed clips
        int circular_index = i%sample_size;

        // read the current sample into the circular array
        qint8 tmp = 0;
        for (int k=start; k<start+chunk_size; k++){
          tmp = qMax(tmp, qint8(qRound(double(ms->audio_preview.at(k)))));
        }
        vols[circular_index] = tmp;

        int overthreshold = 0;
        int cut_idx = 0;  //how much to cut (backwards)

        // if current volume value is above threshold
        if (vols[circular_index] >= current_attack_threshold && !attack){   // if we get one sample over the threshold
          for(int k=0; k<sample_size; k++){                       // count how many times this happened before within sample_size range (avoid false activations)
            int back_idx = (((circular_index-k)%sample_size)+sample_size)%sample_size;  // positive modulus
            if(vols[back_idx] > current_attack_threshold){
              overthreshold++;
              cut_idx = k+1;
            }
          }
          // if we reached threshold over the set tolerance
          if(overthreshold >= current_attack_time){
            audio_seg_start = qMax(i-cut_idx, clip_start);
            split_positions.append(audio_seg_start);
            attack = true;
            any_attack_triggered = true;
            release = false;
          }
          overthreshold = 0;
          cut_idx = 0;
        }else if (vols[circular_index] < current_release_threshold && !release){   // if we get one sample under the threshold
          for(int k=0; k<sample_size; k++){                               // count how many times this happened before within sample_size range
            int back_idx = (((circular_index-k)%sample_size)+sample_size)%sample_size;  // positive modulus
            if(vols[back_idx] < current_release_threshold)
              overthreshold++;
          }
          // if we reached threshold over the set tolerance
          if(overthreshold >= current_release_time){        // must be <= sample_size
            if (audio_seg_start >= 0) {
              audio_segments.append({audio_seg_start, i});
            }
            audio_seg_start = -1;
            attack = false;
            release = true;
            split_positions.append(i);
          }
          overthreshold = 0;
        }
      }

      // If audio was still playing at end of clip, close the segment
      if (attack && audio_seg_start >= 0) {
        audio_segments.append({audio_seg_start, clip_end});
      }

      // Build silence segments as the complement of audio segments within [clip_start, clip_end].
      // This is independent of the split_positions alternation pattern (which depends on
      // whether the clip starts with silence or audio).
      if (ripple_delete_enabled && !audio_segments.isEmpty()) {
        int track = clip->track();
        long cursor = clip_start;
        for (const auto& seg : audio_segments) {
          long seg_in = qMax(seg.in, clip_start);
          if (cursor < seg_in) {
            silence_segments.append({cursor, seg_in, track});
          }
          cursor = seg.out;
        }
        if (cursor < clip_end) {
          silence_segments.append({cursor, clip_end, track});
        }
      }

      // Filter out positions at clip boundaries (no-op splits)
      split_positions.erase(
        std::remove_if(split_positions.begin(), split_positions.end(),
          [clip_start, clip_end](long pos) { return pos <= clip_start || pos >= clip_end; }),
        split_positions.end());

      if (!split_positions.isEmpty()) {
        panel_timeline->split_clip_at_positions(ca, j, split_positions);
      }
    }

  }

  if (!any_audio_processed) {
    delete ca;
    return NoAudioClips;
  }

  if (!ca->hasActions()) {
    delete ca;
    if (!ripple_delete_enabled || silence_segments.isEmpty())
      return any_attack_triggered ? NoSilenceDetected : NoAudioDetected;
  } else {
    amber::UndoStack.push(ca);
  }

  // Ripple delete: find and remove silence clips, shift everything back
  if (!ripple_delete_enabled || silence_segments.isEmpty()) return CutsApplied;

  // Sort back-to-front so ripples don't invalidate earlier positions
  std::sort(silence_segments.begin(), silence_segments.end(),
            [](const SilenceSegment& a, const SilenceSegment& b) {
              return a.in > b.in;
            });

  ComboAction* delete_ca = new ComboAction(tr("Auto-Cut Silence (Ripple Delete)"));

  for (const auto& seg : silence_segments) {
    long silence_length = seg.out - seg.in;
    long ripple_amount = qMax(0L, silence_length - (long)current_gap_size);

    // Find the clip matching this silence segment
    for (int i = 0; i < amber::ActiveSequence->clips.size(); i++) {
      ClipPtr c = amber::ActiveSequence->clips.at(i);
      if (c != nullptr && c->track() == seg.track
          && c->timeline_in() == seg.in && c->timeline_out() == seg.out) {

        // Delete this clip and all its linked clips (e.g. corresponding video track)
        delete_ca->append(new DeleteClipAction(amber::ActiveSequence.get(), i));
        for (int link : c->linked) {
          if (link >= 0 && link < amber::ActiveSequence->clips.size()) {
            ClipPtr linked_clip = amber::ActiveSequence->clips.at(link);
            if (linked_clip != nullptr
                && linked_clip->timeline_in() == seg.in
                && linked_clip->timeline_out() == seg.out) {
              delete_ca->append(new DeleteClipAction(amber::ActiveSequence.get(), link));
            }
          }
        }

        if (ripple_amount > 0) {
          ripple_clips(delete_ca, amber::ActiveSequence.get(), seg.in, -ripple_amount);
        }
        break;
      }
    }
  }

  if (delete_ca->hasActions()) {
    amber::UndoStack.push(delete_ca);
  } else {
    delete delete_ca;
  }

  return CutsApplied;
}
