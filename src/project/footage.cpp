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

#include "footage.h"

#include <QDebug>
#include <QFileInfo>
#include <QtMath>
#include <QPainter>

#include "project/previewgenerator.h"
#include "engine/clip.h"

Footage::Footage() {
  ready = (false);
  preview_gen = (nullptr);
  invalid = (false);
  in = (0);
  out = (0);
  speed = (1.0);
  alpha_is_premultiplied = (false);
  proxy = (false);
  start_number = 0;

  ready_lock.lock();
}

QString Footage::decode_url(bool exporting) const {
  if (!exporting && proxy && !proxy_path.isEmpty() && QFileInfo::exists(proxy_path)) return proxy_path;
  return url;
}

Footage::~Footage() {
  reset();
  // The probe releases the lock the constructor took, but footage destroyed before its probe ran or finished (load
  // cancelled or failed, probe cancelled) still holds it; destroying a locked QMutex is undefined. reset() waited for
  // the probe, so nothing else touches the lock now: take it if free, then release it either way.
  ready_lock.tryLock();
  ready_lock.unlock();
}

void Footage::reset() {
  // Load once: the generator thread clears preview_gen when it finishes
  PreviewGenerator* gen = preview_gen.load();
  if (gen != nullptr) {
    gen->cancel();
  }
  video_tracks.clear();
  audio_tracks.clear();
  ready = false;
}

long Footage::get_length_in_frames(double frame_rate) {
  if (length >= 0) {
    if (qFuzzyIsNull(speed)) return LONG_MAX;
    return static_cast<long>(std::floor((double(length) / double(AV_TIME_BASE)) * frame_rate / speed));
  }
  return 0;
}

FootageStream* Footage::get_stream_from_file_index(bool video, int index) {
  if (video) {
    for (auto & video_track : video_tracks) {
      if (video_track.file_index == index) {
        return &video_track;
      }
    }
  } else {
    for (auto & audio_track : audio_tracks) {
      if (audio_track.file_index == index) {
        return &audio_track;
      }
    }
  }
  return nullptr;
}
