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
    QMessageBox::information(this, tr("Cut Silence"), tr("No audio found in the selected clips."));
    return;
  }
  if (result == NoAudioDetected) {
    QMessageBox::information(this, tr("Cut Silence"), tr("No audio detected above threshold — nothing to cut."));
    return;
  }
  if (result == NoSilenceDetected) {
    QMessageBox::information(this, tr("Cut Silence"), tr("No silence detected below threshold — nothing to cut."));
    return;
  }

  update_ui(true);
  QDialog::accept();
}

namespace {

struct AudioSegment {
  long in;
  long out;
};

struct SilenceSegment {
  long in;
  long out;
  int track;
};

// Check whether `vols[circular_index]` crosses the attack threshold enough times in the
// circular buffer to trigger an attack event.  Returns the number of samples over threshold
// found (overthreshold) and sets cut_idx to how far back to cut.
static void count_overthreshold_attack(const QVector<qint8>& vols, int circular_index, int sample_size,
                                       int attack_threshold, int& overthreshold, int& cut_idx) {
  overthreshold = 0;
  cut_idx = 0;
  for (int k = 0; k < sample_size; k++) {
    int back_idx = (((circular_index - k) % sample_size) + sample_size) % sample_size;
    if (vols[back_idx] > attack_threshold) {
      overthreshold++;
      cut_idx = k + 1;
    }
  }
}

// Count how many samples in the circular buffer are below the release threshold.
static int count_underthreshold_release(const QVector<qint8>& vols, int circular_index, int sample_size,
                                        int release_threshold) {
  int count = 0;
  for (int k = 0; k < sample_size; k++) {
    int back_idx = (((circular_index - k) % sample_size) + sample_size) % sample_size;
    if (vols[back_idx] < release_threshold) count++;
  }
  return count;
}

// Analyse the audio data in `clip` for silence/audio transitions.
// Appends to `split_positions`, `audio_segments`, and sets `any_attack_triggered`.
static void analyse_clip_audio(Clip* clip, int attack_threshold, int attack_time, int release_threshold,
                                int release_time, QVector<long>& split_positions,
                                QVector<AudioSegment>& audio_segments, bool& any_attack_triggered) {
  long clip_start = clip->timeline_in();
  long clip_end = clip->timeline_out();
  const FootageStream* ms = clip->media_stream();

  long media_length = clip->media_length();
  int preview_size = ms->audio_preview.length();
  float chunk_size = (float)preview_size / media_length;

  int sample_size = qMax(attack_time, release_time) + 1;

  bool attack = false;
  bool release = false;
  long audio_seg_start = -1;

  QVector<qint8> vols;
  vols.resize(sample_size);
  vols.fill(0);

  for (long i = clip_start; i < clip_end; i++) {
    long start = ((i - clip_start + clip->clip_in()) * chunk_size);
    int circular_index = i % sample_size;

    qint8 tmp = 0;
    for (int k = start; k < start + chunk_size; k++) {
      tmp = qMax(tmp, qint8(qRound(double(ms->audio_preview.at(k)))));
    }
    vols[circular_index] = tmp;

    if (vols[circular_index] >= attack_threshold && !attack) {
      int overthreshold = 0;
      int cut_idx = 0;
      count_overthreshold_attack(vols, circular_index, sample_size, attack_threshold, overthreshold, cut_idx);
      if (overthreshold >= attack_time) {
        audio_seg_start = qMax(i - cut_idx, clip_start);
        split_positions.append(audio_seg_start);
        attack = true;
        any_attack_triggered = true;
        release = false;
      }
    } else if (vols[circular_index] < release_threshold && !release) {
      int underthreshold = count_underthreshold_release(vols, circular_index, sample_size, release_threshold);
      if (underthreshold >= release_time) {
        if (audio_seg_start >= 0) {
          audio_segments.append({audio_seg_start, i});
        }
        audio_seg_start = -1;
        attack = false;
        release = true;
        split_positions.append(i);
      }
    }
  }

  if (attack && audio_seg_start >= 0) {
    audio_segments.append({audio_seg_start, clip_end});
  }
}

// Build silence segments as the complement of audio segments within [clip_start, clip_end].
static void collect_silence_segments(const QVector<AudioSegment>& audio_segments, long clip_start, long clip_end,
                                     int track, QVector<SilenceSegment>& silence_segments) {
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

// Execute the ripple-delete phase: find and remove silent clip segments, shift clips back.
static void apply_ripple_delete(const QVector<SilenceSegment>& silence_segments, long gap_size) {
  QVector<SilenceSegment> sorted = silence_segments;
  std::sort(sorted.begin(), sorted.end(),
            [](const SilenceSegment& a, const SilenceSegment& b) { return a.in > b.in; });

  ComboAction* delete_ca = new ComboAction(QCoreApplication::translate("AutoCutSilenceDialog",
                                                                        "Auto-Cut Silence (Ripple Delete)"));

  for (const auto& seg : sorted) {
    long silence_length = seg.out - seg.in;
    long ripple_amount = qMax(0L, silence_length - (long)gap_size);

    for (int i = 0; i < amber::ActiveSequence->clips.size(); i++) {
      ClipPtr c = amber::ActiveSequence->clips.at(i);
      if (c == nullptr || c->track() != seg.track || c->timeline_in() != seg.in || c->timeline_out() != seg.out) {
        continue;
      }
      delete_ca->append(new DeleteClipAction(amber::ActiveSequence.get(), i));
      for (int link : c->linked) {
        if (link < 0 || link >= amber::ActiveSequence->clips.size()) continue;
        ClipPtr linked_clip = amber::ActiveSequence->clips.at(link);
        if (linked_clip != nullptr && linked_clip->timeline_in() == seg.in && linked_clip->timeline_out() == seg.out) {
          delete_ca->append(new DeleteClipAction(amber::ActiveSequence.get(), link));
        }
      }
      if (ripple_amount > 0) {
        ripple_clips(delete_ca, amber::ActiveSequence.get(), seg.in, -ripple_amount);
      }
      break;
    }
  }

  if (delete_ca->hasActions()) {
    amber::UndoStack.push(delete_ca);
  } else {
    delete delete_ca;
  }
}

}  // namespace

AutoCutSilenceDialog::CutResult AutoCutSilenceDialog::cut_silence() {
  if (amber::ActiveSequence == nullptr) return NoAudioClips;

  ComboAction* ca = new ComboAction(tr("Auto-Cut Silence"));

  QVector<SilenceSegment> silence_segments;
  bool any_audio_processed = false;
  bool any_attack_triggered = false;

  for (int j : clips_) {
    Clip* clip = amber::ActiveSequence->clips.at(j).get();

    if (clip->track() < 0 || clip->media() == nullptr || !clip->media_stream()->preview_done) continue;

    any_audio_processed = true;

    QVector<long> split_positions;
    QVector<AudioSegment> audio_segments;

    analyse_clip_audio(clip, current_attack_threshold, current_attack_time, current_release_threshold,
                       current_release_time, split_positions, audio_segments, any_attack_triggered);

    long clip_start = clip->timeline_in();
    long clip_end = clip->timeline_out();

    if (ripple_delete_enabled && !audio_segments.isEmpty()) {
      collect_silence_segments(audio_segments, clip_start, clip_end, clip->track(), silence_segments);
    }

    split_positions.erase(
        std::remove_if(split_positions.begin(), split_positions.end(),
                       [clip_start, clip_end](long pos) { return pos <= clip_start || pos >= clip_end; }),
        split_positions.end());

    if (!split_positions.isEmpty()) {
      panel_timeline->split_clip_at_positions(ca, j, split_positions);
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

  if (!ripple_delete_enabled || silence_segments.isEmpty()) return CutsApplied;

  apply_ripple_delete(silence_segments, current_gap_size);

  return CutsApplied;
}
