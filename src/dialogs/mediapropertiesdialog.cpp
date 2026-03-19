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

#include "mediapropertiesdialog.h"

#include <QGridLayout>
#include <QLabel>
#include <QComboBox>
#include <QLineEdit>
#include <QDialogButtonBox>
#include <QTreeWidgetItem>
#include <QGroupBox>
#include <QListWidget>
#include <QCheckBox>
#include <QSpinBox>

#include "project/footage.h"
#include "project/media.h"
#include "panels/project.h"
#include "engine/undo/undo.h"
#include "engine/undo/undostack.h"

MediaPropertiesDialog::MediaPropertiesDialog(QWidget *parent, Media *i) :
  QDialog(parent),
  item(i)
{
  setWindowTitle(tr("\"%1\" Properties").arg(i->get_name()));
  setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

  QGridLayout* grid = new QGridLayout(this);

  int row = 0;

  Footage* f = item->to_footage();

  grid->addWidget(new QLabel(tr("Tracks:"), this), row, 0, 1, 2);
  row++;

  track_list = new QListWidget(this);
  for (const auto & fs : f->video_tracks) {
    QListWidgetItem* item = new QListWidgetItem(
          tr("Video %1: %2x%3 %4FPS").arg(
            QString::number(fs.file_index),
            QString::number(fs.video_width),
            QString::number(fs.video_height),
            QString::number(fs.video_frame_rate)
            ),
          track_list
          );
    item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
    item->setCheckState(fs.enabled ? Qt::Checked : Qt::Unchecked);
    item->setData(Qt::UserRole+1, fs.file_index);
    track_list->addItem(item);
  }
  for (const auto & fs : f->audio_tracks) {
    QListWidgetItem* item = new QListWidgetItem(
          tr("Audio %1: %2Hz %3").arg(
            QString::number(fs.file_index),
            QString::number(fs.audio_frequency),
            tr("%n channel(s)", "", fs.audio_channels)
            ),
          track_list
          );
    item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
    item->setCheckState(fs.enabled ? Qt::Checked : Qt::Unchecked);
    item->setData(Qt::UserRole+1, fs.file_index);
    track_list->addItem(item);
  }
  grid->addWidget(track_list, row, 0, 1, 2);
  row++;

  if (f->video_tracks.size() > 0) {
    // frame conforming
    if (!f->video_tracks.at(0).infinite_length) {
      grid->addWidget(new QLabel(tr("Conform to Frame Rate:"), this), row, 0);
      conform_fr = new QDoubleSpinBox(this);
      conform_fr->setMinimum(0.01);
      conform_fr->setValue(f->video_tracks.at(0).video_frame_rate * f->speed);
      grid->addWidget(conform_fr, row, 1);
    }

    row++;

    // premultiplied alpha mode
    premultiply_alpha_setting = new QCheckBox(tr("Alpha is Premultiplied"), this);
    premultiply_alpha_setting->setChecked(f->alpha_is_premultiplied);
    grid->addWidget(premultiply_alpha_setting, row, 0);

    row++;

    // deinterlacing mode
    interlacing_box = new QComboBox(this);
    interlacing_box->addItem(
          tr("Auto (%1)").arg(
            get_interlacing_name(f->video_tracks.at(0).video_auto_interlacing)
            )
          );
    interlacing_box->addItem(get_interlacing_name(VIDEO_PROGRESSIVE));
    interlacing_box->addItem(get_interlacing_name(VIDEO_TOP_FIELD_FIRST));
    interlacing_box->addItem(get_interlacing_name(VIDEO_BOTTOM_FIELD_FIRST));

    interlacing_box->setCurrentIndex(
          (f->video_tracks.at(0).video_auto_interlacing == f->video_tracks.at(0).video_interlacing)
          ? 0
          : f->video_tracks.at(0).video_interlacing + 1);

    grid->addWidget(new QLabel(tr("Interlacing:"), this), row, 0);
    grid->addWidget(interlacing_box, row, 1);

    row++;
  }

  name_box = new QLineEdit(item->get_name(), this);
  grid->addWidget(new QLabel(tr("Name:"), this), row, 0);
  grid->addWidget(name_box, row, 1);
  row++;

  QDialogButtonBox* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
  buttons->setCenterButtons(true);
  grid->addWidget(buttons, row, 0, 1, 2);

  connect(buttons, &QDialogButtonBox::accepted, this, &MediaPropertiesDialog::accept);
  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

void MediaPropertiesDialog::accept() {
  Footage* f = item->to_footage();

  ComboAction* ca = new ComboAction();

  // set track enable
  for (int i=0;i<track_list->count();i++) {
    QListWidgetItem* item = track_list->item(i);
    const QVariant& data = item->data(Qt::UserRole+1);
    if (!data.isNull()) {
      int index = data.toInt();
      bool found = false;
      for (auto & video_track : f->video_tracks) {
        if (video_track.file_index == index) {
          video_track.enabled = (item->checkState() == Qt::Checked);
          found = true;
          break;
        }
      }
      if (!found) {
        for (auto & audio_track : f->audio_tracks) {
          if (audio_track.file_index == index) {
            audio_track.enabled = (item->checkState() == Qt::Checked);
            break;
          }
        }
      }
    }
  }

  bool refresh_clips = false;

  // set interlacing
  if (f->video_tracks.size() > 0) {
    if (interlacing_box->currentIndex() > 0) {
      ca->append(new SetInt(&f->video_tracks[0].video_interlacing, interlacing_box->currentIndex() - 1));
    } else {
      ca->append(new SetInt(&f->video_tracks[0].video_interlacing, f->video_tracks.at(0).video_auto_interlacing));
    }

    // set frame rate conform
    if (!f->video_tracks.at(0).infinite_length) {
      if (!qFuzzyCompare(conform_fr->value(), f->video_tracks.at(0).video_frame_rate)) {
        ca->append(new SetDouble(&f->speed, f->speed, conform_fr->value()/f->video_tracks.at(0).video_frame_rate));
        refresh_clips = true;
      }
    }

    // set premultiplied alpha
    f->alpha_is_premultiplied = premultiply_alpha_setting->isChecked();
  }

  // set name
  MediaRename* mr = new MediaRename(item, name_box->text());

  ca->append(mr);
  ca->appendPost(new CloseAllClipsCommand());
  ca->appendPost(new UpdateFootageTooltip(item));
  if (refresh_clips) {
    ca->appendPost(new RefreshClips(item));
  }
  ca->appendPost(new UpdateViewer());

  amber::UndoStack.push(ca);

  QDialog::accept();
}
