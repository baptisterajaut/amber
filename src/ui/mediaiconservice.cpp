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

#include "mediaiconservice.h"

#include <QThread>

const int kThrobberLimit = 20;
const int kThrobberSize = 50;

#include "project/projectmodel.h"
#include "ui/icons.h"

std::unique_ptr<MediaIconService> amber::media_icon_service;

MediaIconService::MediaIconService() {
  // set up animation timer
  throbber_animator_.setInterval(20);
  connect(&throbber_animator_, &QTimer::timeout, this, &MediaIconService::AnimationUpdate);

  // set up pixmap
  throbber_pixmap_ = QPixmap(":/icons/throbber.png");
}

namespace {

// True if `m` is currently in the project tree under `parent`. Only dereferences nodes that are in the tree.
bool media_in_tree(Media* parent, Media* m) {
  for (int i = 0; i < parent->childCount(); i++) {
    Media* child = parent->child(i);
    if (child == m || media_in_tree(child, m)) return true;
  }
  return false;
}

}  // namespace

void MediaIconService::StopThrobberFor(Media* media) {
  // if this icon is already part of the throbber animation loop, remove it
  if (throbber_items_.contains(media)) {
    throbber_lock_.lock();

    throbber_items_.removeAll(media);

    throbber_lock_.unlock();

    // if we aren't animating anything, no need to run the timer for now
    if (throbber_items_.empty()) {
      // ensure timer function is called in its own thread
      QMetaObject::invokeMethod(&throbber_animator_, "stop", Qt::QueuedConnection);
    }
  }
}

void MediaIconService::SetMediaIcon(Media *media, int icon_type) {
  if (QThread::currentThread() != thread()) {
    // PreviewGenerator / LoadThread call this from their own thread, but the project model, its views and the
    // throbber list are GUI-thread only: re-dispatch. Media isn't a QObject, so a MediaPtr keeps it alive meanwhile.
    Media* parent = media->parentItem();
    MediaPtr keep_alive = (parent != nullptr) ? parent->get_shared_ptr(media) : MediaPtr();
    QMetaObject::invokeMethod(
        this,
        [this, media, keep_alive, icon_type] {
          // Removed from the project (or never found): ProjectModel::set_icon would build an index from a stale
          // row, so only drop it from the throbber loop. `media` is compared here, never dereferenced.
          if (keep_alive == nullptr || !media_in_tree(amber::project_model.get_root(), media)) {
            StopThrobberFor(media);
            emit IconChanged();
            return;
          }
          SetMediaIcon(media, icon_type);
        },
        Qt::QueuedConnection);
    return;
  }

  StopThrobberFor(media);

  switch (icon_type) {
  case ICON_TYPE_VIDEO:
    amber::project_model.set_icon(media, amber::icon::MediaVideo);
    break;
  case ICON_TYPE_AUDIO:
    amber::project_model.set_icon(media, amber::icon::MediaAudio);
    break;
  case ICON_TYPE_IMAGE:
    amber::project_model.set_icon(media, amber::icon::MediaImage);
    break;
  case ICON_TYPE_LOADING:
    throbber_items_.append(media);

    // if the animation timer isn't running, start it
    if (!throbber_animator_.isActive()) {
      // set starting frame to 0
      throbber_animation_frame_ = 0;

      // ensure timer function is called in its own thread
      QMetaObject::invokeMethod(&throbber_animator_, "start", Qt::QueuedConnection);
    }
    break;
  case ICON_TYPE_ERROR:
    amber::project_model.set_icon(media, amber::icon::MediaError);
    break;
  }

  emit IconChanged();
}

void MediaIconService::AnimationUpdate() {
  if (throbber_animation_frame_ == kThrobberLimit) {
    throbber_animation_frame_ = 0;
  }

  QIcon throbber_ico = QIcon(throbber_pixmap_.copy(kThrobberSize*throbber_animation_frame_, 0, kThrobberSize, kThrobberSize));

  throbber_lock_.lock();

  for (auto throbber_item : throbber_items_) {
    amber::project_model.set_icon(throbber_item, throbber_ico);
  }

  throbber_lock_.unlock();

  throbber_animation_frame_++;
}
