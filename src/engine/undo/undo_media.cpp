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

#include "undo_media.h"

#include "core/appcontext.h"
#include "project/footage.h"
#include "project/media.h"
#include "project/previewgenerator.h"
#include "project/projectmodel.h"
#include "engine/sequence.h"

AddMediaCommand::AddMediaCommand(MediaPtr iitem, Media *iparent) :
  item(std::move(iitem)),
  parent(iparent)
  
{
  doRedo();
}

void AddMediaCommand::doUndo() {
  if (!parent) {
    qWarning() << "AddMediaCommand::doUndo: parent is null";
    return;
  }
  amber::project_model.removeChild(parent, item.get());
  done_ = false;
}

void AddMediaCommand::doRedo() {
  if (!parent) {
    qWarning() << "AddMediaCommand::doRedo: parent is null";
    return;
  }
  if (!done_) {
    amber::project_model.appendChild(parent, item);
    done_ = true;
  }
}

DeleteMediaCommand::DeleteMediaCommand(const MediaPtr& i) :
  item(i),
  parent(i->parentItem())
{
}

void DeleteMediaCommand::doUndo() {
  if (!parent) {
    qWarning() << "DeleteMediaCommand::doUndo: parent is null";
    return;
  }
  amber::project_model.appendChild(parent, item);
}

void DeleteMediaCommand::doRedo() {
  if (!parent) {
    qWarning() << "DeleteMediaCommand::doRedo: parent is null";
    return;
  }
  amber::project_model.removeChild(parent, item.get());
}

ReplaceMediaCommand::ReplaceMediaCommand(MediaPtr i, QString s) {
  item = std::move(i);
  new_filename = s;
  Footage* f = item ? item->to_footage() : nullptr;
  if (!f) {
    qWarning() << "ReplaceMediaCommand: to_footage() is null";
  } else {
    old_filename = f->url;
  }
}

void ReplaceMediaCommand::replace(QString& filename) {
  if (!amber::app_ctx) {
    qWarning() << "ReplaceMediaCommand::replace: app_ctx is null";
    return;
  }
  // close any clips currently using this media
  QVector<Media*> all_sequences = amber::app_ctx->listAllSequences();
  for (auto all_sequence : all_sequences) {
    if (!all_sequence) continue;
    Sequence* s = all_sequence->to_sequence().get();
    if (!s) continue;
    for (const auto& c : s->clips) {
      if (c != nullptr && c->media() == item.get() && c->IsOpen()) {
        c->Close(true);
        c->replaced = true;
      }
    }
  }

  // replace media
  QStringList files;
  files.append(filename);
  amber::app_ctx->processFileList(files, false, item, nullptr);
  PreviewGenerator::AnalyzeMedia(item.get());
}

void ReplaceMediaCommand::doUndo() {
  replace(old_filename);


}

void ReplaceMediaCommand::doRedo() {
  replace(new_filename);
}

MediaMove::MediaMove() = default;

void MediaMove::doUndo() {
  for (int i=0;i<items.size();i++) {
    amber::project_model.moveChild(items.at(i), froms.at(i));
  }
}

void MediaMove::doRedo() {
  if (to == nullptr) to = amber::project_model.get_root();
  froms.resize(items.size());
  for (int i=0;i<items.size();i++) {
    if (!items.at(i)) {
      qWarning() << "MediaMove::doRedo: item at index" << i << "is null";
      continue;
    }
    Media* parent = items.at(i)->parentItem();
    froms[i] = parent;
    amber::project_model.moveChild(items.at(i), to);
  }
}

MediaRename::MediaRename(Media* iitem, QString ito) {
  item = iitem;
  from = iitem->get_name();
  to = ito;
}

void MediaRename::doUndo() {
  if (!item) {
    qWarning() << "MediaRename::doUndo: item is null";
    return;
  }
  item->set_name(from);
}

void MediaRename::doRedo() {
  if (!item) {
    qWarning() << "MediaRename::doRedo: item is null";
    return;
  }
  item->set_name(to);
}

UpdateFootageTooltip::UpdateFootageTooltip(Media *i) {
  item = i;
}

void UpdateFootageTooltip::doUndo() {
  redo();
}

void UpdateFootageTooltip::doRedo() {
  if (!item) {
    qWarning() << "UpdateFootageTooltip::doRedo: item is null";
    return;
  }
  item->update_tooltip();
}
