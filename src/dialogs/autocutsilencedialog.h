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

#ifndef SILENCEDIALOG_H
#define SILENCEDIALOG_H

#include <QCheckBox>
#include <QDialog>
#include <QMessageBox>
#include <QSpinBox>

#include "engine/clip.h"
#include "ui/labelslider.h"

class AutoCutSilenceDialog : public QDialog
{
  Q_OBJECT
public:
  AutoCutSilenceDialog(QWidget* parent, QVector<int> clips);
public slots:
  int exec() override;
private slots:
  void accept() override;
private:
  enum CutResult { NoAudioClips, NoAudioDetected, NoSilenceDetected, CutsApplied };
  CutResult cut_silence();

  QVector<int> clips_;

  LabelSlider* attack_threshold;
  LabelSlider* release_threshold;
  LabelSlider* attack_time;
  LabelSlider* release_time;
  QCheckBox* ripple_delete_checkbox;
  QSpinBox* gap_size_spinbox;

  int default_attack_threshold;
  int current_attack_threshold;
  int default_release_threshold;
  int current_release_threshold;
  int default_attack_time;
  int current_attack_time;
  int default_release_time;
  int current_release_time;
  bool ripple_delete_enabled;
  int current_gap_size;
};

#endif // SILENCEDIALOG_H
